#include "MainWindow.h"
#include "LocalizedText.h"
#include "plugin/PluginHost.h"
#include "plugin/PluginRuntime.h"
#include "plugin/UxpNativeHost.h"
#include "plugin/ScriptRuntime.h"
#include <QAction>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStringList>

namespace compositor {

// Plugins come in two flavours:
//   * native libraries in <exe>/plugins/*.dll (C ABI, see include/compositor_plugin.h)
//   * Photoshop UXP plugins, which run unmodified in the embedded JavaScript engine
void MainWindow::initializePlugins() {
    delete plugins_;
    plugins_ = new plugin::PluginHost();
    auto& runtime = plugins_->runtime();

    // The editor owns document state; plugins only read and edit through these
    // callbacks so every change stays undoable.
    runtime.setDocumentProvider([this]() -> Document* {
        auto* project = current();
        if (!project || !project->document) return nullptr;
        return &*project->document;
    });
    runtime.setActiveLayerProvider([this]() -> int32_t {
        auto* project = current();
        if (!project || !project->document) return -1;
        const auto& layers = project->document->layers;
        for (size_t index = 0; index < layers.size(); ++index) {
            if (layers[index].id == project->active) return int32_t(index);
        }
        return -1;
    });
    runtime.setEditInvoker([this](const char* name, const plugin::PluginRuntime::EditOperation& operation) {
        if (!current() || !current()->document) return false;
        edit(name, operation);
        return true;
    });
    runtime.setCommandInvoker([this](const QString& commandId) -> compositor_result {
        if (!commands_ || !commands_->invoke(commandId)) return COMPOSITOR_ERROR_NOT_FOUND;
        return COMPOSITOR_OK;
    });
    runtime.setLogSink([](const QString& message) { qInfo("[plugin] %s", qUtf8Printable(message)); });
    runtime.setTitleProvider([this] {
        const int index = tabs_ ? tabs_->currentIndex() : -1;
        return tabs_ && index >= 0 ? tabs_->tabText(index) : QStringLiteral("Compositor");
    });

    const QString root = plugin::defaultPluginDirectory();
    plugins_->scanDirectories({root});
    // UXP plugins are folders beside the native libraries.
    QStringList uxpErrors;
    plugins_->loadUxpDirectory(root, &uxpErrors);
    for (const auto& error : uxpErrors) qWarning("[plugin] %s", qUtf8Printable(error));
    rebuildPluginsMenu();
}

void MainWindow::importUxpPlugin() {
    const QString directory = QFileDialog::getExistingDirectory(
        this, ui::localizedText("选择 UXP 插件文件夹"), QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (directory.isEmpty()) return;

    plugin::UxpPlugin plugin;
    QString error;
    if (!plugin::UxpNativeHost::discover(directory, plugin, error)) {
        QMessageBox::warning(this, ui::localizedText("导入 UXP 插件"), error);
        return;
    }

    // Copy into the plugin folder so the plugin survives moving the original.
    const QString target = QDir(plugin::defaultPluginDirectory()).filePath(plugin.id);
    if (QDir::cleanPath(QDir(directory).absolutePath()) != QDir::cleanPath(QDir(target).absolutePath())) {
        QDir().mkpath(target);
        QDirIterator it(plugin.root, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString source = it.next();
            const QFileInfo info(source);
            const QString relative = QDir(plugin.root).relativeFilePath(source);
            const QString destination = QDir(target).filePath(relative);
            if (info.isDir()) {
                QDir().mkpath(destination);
            } else {
                QDir().mkpath(QFileInfo(destination).absolutePath());
                QFile::remove(destination);
                QFile::copy(source, destination);
            }
        }
        // Re-read from the installed copy so the entry path matches what runs.
        plugin::UxpPlugin installed;
        QString installError;
        if (!plugin::UxpNativeHost::discover(target, installed, installError)) {
            QMessageBox::warning(this, ui::localizedText("导入 UXP 插件"), installError);
            return;
        }
        plugin = installed;
    }

    QStringList errors;
    plugins_->loadUxpDirectory(plugin::defaultPluginDirectory(), &errors);
    rebuildPluginsMenu();

    QMessageBox::information(this, ui::localizedText("导入 UXP 插件"),
        QStringLiteral("插件：%1\n标识：%2\n入口：%3\n\n已安装到：%4\n可从“插件”菜单运行。")
            .arg(plugin.name, plugin.id, plugin.entryPath, QDir::toNativeSeparators(target)));
}

void MainWindow::rebuildPluginsMenu() {
    auto* bar = menuBar();
    // The UXP importer needs the embedded engine; probe once so the menu can say
    // why it is unavailable instead of failing after the user picks a folder.
    QString engineError;
    {
        plugin::ScriptRuntime probe;
        if (!probe.available(engineError)) uxpEngineError_ = engineError;
        else uxpEngineError_.clear();
    }
    if (!pluginsMenu_) {
        pluginsMenu_ = new QMenu(ui::localizedText("Plugins"), this);
        pluginsMenu_->setObjectName("pluginsMenu");
        bar->addMenu(pluginsMenu_);
    }
    pluginsMenu_->clear();

    auto* importer = pluginsMenu_->addAction(ui::localizedText("导入 UXP 插件…"));
    importer->setObjectName("importUxpPlugin");
    connect(importer, &QAction::triggered, this, [this] { importUxpPlugin(); });
    if (!uxpEngineError_.isEmpty()) {
        importer->setEnabled(false);
        importer->setToolTip(uxpEngineError_);
        auto* reason = pluginsMenu_->addAction(ui::localizedText("UXP 引擎不可用") + QStringLiteral("：") + uxpEngineError_);
        reason->setEnabled(false);
    }

    bool anyAdded = false;
    if (plugins_ && !plugins_->plugins().empty()) {
        pluginsMenu_->addSeparator();
        anyAdded = true;
        for (const auto& plugin : plugins_->plugins()) {
            const QString label = plugin.name.isEmpty() ? plugin.id : plugin.name;
            auto* action = pluginsMenu_->addAction(label);
            const QString id = plugin.id;
            connect(action, &QAction::triggered, this, [this, id, label] {
                const auto result = plugins_->run(id);
                if (result != COMPOSITOR_OK) {
                    QMessageBox::warning(this, label,
                        ui::localizedText("插件执行失败") + QStringLiteral(" (%1)：%2")
                            .arg(int(result)).arg(plugins_->lastError()));
                }
            });
            if (!plugin.description.isEmpty()) action->setToolTip(plugin.description);
        }
    }
    if (plugins_ && !plugins_->uxp().plugins().empty()) {
        pluginsMenu_->addSeparator();
        anyAdded = true;
        for (const auto& plugin : plugins_->uxp().plugins()) {
            auto* action = pluginsMenu_->addAction(plugin.name.isEmpty() ? plugin.id : plugin.name);
            const plugin::UxpPlugin* target = &plugin;
            connect(action, &QAction::triggered, this, [this, target] {
                QString error;
                const auto result = plugins_->uxp().run(*target, error);
                if (result != COMPOSITOR_OK) {
                    QMessageBox::warning(this,
                        target->name.isEmpty() ? target->id : target->name,
                        ui::localizedText("插件执行失败") + QStringLiteral("：") + error);
                }
            });
            action->setToolTip(QStringLiteral("UXP · %1\n%2").arg(plugin.version, plugin.entryPath));
        }
    }
    Q_UNUSED(anyAdded);
}

}  // namespace compositor

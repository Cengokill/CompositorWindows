#include "ui/MainWindow.h"
#include <QAction>
#include <QApplication>
#include <QListWidget>
#include <QSpinBox>
#include <QTest>
#include <QToolButton>
#include <iostream>
#include <stdexcept>

using namespace compositor;
namespace {
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template<class T> T* control(MainWindow& window, const char* name) {
    auto* result = window.findChild<T*>(name); check(result != nullptr, name); return result;
}
void action(MainWindow& window, const QString& prefix) {
    for (auto* a : window.findChildren<QAction*>()) if (a->text().startsWith(prefix) && a->isEnabled()) {
        a->trigger(); return;
    }
    throw std::runtime_error("History action unavailable");
}
}
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    try {
        Document document; document.id = newId(); document.width = 4; document.height = 4;
        Layer layer; layer.id = newId(); layer.transform = {0, 0, 4, 4};
        auto red = Raster::filled(4, 4, {255, 0, 0, 255});
        auto blue = Raster::filled(4, 4, {0, 0, 255, 255});
        layer.raster = red; layer.animation = GifAnimation{{{red, 40}, {blue, 70}}, 0};
        document.layers.push_back(layer);
        MainWindow window(true); auto& project = window.addProject(document); window.show(); QApplication::processEvents();
        auto* frames = control<QListWidget>(window, "gifFrameList");
        auto* duplicate = control<QToolButton>(window, "gifDuplicate");
        auto* remove = control<QToolButton>(window, "gifDelete");
        auto* play = control<QToolButton>(window, "gifPlay");
        auto* delay = control<QSpinBox>(window, "gifFrameDelay");
        check(frames->count() == 2, "Frame list is missing imported frames");
        const auto initialHistory = project.history.undoCount();
        frames->setCurrentRow(1);
        check(project.document->layers[0].raster == blue && project.history.undoCount() == initialHistory,
              "Frame selection changed history or showed wrong pixels");
        duplicate->click();
        check(project.document->layers[0].animation->frames.size() == 3 && project.history.undoCount() == initialHistory + 1,
              "Frame duplication was not one history entry");
        action(window, "Undo");
        check(project.document->layers[0].animation->frames.size() == 2, "Undo did not restore frames");
        action(window, "Redo");
        check(project.document->layers[0].animation->frames.size() == 3, "Redo did not restore duplicated frame");
        remove->click();
        check(project.document->layers[0].animation->frames.size() == 2, "Frame deletion failed");
        action(window, "Undo");
        check(project.document->layers[0].animation->frames.size() == 3, "Undo did not restore deleted frame");
        delay->setValue(240); QMetaObject::invokeMethod(delay, "editingFinished", Qt::DirectConnection);
        check(project.document->layers[0].animation->frames[project.document->layers[0].animation->selectedFrame].delayMs == 240,
              "Frame duration did not change");
        action(window, "Undo");
        check(project.document->layers[0].animation->frames[project.document->layers[0].animation->selectedFrame].delayMs != 240,
              "Undo did not restore duration");
        const auto before = project.document->layers[0].animation->selectedFrame;
        const auto depth = project.history.undoCount(); play->click();
        check(QTest::qWaitFor([&] { return project.document->layers[0].animation->selectedFrame != before; }, 1000),
              "GIF playback did not advance");
        play->click(); const auto paused = project.document->layers[0].animation->selectedFrame;
        check(!QTest::qWaitFor([&] { return project.document->layers[0].animation->selectedFrame != paused; }, 200),
              "GIF playback continued after pause");
        check(project.history.undoCount() == depth, "Playback added history entries");
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

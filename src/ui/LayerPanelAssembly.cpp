#include "MainWindow.h"
#include "LayerPanel.h"
#include "LayerPanelWidgets.h"
#include "LayerPanelTokens.h"
#include "PreviewComboBox.h"
#include <QDockWidget>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPointer>
#include <QShortcut>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTreeWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace compositor {
namespace {
class LayerResizeEdge final : public QWidget {
public:
    LayerResizeEdge(QMainWindow* window, QDockWidget* dock)
        : QWidget(window), window_(window), dock_(dock) {
        namespace t = ui::layer_panel;
        setObjectName("layerResizeEdge");
        setFixedWidth(t::resizeHitWidth);
        setCursor(Qt::SplitHCursor);
        setToolTip("Drag to resize the panel");
        setAttribute(Qt::WA_NoSystemBackground);
        window->installEventFilter(this);
        dock->installEventFilter(this);
        QTimer::singleShot(0, this, [this] { sync(); });
    }
protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if ((watched == dock_ || watched == window_) &&
            (event->type() == QEvent::Move || event->type() == QEvent::Resize ||
             event->type() == QEvent::Show || event->type() == QEvent::Hide))
            QTimer::singleShot(0, this, [this] { sync(); });
        return false;
    }
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(ui::layer_panel::background));
        painter.fillRect(QRect(width() - ui::layer_panel::separatorWidth, 0,
                               ui::layer_panel::separatorWidth, height()),
                         QColor(ui::layer_panel::separator));
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton || !dock_ || !window_) return;
        startX_ = event->globalPosition().x();
        startWidth_ = dock_->width();
        dragging_ = true;
        event->accept();
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (!dragging_ || !dock_ || !window_) return;
        const int wanted = std::clamp(startWidth_ + int(std::lround(startX_ - event->globalPosition().x())),
                                      ui::layer_panel::minWidth, ui::layer_panel::maxWidth);
        window_->resizeDocks({dock_}, {wanted}, Qt::Horizontal);
        sync();
        event->accept();
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) dragging_ = false;
        event->accept();
    }
private:
    void sync() {
        if (!window_ || !dock_ || !dock_->isVisible()) { hide(); return; }
        const auto topLeft = dock_->mapTo(window_, QPoint{});
        setGeometry(topLeft.x() - width(),
                    topLeft.y(), width(), dock_->height());
        show();
        raise();
    }
    QPointer<QMainWindow> window_;
    QPointer<QDockWidget> dock_;
    qreal startX_{};
    int startWidth_{};
    bool dragging_{};
};
class LayerListStack final : public QStackedWidget {
public:
    using QStackedWidget::QStackedWidget;
    QSize minimumSizeHint() const override { return {0, 0}; }
};
}
QDockWidget* MainWindow::setupLayersDock() {
    namespace t = ui::layer_panel;
    auto* dock = new QDockWidget("Layers", this);
    dock->setObjectName("layersDock");
    auto* header = new QWidget(dock);
    header->setObjectName("layersHeader");
    header->setFixedHeight(t::headerHeight);
    header->setStyleSheet(QString("QWidget#layersHeader { background: %1; border: 0; }").arg(t::background));
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(t::headerLeading, t::headerVertical, t::headerTrailing, t::headerVertical);
    headerLayout->setSpacing(t::headerSpacing);
    auto* title = new QLabel("Layers", header);
    title->setObjectName("layersTitle");
    title->setFont(ui::layerPanelFont(t::titleFont, QFont::DemiBold));
    title->setStyleSheet(QString("QLabel { background: transparent; color: %1; font-family: '%2'; font-size: %3px; }")
                             .arg(t::primary, ui::layerPanelFont(t::titleFont).family()).arg(t::titleFont));
    auto* searchField = new ui::SearchField(header);
    searchField->setObjectName("layerSearchField");
    searchField->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* search = searchField->lineEdit();
    searchField->hide();
    auto* searchButton = new ui::IconButton("search", header);
    searchButton->setObjectName("layerSearchButton");
    searchButton->setAccessibleName("Search layers");
    searchButton->setToolTip("Search layers by name");
    auto* filterButton = new ui::IconButton("list-filter", header);
    filterButton->setObjectName("layerTypeFilterButton");
    filterButton->setAccessibleName("Filter the layer list");
    filterButton->setToolTip("Filter layer types (coming later)");
    connect(filterButton, &QToolButton::clicked, this, [] {
        // TODO: layer type filter and popover described in LAYERS_PANEL.md §3.3.
    });
    auto* selected = new ui::IconButton("list-checks", header);
    selected->setObjectName("layerSelectedFilter");
    selected->setCheckable(true);
    selected->setAccessibleName("Show selected layers only");
    selected->setToolTip("Show selected layers only");
    headerLayout->addWidget(title, 1);
    headerLayout->addWidget(searchField, 1);
    headerLayout->addWidget(searchButton);
    headerLayout->addWidget(filterButton);
    headerLayout->addWidget(selected);
    dock->setTitleBarWidget(header);

    auto refreshFilter = [this, search, selected] {
        if (auto* controller = ui::LayerPanelController::find(layers_))
            controller->setFilter(search->text(), selected->isChecked());
    };
    connect(search, &QLineEdit::textChanged, this, [refreshFilter](const QString&) { refreshFilter(); });
    connect(selected, &QToolButton::toggled, this, [refreshFilter, selected](bool checked) {
        selected->setActive(checked);
        refreshFilter();
    });
    auto closeSearch = [search, searchField, title, searchButton] {
        search->clear();
        searchField->hide();
        title->show();
        searchButton->setSymbol("search");
        searchButton->setAccessibleName("Search layers");
        searchButton->setToolTip("Search layers by name");
    };
    connect(searchButton, &QToolButton::clicked, this, [search, searchField, title, searchButton, closeSearch] {
        if (!searchField->isHidden()) { closeSearch(); return; }
        title->hide();
        searchField->show();
        searchButton->setSymbol("x");
        searchButton->setAccessibleName("Close layer search");
        searchButton->setToolTip("Close layer search");
        search->setFocus();
    });
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), search);
    escape->setContext(Qt::WidgetShortcut);
    connect(escape, &QShortcut::activated, this, [search, closeSearch] {
        if (search->text().isEmpty()) closeSearch();
        else search->clear();
    });

    auto* panel = new QWidget;
    panel->setObjectName("layersPanel");
    panel->setAutoFillBackground(true);
    auto palette = panel->palette();
    palette.setColor(QPalette::Window, QColor(t::background));
    panel->setPalette(palette);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(new ui::Divider(panel));
    auto* appearance = new QWidget(panel);
    appearance->setObjectName("layerAppearance");
    appearance->setFont(ui::layerPanelFont(t::captionFont));
    auto* appearanceLayout = new QVBoxLayout(appearance);
    appearanceLayout->setContentsMargins(t::appearancePadding, t::appearancePadding,
                                          t::appearancePadding, t::appearancePadding);
    appearanceLayout->setSpacing(t::appearanceSpacing);
    auto* blendRow = new QHBoxLayout;
    blendRow->setContentsMargins(0, 0, 0, 0);
    blendRow->setSpacing(t::appearanceSpacing);
    auto* blendLabel = new QLabel("Blend", appearance);
    blendLabel->setObjectName("layerBlendLabel");
    blendLabel->setFont(ui::layerPanelFont(t::captionFont));
    blendLabel->setStyleSheet(QString("QLabel { color: %1; font-family: '%2'; font-size: %3px; }")
                                  .arg(t::primary, ui::layerPanelFont(t::captionFont).family()).arg(t::captionFont));
    auto* blendPicker = new ui::PreviewComboBox(appearance);
    blend_ = blendPicker;
    blend_->setObjectName("layerBlend");
    blend_->setAccessibleName("Layer blend mode");
    for (size_t index = 0; index < blendNames.size(); ++index) {
        blend_->addItem(blendNames[index], int(index));
        if (index == 1 || index == 4 || index == 6 || index == 9 || index == 13 || index == 21)
            blend_->setItemData(int(index), true, Qt::UserRole + 1);
    }
    blendPicker->preview = [this](int index) {
        const auto data = blend_->itemData(index);
        if (data.isValid()) previewBlendMode(Blend(data.toInt()));
    };
    blendPicker->dismissed = [this] { previewBlendMode({}); };
    blendRow->addWidget(blendLabel);
    blendRow->addWidget(blend_, 1);
    appearanceLayout->addLayout(blendRow);
    opacity_ = new ui::Slider(Qt::Horizontal, appearance);
    opacity_->setObjectName("layerOpacity");
    opacity_->setRange(0, 100);
    opacity_->setDefaultValue(100);
    opacity_->withdrawPressStep = [this] { withdrawOpacityPress(); };
    opacity_->setAccessibleName("Layer opacity");
    appearanceLayout->addWidget(createLayerOpacityControl());
    layout->addWidget(appearance);
    layout->addWidget(new ui::Divider(panel));
    auto* list = new LayerListStack(panel);
    list->setObjectName("layerListStack");
    list->setMinimumWidth(0);
    list->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    layers_ = new QTreeWidget(list);
    layers_->setObjectName("layersTree");
    layers_->setHeaderHidden(true);
    layers_->setAccessibleName("Layer list");
    layers_->setMinimumWidth(0);
    layers_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    layers_->setStyleSheet(QString(
        "QTreeView#layersTree { background: %1; border: 0; }"
        "QTreeView#layersTree::item:selected { background: %2; }"
        "QTreeView#layersTree::item:hover { background: %3; }")
        .arg(t::background, t::selectedRow, t::hoveredRow));
    auto* empty = new ui::EmptyState(list);
    empty->setObjectName("layerEmptyState");
    list->addWidget(layers_);
    list->addWidget(empty);
    list->setCurrentWidget(empty);
    layout->addWidget(list, 1);
    dock->setWidget(panel);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    new LayerResizeEdge(this, dock);
    connect(layers_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item) {
        if (refreshing_ || !item) return;
        const auto id = item->data(0, Qt::UserRole).toString().toStdString();
        const bool visible = item->checkState(0) == Qt::Checked;
        edit("Layer Visibility", [&](Document& document) {
            for (auto& layer : document.layers)
                if (layer.id == id) {
                    layer.visible = visible;
                    if (!item->text(0).trimmed().isEmpty()) layer.name = item->text(0).trimmed().toStdString();
                }
        });
    });
    connect(blend_, &QComboBox::currentIndexChanged, this, [this](int index) {
        const auto data = blend_->itemData(index);
        if (!refreshing_ && data.isValid()) setLayerBlendMode(Blend(data.toInt()));
    });
    connect(opacity_, &QSlider::sliderPressed, this, [this] {
        opacityPressUndoCount_.reset();
        if (!canEditAppearance()) return;
        finishOpacityEdit();
        stopGifPlayback();
        opacityOwner_ = current();
        if (opacityOwner_) {
            opacityPressUndoCount_ = opacityOwner_->history.undoCount();
            opacityOwner_->history.begin("Layer Opacity", opacityOwner_->document, opacityOwner_->active);
        }
    });
    connect(opacity_, &QSlider::sliderReleased, this, [this] {
        finishOpacityEdit();
        if (auto* project = current(); project && project->canvas) project->canvas->scheduleNavigator();
        refresh(false);
    });
    connect(opacity_, &QSlider::valueChanged, this, [this](int value) {
        if (refreshing_ || !canEditAppearance()) return;
        if (auto* project = current(); project == opacityOwner_ && opacity_->isSliderDown()) {
            active()->opacity = value / 100.;
            // Keep the last frame while the existing viewport worker renders this value.
            project->canvas->updateLayerVisibilityComposite(true);
            refreshOpacityPercent();
        } else edit("Layer Opacity", [&](Document&) { active()->opacity = value / 100.; });
    });
    return dock;
}

void MainWindow::setupLayerFooter() {
    namespace t = ui::layer_panel;
    auto* dock = findChild<QDockWidget*>("layersDock");
    if (!dock) return;
    auto* panel = dock->widget();
    auto* layout = qobject_cast<QVBoxLayout*>(panel->layout());
    if (!layout) return;
    auto* editTarget = new QComboBox(panel);
    editTarget->setObjectName("layerEditTarget");
    editTarget->setAccessibleName("Layer editing target");
    editTarget->addItems({"Edit image", "Edit mask"});
    layout->addWidget(editTarget);
    editTarget->hide();
    connect(editTarget, &QComboBox::currentIndexChanged, this, [this](int value) {
        if (!refreshing_) maskCommand(int(value ? ui::MaskCommand::EditMask : ui::MaskCommand::EditImage));
    });
    layout->addWidget(new ui::Divider(panel));
    auto* footer = new QWidget(panel);
    footer->setObjectName("layerFooter");
    auto* row = new QHBoxLayout(footer);
    row->setContentsMargins(t::footerHorizontal, t::footerVertical, t::footerHorizontal, t::footerVertical);
    row->setSpacing(0);
    auto addButton = [this, footer, row](const QString& symbol, const QString& name, const QString& label,
                                         const QString& tip, std::function<void()> run) {
        auto* button = new ui::FooterIconButton(symbol, footer);
        button->setObjectName(name);
        button->setAccessibleName(label);
        button->setToolTip(tip);
        connect(button, &QToolButton::clicked, this, [this, run] {
            try { run(); }
            catch (const std::exception& error) { statusBar()->showMessage(error.what()); }
        });
        row->addWidget(button);
        return button;
    };
    addButton("square-plus", "layerAddButton", "New blank layer", "New blank layer (Ctrl+Shift+N)",
              [this] { newBlankLayer(); });
    addButton("folder-plus", "layerGroupButton", "Group", "Group selected layers (Ctrl+G)",
              [this] { layerCommand(1); });
    addButton("panel-top", "layerAddMaskButton", "Add Mask", "Add layer mask",
              [this] { maskCommand(int(ui::MaskCommand::AddReveal)); });
    auto addMenu = [footer, row](const QString& symbol, const QString& name, const QString& label,
                                 const QString& tip, const QString& menuName) {
        auto* button = new ui::FooterMenuButton(symbol, footer);
        button->setObjectName(name);
        button->setAccessibleName(label);
        button->setToolTip(tip);
        auto* menu = new QMenu(button);
        menu->setObjectName(menuName);
        button->setMenu(menu);
        row->addWidget(button);
        return menu;
    };
    auto* adjustmentMenu = addMenu("circle", "layerAdjustmentButton", "New adjustment layer",
                                   "New adjustment layer", "layerAdjustmentFooterMenu");
    auto* filterMenu = addMenu("checker-circle", "layerFilterLayerButton", "New filter layer",
                               "New filter layer", "layerFilterLayerFooterMenu");
    for (auto* barAction : menuBar()->actions()) {
        if (barAction->text() != "&Adjustments" || !barAction->menu()) continue;
        for (auto* action : barAction->menu()->actions()) {
            if (!action->menu() || action->text() != "New Adjustment Layer") continue;
            const QStringList imageAdjustments{"Hue/Saturation…", "Levels…", "Curves…", "Exposure…",
                "Gradient Map…", "Grain…", "Invert…", "Black and White…", "Color Balance…"};
            for (auto* item : action->menu()->actions())
                (imageAdjustments.contains(item->text()) ? adjustmentMenu : filterMenu)->addAction(item);
        }
    }
    auto* effectsMenu = addMenu("sparkles", "layerEffectsButton", "Effects", "Add layer effect", "layerEffectsMenu");
    const std::pair<const char*, int> effects[]{{"Stroke", 0}, {"Drop Shadow", 1}, {"Color Overlay", 2},
        {"Inner Shadow", 3}, {"Outer Glow", 4}, {"Inner Glow", 5}};
    for (const auto& effect : effects) {
        auto* item = effectsMenu->addAction(effect.first);
        bindCommand(item, "Effects", effect.first, [this, kind = effect.second] { editLayerEffect(kind); });
    }
    row->addStretch();
    addButton("trash", "layerDeleteButton", "Delete", "Delete selected layer", [this] {
        auto* project = current();
        if (project && project->maskSelected && layerSelection().ids.size() == 1)
            maskCommand(int(ui::MaskCommand::Delete));
        else layerCommand(6);
    });
    layout->addWidget(footer);
}

void MainWindow::refreshLayerFooterTooltips() {
    auto* button = findChild<QToolButton*>("layerDeleteButton");
    if (!button) return;
    const auto* project = current();
    const auto count = project ? project->selected.size() : 0;
    button->setToolTip(project && project->maskSelected && count == 1 ? "Delete layer mask"
        : count > 1 ? "Delete selected layers" : "Delete selected layer");
}
}

#include "ui/MainWindow.h"
#include "ui/LayerPanelTokens.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QSettings>
#include <QShortcut>
#include <QSlider>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <iostream>
#include <stdexcept>

using namespace compositor;
namespace t = compositor::ui::layer_panel;

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("CompositorFixture");
    QCoreApplication::setApplicationName("LayerPanelVisual");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QTemporaryDir settings;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settings.path());
    MainWindow window(true);
    window.resize(1280, 850);
    window.show();
    QTest::qWait(200);
    auto* dock = window.findChild<QDockWidget*>("layersDock");
    auto* transform = window.findChild<QDockWidget*>("transformDock");
    auto* header = window.findChild<QWidget*>("layersHeader");
    auto* searchField = window.findChild<QWidget*>("layerSearchField");
    auto* title = window.findChild<QWidget*>("layersTitle");
    auto* appearance = window.findChild<QWidget*>("layerAppearance");
    auto* percent = window.findChild<QLineEdit*>("layerOpacityPercent");
    auto* slider = window.findChild<QSlider*>("layerOpacity");
    auto* opacityLabel = window.findChild<QLabel*>("layerOpacityLabel");
    auto* footer = window.findChild<QWidget*>("layerFooter");
    auto* resizeEdge = window.findChild<QWidget*>("layerResizeEdge");
    auto* tree = window.findChild<QTreeWidget*>("layersTree");
    auto* blend = window.findChild<QComboBox*>("layerBlend");
    auto* searchButton = window.findChild<QToolButton*>("layerSearchButton");
    if (!dock || !transform || !header || !searchField || !title || !appearance || !percent || !slider || !opacityLabel || !footer ||
        !tree || !blend || !searchButton || !resizeEdge) return 2;
    auto check = [](bool condition, const char* name) {
        if (!condition) std::cerr << "FAIL " << name << '\n';
        return condition;
    };
    if (argc == 2 && QString::fromLocal8Bit(argv[1]) == "--transform-background") {
        const auto sample = [](QWidget* widget) {
            const auto image = widget->grab().toImage();
            const auto scale = image.devicePixelRatio();
            return image.pixelColor(int(5 * scale), int(5 * scale));
        };
        const auto titleColor = sample(transform->titleBarWidget());
        const auto contentColor = sample(transform->widget());
        const bool matches = check(titleColor == QColor(t::background) && contentColor == QColor(t::background),
                                   "Transform title and content match Layers background");
        if (!matches) std::cerr << "title=" << titleColor.name().toStdString()
                                << " content=" << contentColor.name().toStdString() << '\n';
        return matches ? 0 : 1;
    }
    if (argc == 2 && QString::fromLocal8Bit(argv[1]) == "--accent-colors") {
        window.addFeasibilityDocument();
        QTest::qWait(100);
        const auto sliderImage = slider->grab().toImage();
        const auto sliderBlue = sliderImage.pixelColor(sliderImage.width() / 2, sliderImage.height() / 2);
        const auto thumbnailRect = tree->visualRect(tree->model()->index(0, 1));
        const auto thumbnailImage = tree->viewport()->grab().toImage();
        const auto scale = thumbnailImage.devicePixelRatio();
        bool selectedBlue = false;
        for (int y = int(thumbnailRect.top() * scale); y < int((thumbnailRect.bottom() + 1) * scale); ++y)
            for (int x = int(thumbnailRect.left() * scale); x < int((thumbnailRect.right() + 1) * scale); ++x)
                selectedBlue |= thumbnailImage.pixelColor(x, y) == QColor("#007aff");
        const bool sliderMatches = check(sliderBlue == QColor("#067dff"), "Mac slider accent");
        const bool thumbnailMatches = check(selectedBlue, "Mac selected thumbnail accent");
        if (!sliderMatches || !thumbnailMatches) {
            std::cerr << "slider=" << sliderBlue.name().toStdString() << " thumbnail=" << selectedBlue << '\n';
            return 1;
        }
        return 0;
    }
    if (argc == 2 && QString::fromLocal8Bit(argv[1]) == "--focused-selection") {
        window.addFeasibilityDocument();
        QTest::qWait(100);
        const auto nameRect = tree->visualRect(tree->model()->index(0, 0));
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, nameRect.center());
        QCoreApplication::processEvents();
        const auto sample = [&] {
            const auto image = tree->viewport()->grab().toImage();
            const auto scale = image.devicePixelRatio();
            return image.pixelColor(int((nameRect.right() - 4) * scale), int(nameRect.center().y() * scale));
        };
        const auto focused = sample();
        const auto focusedImage = tree->viewport()->grab().toImage();
        const auto scale = focusedImage.devicePixelRatio();
        const auto rowTop = tree->visualItemRect(tree->topLevelItem(0)).top() + 3;
        bool fullRowBlue = true;
        for (const int x : {3, tree->header()->sectionViewportPosition(0) + 4,
                            tree->viewport()->width() / 2, tree->viewport()->width() - 8})
            fullRowBlue &= focusedImage.pixelColor(int(x * scale), int(rowTop * scale)) == QColor(t::focusedRow);
        const auto thumbnailRect = tree->visualRect(tree->model()->index(0, 1));
        bool thumbnailBorderBlue = false;
        for (int y = int(thumbnailRect.top() * scale); y < int((thumbnailRect.bottom() + 1) * scale); ++y)
            for (int x = int(thumbnailRect.left() * scale); x < int((thumbnailRect.right() + 1) * scale); ++x)
                thumbnailBorderBlue |= focusedImage.pixelColor(x, y) == QColor(t::selectedThumbnail);
        QWidget* canvas = nullptr;
        for (auto* widget : window.findChildren<QWidget*>())
            if (widget->accessibleName() == "Image canvas") { canvas = widget; break; }
        if (!canvas) return 2;
        canvas->setFocus(Qt::MouseFocusReason);
        QCoreApplication::processEvents();
        const auto unfocused = sample();
        const bool focusedMatches = check(focused == QColor("#0059d1") && fullRowBlue && thumbnailBorderBlue,
                                          "focused Mac row and thumbnail blue");
        const bool unfocusedMatches = check(unfocused == QColor(t::selectedRow) && tree->topLevelItem(0)->isSelected(),
                                            "unfocused Mac row stays selected and gray");
        if (!focusedMatches || !unfocusedMatches) {
            std::cerr << "focused=" << focused.name().toStdString()
                      << " fullRowBlue=" << fullRowBlue << " thumbnailBorderBlue=" << thumbnailBorderBlue
                      << " unfocused=" << unfocused.name().toStdString() << '\n';
            return 1;
        }
        return 0;
    }
    if (argc == 2 && QString::fromLocal8Bit(argv[1]) == "--blend-hover") {
        window.addFeasibilityDocument();
        blend->showPopup();
        QTest::qWait(100);
        auto* view = blend->view();
        const auto row = view->visualRect(view->model()->index(blend->currentIndex(), 0));
        const QPoint sample(row.right() - 12, row.center().y());
        const auto background = [&] { return view->viewport()->grab().toImage().pixelColor(sample); };
        const auto before = background();
        QTest::mouseMove(view->viewport(), row.center());
        QCoreApplication::processEvents();
        const auto hovered = background();
        blend->hidePopup();
        if (!check(before == QColor(t::accent), "selected Blend background") ||
            !check(hovered == QColor(t::accent), "hovered selected Blend background")) {
            std::cerr << "before=" << before.name().toStdString()
                      << " hovered=" << hovered.name().toStdString() << '\n';
            return 1;
        }
        return 0;
    }
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == "--capture") {
        const QString directory = QString::fromLocal8Bit(argv[2]);
        if (!QDir().mkpath(directory)) return 2;
        auto save = [&](const QString& name) {
            QCoreApplication::processEvents();
            QTest::qWait(70);
            const auto image = dock->grab();
            if (!image.save(directory + "/" + name + ".png")) throw std::runtime_error("Cannot save panel capture");
        };
        try {
            save("no_document");
            if (!transform->grab().save(directory + "/transform.png"))
                throw std::runtime_error("Cannot save Transform capture");
            window.addFeasibilityDocument();
            QTest::qWait(180);
            save("populated");
            const auto firstName = tree->visualRect(tree->model()->index(0, 0));
            QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, firstName.center());
            save("selected_focused");
            for (auto* widget : window.findChildren<QWidget*>())
                if (widget->accessibleName() == "Image canvas") { widget->setFocus(Qt::MouseFocusReason); break; }
            save("selected_unfocused");
            searchButton->click();
            save("search_active");
            searchButton->click();
            blend->showPopup();
            QTest::qWait(100);
            save("blend_open");
            if (!blend->view()->window()->grab().save(directory + "/blend_popup.png"))
                throw std::runtime_error("Cannot save blend popup");
            const auto selected = blend->view()->visualRect(blend->view()->model()->index(blend->currentIndex(), 0));
            QTest::mouseMove(blend->view()->viewport(), selected.center());
            QTest::qWait(70);
            if (!blend->view()->window()->grab().save(directory + "/blend_popup_hover.png"))
                throw std::runtime_error("Cannot save hovered blend popup");
            blend->hidePopup();
            auto* effectsButton = window.findChild<QToolButton*>("layerEffectsButton");
            if (effectsButton && effectsButton->menu()) {
                effectsButton->click();
                QTest::qWait(80);
                if (!effectsButton->menu()->isVisible() ||
                    !effectsButton->menu()->grab().save(directory + "/effects_popup.png"))
                    throw std::runtime_error("Cannot save effects popup");
                effectsButton->menu()->hide();
            }
            if (tree->topLevelItemCount() >= 2) {
                tree->clearSelection();
                tree->topLevelItem(0)->setSelected(true);
                tree->topLevelItem(1)->setSelected(true);
                QTest::qWait(100);
            }
            save("multiple_selected");
            if (auto* group = window.findChild<QToolButton*>("layerGroupButton")) group->click();
            QTest::qWait(130);
            save("folder_selected");
            for (const auto* name : {"layerAddButton", "layerGroupButton", "layerAddMaskButton",
                                     "layerAdjustmentButton", "layerFilterLayerButton", "layerEffectsButton",
                                     "layerDeleteButton"})
                if (auto* button = window.findChild<QToolButton*>(name)) button->setEnabled(false);
            save("footer_disabled");
            window.resizeDocks({dock}, {t::minWidth}, Qt::Horizontal);
            QTest::qWait(150);
            save("width_261");
            const auto dockTopLeft = dock->mapTo(&window, QPoint{});
            const auto edgeImage = window.grab(QRect(dockTopLeft.x() - t::resizeHitWidth, dockTopLeft.y(),
                                                     dock->width() + t::resizeHitWidth, dock->height())).toImage();
            if (!edgeImage.save(directory + "/resize_edge.png"))
                throw std::runtime_error("Cannot save resize edge");
            const int listTop = tree->mapTo(dock, QPoint{}).y();
            for (int y = listTop; y < qMin(listTop + 160, edgeImage.height()); ++y)
                for (int x = 0; x < t::resizeHitWidth; ++x) {
                    const auto color = edgeImage.pixelColor(x, y);
                    if (color.red() > 180 && color.green() > 180 && color.blue() > 180)
                        throw std::runtime_error("Layer rows leaked left of the narrow dock");
                }
            window.resizeDocks({dock}, {t::maxWidth}, Qt::Horizontal);
            QTest::qWait(150);
            save("width_372");
            std::cout << "CAPTURED " << directory.toStdString() << '\n';
            return 0;
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            return 2;
        }
    }
    bool okay = true;
    okay &= check(header->height() == t::headerHeight, "header height");
    okay &= check(searchField->height() == t::searchHeight, "search field height");
    const auto family = title->font().family();
    okay &= check((family == "Segoe UI Variable" || family == "Segoe UI") &&
                  title->font().pixelSize() == t::titleFont, "panel font");
    okay &= check(dock->minimumWidth() == t::minWidth && dock->maximumWidth() == t::maxWidth, "panel bounds");
    okay &= check(dock->width() == t::minWidth, "default panel width");
    okay &= check(resizeEdge->width() == t::resizeHitWidth &&
                  resizeEdge->cursor().shape() == Qt::SplitHCursor, "resize edge");
    auto* appearanceLayout = qobject_cast<QVBoxLayout*>(appearance->layout());
    okay &= check(appearanceLayout && appearanceLayout->contentsMargins() ==
                  QMargins(t::appearancePadding, t::appearancePadding,
                           t::appearancePadding, t::appearancePadding), "appearance margins");
    okay &= check(percent->width() == t::percentWidth, "percent width");
    okay &= check(slider->height() == t::sliderHeight, "slider height");
    okay &= check(slider->mapTo(appearance, QPoint(0, slider->height() - 1)).y() ==
                  percent->mapTo(appearance, QPoint(0, percent->height() - 1)).y() &&
                  slider->mapTo(appearance, QPoint(0, slider->height() - 1)).y() ==
                  opacityLabel->mapTo(appearance, QPoint(0, opacityLabel->height() - 1)).y(),
                  "opacity slider vertical alignment");
    QStyleOptionSlider option;
    option.initFrom(slider);
    option.orientation = Qt::Horizontal;
    option.minimum = slider->minimum();
    option.maximum = slider->maximum();
    option.sliderPosition = slider->sliderPosition();
    const auto groove = slider->style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderGroove, slider);
    const auto handle = slider->style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, slider);
    okay &= check(groove.height() == t::sliderTrack, "slider track thickness");
    okay &= check(handle.width() == t::sliderKnobWidth && handle.height() == t::sliderKnobHeight,
                  "slider knob dimensions");
    auto* add = window.findChild<QToolButton*>("layerAddButton");
    auto* group = window.findChild<QToolButton*>("layerGroupButton");
    auto* mask = window.findChild<QToolButton*>("layerAddMaskButton");
    auto* adjustment = window.findChild<QToolButton*>("layerAdjustmentButton");
    auto* filter = window.findChild<QToolButton*>("layerFilterLayerButton");
    auto* effects = window.findChild<QToolButton*>("layerEffectsButton");
    okay &= check(add && group && mask && adjustment && filter && effects, "footer controls");
    if (add && group && mask && adjustment && filter && effects) {
        okay &= check(group->x() - add->x() == t::footerIconSize + 2 * t::footerIconHorizontal,
                      "plain footer gap");
        okay &= check(adjustment->x() - mask->x() == t::footerIconSize + 2 * t::footerIconHorizontal,
                      "mask to menu gap");
        okay &= check(filter->x() - adjustment->x() ==
                      t::footerIconSize + t::footerChevronGap + t::footerChevronSize +
                      2 * t::footerMenuHorizontal, "menu footer gap");
        okay &= check(adjustment->menu() && adjustment->menu()->actions().size() == 9 &&
                      filter->menu() && !filter->menu()->actions().isEmpty() &&
                      effects->menu() && effects->menu()->actions().size() == 6, "footer menus");
    }
    for (const auto* name : {"layerAddButton", "layerGroupButton", "layerAddMaskButton",
                             "layerAdjustmentButton", "layerFilterLayerButton", "layerEffectsButton",
                             "layerDeleteButton"}) {
        auto* button = window.findChild<QToolButton*>(name);
        okay &= check(button && !button->toolTip().isEmpty(), "footer tooltip");
    }
    searchButton->click();
    auto* edit = window.findChild<QLineEdit*>("layerSearch");
    okay &= check(searchField->isVisible() && edit && edit->hasFocus(), "search visible and focused");
    okay &= check(edit && edit->width() >= edit->fontMetrics().horizontalAdvance("Search Layers"),
                  "search placeholder fits");
    auto* typeFilter = window.findChild<QToolButton*>("layerTypeFilterButton");
    if (typeFilter) typeFilter->click();
    okay &= check(typeFilter && !typeFilter->menu() && !typeFilter->isCheckable() &&
                  !typeFilter->property("active").toBool(), "inert type filter");
    window.addFeasibilityDocument();
    QTest::qWait(100);
    window.resizeDocks({dock}, {t::minWidth}, Qt::Horizontal);
    QCoreApplication::processEvents();
    const auto dockBounds = QRect(dock->mapTo(&window, QPoint{}), dock->size());
    const auto treeBounds = QRect(tree->mapTo(&window, QPoint{}), tree->size());
    const auto viewportBounds = QRect(tree->viewport()->mapTo(&window, QPoint{}), tree->viewport()->size());
    okay &= check(treeBounds.left() >= dockBounds.left() && treeBounds.right() <= dockBounds.right() &&
                  viewportBounds.left() >= dockBounds.left() && viewportBounds.right() <= dockBounds.right(),
                  "narrow panel contains layer list");
    if (tree->topLevelItemCount() >= 2) {
        tree->clearSelection();
        tree->topLevelItem(0)->setSelected(true);
        tree->topLevelItem(1)->setSelected(true);
        QTest::qWait(100);
        auto* deleteButton = window.findChild<QToolButton*>("layerDeleteButton");
        okay &= check(deleteButton && deleteButton->toolTip() == "Delete selected layers",
                      "multiple selection delete tooltip");
    }
    std::cout << "font=" << family.toStdString() << " header=" << header->height() << " search=" << searchField->height()
              << " groove=" << groove.height() << " handle=" << handle.width() << 'x' << handle.height()
              << " width=" << dock->width() << '\n';
    return okay ? 0 : 1;
}

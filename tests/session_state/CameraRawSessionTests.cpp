#define main rememberedSettingsBaseMain
#include "RememberedSettingsTests.cpp"
#undef main
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QImage>
#include <QLabel>
#include <QSlider>
#include <string>
#include <cmath>
#include <QMenu>
#include <QMouseEvent>
#include <QTest>
#include <QToolButton>
namespace {
void rawModal(Fixture& f, const std::function<void(QDialog&)>& configure, bool ok) {
    std::exception_ptr failure;
    bool visited = false, finished = false, submitted = false;
    QPointer<QDialog> panel;
    QMetaObject::Connection completion;
    QEventLoop wait;
    QTimer poll;
    poll.setInterval(5);
    QElapsedTimer elapsed;
    elapsed.start();
    QObject::connect(&poll, &QTimer::timeout, &f.window, [&] {
        try {
            if (elapsed.elapsed() > 15000) throw std::runtime_error("Camera Raw panel did not finish within 15 seconds");
            QDialog* dialog = panel ? panel.data() : nullptr;
            if (!dialog) for (auto* object : f.window.findChildren<QObject*>()) if (auto* session = dynamic_cast<ui::EditPanelSession*>(object))
                if (session->canvas() == f.window.canvas() && session->panel() && session->panel()->isVisible()) dialog = session->panel();
            if (!dialog) return;
            if (!visited) {
                visited = true;
                panel = dialog;
                completion = QObject::connect(dialog, &QDialog::finished, &wait, [&] { finished = true; wait.quit(); });
                configure(*dialog);
                if (!ok) { dialog->reject(); return; }
            }
            auto* box = dialog->findChild<QDialogButtonBox*>();
            auto* button = box ? box->button(QDialogButtonBox::Ok) : nullptr;
            if (!submitted && button && button->isEnabled()) { submitted = true; button->click(); }
        } catch (...) {
            failure = std::current_exception();
            poll.stop();
            if (panel) panel->reject();
            wait.quit();
        }
    });
    poll.start();
    f.trigger("filter.camera_raw");
    if (!finished && !failure) wait.exec();
    poll.stop();
    QObject::disconnect(completion);
    if (failure) std::rethrow_exception(failure);
    require(visited && finished, "Camera Raw panel opened and finished");
}
void identity_ok() {
    Fixture f;
    const auto before = f.a->document;
    rawModal(f, [](QDialog&) {}, true);
    require(f.a->document == before && f.a->history.undoCount() == 0, "OK on an identity grade adds no undo");
    require(f.a->toolState.filterSettings.cameraRaw.isIdentity(), "identity OK remembers the identity grade");
}
void commit_ok() {
    Fixture f;
    rawModal(f, [](QDialog& dialog) { field<QDoubleSpinBox>(dialog, "Exposure")->setValue(1); }, true);
    require(f.a->history.undoCount() == 1 && f.a->history.undoName() == "Camera Raw Filter", "OK adds one Camera Raw Filter undo");
    const auto pixel = f.a->document->layers.front().raster->pixel(0, 0);
    require(pixel.r > 165 && pixel.r < 190 && pixel.r == pixel.g && pixel.a == 255, "OK bakes about one stop");
    rawModal(f, [](QDialog& dialog) { require(field<QDoubleSpinBox>(dialog, "Exposure")->value() == 1, "reopen restores the committed exposure"); }, false);
}
void cancel_keeps() {
    Fixture f;
    const auto before = f.a->document;
    rawModal(f, [](QDialog& dialog) { field<QDoubleSpinBox>(dialog, "Exposure")->setValue(1); }, false);
    require(f.a->document == before && f.a->history.undoCount() == 0 && f.a->toolState.filterSettings.cameraRaw.exposure == 0, "Cancel does not remember or change pixels");
}
void hidden_group() {
    Fixture f;
    const auto before = f.a->document;
    rawModal(f, [](QDialog& dialog) {
        field<QDoubleSpinBox>(dialog, "Exposure")->setValue(1);
        field<QToolButton>(dialog, "eye.light")->setChecked(false);
    }, true);
    require(f.a->document == before && f.a->history.undoCount() == 0 && f.a->toolState.filterSettings.cameraRaw.exposure == 0, "OK with the Light eye off and exposure 1 adds no undo");
    rawModal(f, [](QDialog& dialog) {
        field<QDoubleSpinBox>(dialog, "Exposure")->setValue(1);
        field<QDoubleSpinBox>(dialog, "Temperature")->setValue(40);
        field<QToolButton>(dialog, "eye.light")->setChecked(false);
    }, true);
    require(f.a->history.undoCount() == 1 && f.a->toolState.filterSettings.cameraRaw.exposure == 0 && f.a->toolState.filterSettings.cameraRaw.temperature == 40, "a hidden Light group is omitted from the remembered grade");
    const auto pixel = f.a->document->layers.front().raster->pixel(0, 0);
    require(pixel.r != pixel.b, "the committed grade keeps temperature");
    rawModal(f, [](QDialog& dialog) {
        require(field<QDoubleSpinBox>(dialog, "Exposure")->value() == 0 && field<QDoubleSpinBox>(dialog, "Temperature")->value() == 40, "reopen shows the rendered grade");
    }, false);
}
void dock_and_menu() {
    Fixture f;
    QStringList labels;
    for (auto* menu : f.window.findChildren<QMenu*>()) if (menu->title() == "&Filters") for (auto* action : menu->actions()) labels << action->text();
    require(labels.indexOf("Lens Correction…") + 1 == labels.indexOf("Camera Raw Filter…"), "Camera Raw Filter follows Lens Correction");
    f.window.show();
    f.window.setGeometry(80, 60, 1000, 700);
    QApplication::processEvents();
    QPointer<QDialog> panel;
    QEventLoop wait;
    QTimer::singleShot(0, &f.window, [&] { f.trigger("filter.camera_raw"); });
    QTimer poll;
    poll.setInterval(10);
    QObject::connect(&poll, &QTimer::timeout, &f.window, [&] {
        for (auto* object : f.window.findChildren<QObject*>()) if (auto* session = dynamic_cast<ui::EditPanelSession*>(object))
            if (session->panel() && session->panel()->isVisible()) { panel = session->panel(); wait.quit(); }
    });
    poll.start();
    QTimer::singleShot(15000, &wait, &QEventLoop::quit);
    wait.exec();
    poll.stop();
    require(panel, "docked panel opened");
    auto matches = [&] {
        const auto frame = f.window.frameGeometry();
        const auto box = panel->frameGeometry();
        if (panel->width() == 440 && box.height() == frame.height() && box.right() == frame.right() && box.top() == frame.top()) return true;
        throw std::runtime_error("dock panel " + std::to_string(box.x()) + "," + std::to_string(box.y()) + " " + std::to_string(box.width()) + "x" + std::to_string(box.height()) + " frame " + std::to_string(frame.x()) + "," + std::to_string(frame.y()) + " " + std::to_string(frame.width()) + "x" + std::to_string(frame.height()));
    };
    QApplication::processEvents();
    require(matches(), "the panel docks to the window's right edge and height");
    f.window.setGeometry(140, 100, 1100, 760);
    QApplication::processEvents();
    require(matches(), "the dock follows move and resize");
    panel->reject();
    QApplication::processEvents();
}
QPointF brightCentroid(const QImage& image) {
    double weight = 0, sumX = 0, sumY = 0;
    for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) {
        const QColor color = image.pixelColor(x, y);
        if (color.alpha() < 170 || color.red() < 210 || color.green() < 210 || color.blue() < 210) continue;
        weight += 1;
        sumX += x + 0.5;
        sumY += y + 0.5;
    }
    if (weight < 20) throw std::runtime_error("control mark is not visible");
    return {sumX / weight, sumY / weight};
}
void glass_controls() {
    Fixture f;
    rawModal(f, [](QDialog& dialog) {
        QApplication::sendPostedEvents();
        auto* exposure = field<QDoubleSpinBox>(dialog, "Exposure");
        require(exposure->text() == exposure->locale().toString(0.0, 'f', 2), "exposure value is shown");
        require(exposure->width() >= exposure->fontMetrics().horizontalAdvance(exposure->text()) + 8, "exposure value fits in its field");
        auto* slider = field<QSlider>(dialog, "Exposure slider");
        QImage sliderImage(slider->size(), QImage::Format_ARGB32_Premultiplied);
        sliderImage.fill(Qt::transparent);
        slider->render(&sliderImage);
        const QPointF knob = brightCentroid(sliderImage);
        require(std::abs(knob.y() - sliderImage.height() / 2.0) <= 1.25, "slider knob is vertically centered");
        require(std::abs(knob.x() - sliderImage.width() / 2.0) <= 2.0, "slider knob starts centered on the track");
        bool luminanceFits = false;
        for (auto* label : dialog.findChildren<QLabel*>()) {
            if (label->text() != "Luminance" || !label->isVisible()) continue;
            luminanceFits = label->width() >= label->fontMetrics().horizontalAdvance(label->text());
            break;
        }
        require(luminanceFits, "color grading luminance text is not truncated");
        auto* wheel = dialog.findChild<QWidget*>("cameraRawGradeShadows");
        require(wheel && wheel->isVisible(), "shadows grade wheel is visible");
        const QPointF edge(wheel->width() - 12, wheel->height() / 2.0);
        const QPointF global = wheel->mapToGlobal(edge);
        QMouseEvent press(QEvent::MouseButtonPress, edge, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(wheel, &press);
        QImage wheelImage(wheel->size(), QImage::Format_ARGB32_Premultiplied);
        wheelImage.fill(Qt::transparent);
        wheel->render(&wheelImage);
        const QPointF point = brightCentroid(wheelImage);
        require(point.x() > wheel->width() * 0.62, "color grading point follows the pointer");
        QMouseEvent release(QEvent::MouseButtonRelease, edge, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(wheel, &release);
    }, false);
}
}
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    const std::map<std::string, void (*)()> cases{{"identity_ok", identity_ok}, {"commit_ok", commit_ok}, {"cancel_keeps", cancel_keeps}, {"hidden_group", hidden_group}, {"dock_and_menu", dock_and_menu}, {"glass_controls", glass_controls}};
    try {
        require(argc == 2 && cases.contains(argv[1]), "provide named case");
        cases.at(argv[1])();
        std::cout << "PASS " << argv[1] << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}

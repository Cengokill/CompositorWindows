#define main rememberedSettingsBaseMain
#include "RememberedSettingsTests.cpp"
#undef main
#include "filters/CameraRaw.h"
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QImage>
#include <QLabel>
#include <QSlider>
#include <string>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
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
void commit_during_refinement() {
    Fixture f(document(2048, 1536));
    std::exception_ptr failure;
    bool opened = false, finished = false, submitted = false, sawRefinement = false;
    QPointer<QDialog> panel;
    QMetaObject::Connection completion;
    QEventLoop wait;
    QTimer poll;
    poll.setInterval(5);
    QElapsedTimer elapsed;
    elapsed.start();
    QObject::connect(&poll, &QTimer::timeout, &f.window, [&] {
        try {
            if (elapsed.elapsed() > 30000) throw std::runtime_error("Camera Raw refinement did not commit within 30 seconds");
            QDialog* dialog = panel ? panel.data() : nullptr;
            if (!dialog) for (auto* object : f.window.findChildren<QObject*>()) if (auto* session = dynamic_cast<ui::EditPanelSession*>(object))
                if (session->canvas() == f.window.canvas() && session->panel() && session->panel()->isVisible()) dialog = session->panel();
            if (!dialog) return;
            if (!opened) {
                opened = true;
                panel = dialog;
                completion = QObject::connect(dialog, &QDialog::finished, &wait, [&] { finished = true; wait.quit(); });
                field<QDoubleSpinBox>(*dialog, "Exposure")->setValue(1);
            }
            auto* box = dialog->findChild<QDialogButtonBox*>();
            auto* button = box ? box->button(QDialogButtonBox::Ok) : nullptr;
            const auto labels = dialog->findChildren<QLabel*>();
            const bool updating = std::any_of(labels.cbegin(), labels.cend(), [](const QLabel* label) { return label->text() == "Updating preview…"; });
            if (!submitted && button && button->isEnabled() && updating) {
                sawRefinement = true;
                submitted = true;
                button->click();
            }
        } catch (...) {
            failure = std::current_exception();
            poll.stop();
            wait.quit();
        }
    });
    poll.start();
    f.trigger("filter.camera_raw");
    if (!finished && !failure) wait.exec();
    poll.stop();
    QObject::disconnect(completion);
    if (failure) std::rethrow_exception(failure);
    require(opened && sawRefinement && submitted && finished, "OK was submitted while the refinement preview was running");
    require(f.a->history.undoCount() == 1 && f.a->history.undoName() == "Camera Raw Filter", "OK during refinement commits one Camera Raw Filter undo");
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
Document checkerboard() {
    auto picture = document(96, 64);
    std::vector<Pixel> pixels(std::size_t(picture.width) * picture.height);
    for (int y = 0; y < picture.height; ++y) for (int x = 0; x < picture.width; ++x)
        pixels[std::size_t(y) * picture.width + x] = ((x / 8) + (y / 8)) % 2 ? Pixel{240, 240, 240, 255} : Pixel{20, 20, 20, 255};
    picture.layers[0].raster = Raster::fromRgba(picture.width, picture.height, reinterpret_cast<const uint8_t*>(pixels.data()), std::size_t(picture.width) * 4);
    return picture;
}
void saveShot(const QImage& image, const char* name) {
    const char* dir = std::getenv("COMPOSITOR_GUIDE_SHOTS");
    if (!dir || !*dir) return;
    QDir().mkpath(QString::fromLocal8Bit(dir));
    require(image.save(QString::fromLocal8Bit(dir) + QLatin1Char('/') + QLatin1String(name)), "guide screenshot save failed");
}
bool guideInk(QColor color) { return color.red() >= 250 && color.green() >= 250 && color.blue() >= 250; }
QColor sampleDocument(const QImage& image, NativeCanvas& canvas, Point document) {
    const auto view = canvas.viewMapping().toView(document);
    const double scale = canvas.devicePixelRatioF();
    const int x = std::clamp(int(std::lround(view.x * scale)), 0, image.width() - 1);
    const int y = std::clamp(int(std::lround(view.y * scale)), 0, image.height() - 1);
    return image.pixelColor(x, y);
}
QImage captureCanvas(NativeCanvas& canvas) {
    QApplication::processEvents();
    auto image = canvas.captureRendered();
    require(!image.isNull() && canvas.deviceReady() && canvas.deviceError().isEmpty(), "canvas capture unavailable");
    return image;
}
void sendPointer(NativeCanvas& canvas, QEvent::Type type, Point view, Qt::MouseButton button, Qt::MouseButtons buttons) {
    const QPointF local(view.x, view.y);
    require(local.x() >= 1 && local.y() >= 1 && local.x() < canvas.width() - 1 && local.y() < canvas.height() - 1, "guide pointer is outside the canvas");
    QMouseEvent event(type, local, canvas.mapToGlobal(local), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &event);
}
Point viewPoint(NativeCanvas& canvas, Point document) {
    const auto view = canvas.viewMapping().toView(document);
    return {view.x, view.y};
}
void armGuides(QDialog& dialog) {
    auto* geometry = field<QToolButton>(dialog, "Geometry");
    if (!geometry->isChecked()) geometry->click();
    auto* draw = field<QToolButton>(dialog, "Draw Guides");
    require(!draw->isChecked(), "Draw Guides was already armed");
    draw->click();
    require(draw->isChecked(), "Draw Guides did not arm");
}
filters::CameraRawGeometryGuide guideFromDocument(const Transform& transform, Point start, Point end) {
    auto axis = [&](Point point) {
        const auto unit = transform.toUnit(point);
        return std::pair{std::clamp(unit.x, 0., 1.), std::clamp(1. - unit.y, 0., 1.)};
    };
    const auto a = axis(start), b = axis(end);
    return {a.first, a.second, b.first, b.second};
}
bool sameRaster(const Raster& left, const Raster& right) {
    if (left.width != right.width || left.height != right.height) return false;
    for (int y = 0; y < left.height; ++y) for (int x = 0; x < left.width; ++x) if (left.pixel(x, y) != right.pixel(x, y)) return false;
    return true;
}
bool differentRaster(const Raster& left, const Raster& right) { return !sameRaster(left, right); }
QDialog* openCameraRaw(Fixture& f) {
    QPointer<QDialog> panel;
    QEventLoop wait;
    QTimer poll;
    poll.setInterval(5);
    QElapsedTimer elapsed;
    elapsed.start();
    QObject::connect(&poll, &QTimer::timeout, &f.window, [&] {
        if (elapsed.elapsed() > 15000) { wait.quit(); return; }
        for (auto* object : f.window.findChildren<QObject*>()) if (auto* session = dynamic_cast<ui::EditPanelSession*>(object))
            if (session->canvas() == f.window.canvas() && session->panel() && session->panel()->isVisible()) { panel = session->panel(); wait.quit(); }
    });
    poll.start();
    f.trigger("filter.camera_raw");
    wait.exec();
    poll.stop();
    require(panel, "Camera Raw panel opened");
    return panel.data();
}
void guided_lines() {
    Fixture f(checkerboard());
    const auto untouched = f.a->document;
    const Point start{30, 20}, end{66, 44};
    const Point flippedStart{start.x, end.y};
    f.window.show();
    f.window.setGeometry(40, 40, 1200, 820);
    QApplication::processEvents();
    auto* canvas = f.window.canvas();
    require(canvas && canvas->width() > 200 && canvas->height() > 200, "canvas has a layout size");
    canvas->fit();
    QApplication::processEvents();
    auto* dialog = openCameraRaw(f);
    armGuides(*dialog);
    sendPointer(*canvas, QEvent::MouseButtonPress, viewPoint(*canvas, start), Qt::LeftButton, Qt::LeftButton);
    sendPointer(*canvas, QEvent::MouseMove, viewPoint(*canvas, end), Qt::NoButton, Qt::LeftButton);
    const auto during = captureCanvas(*canvas);
    saveShot(during, "during.png");
    require(guideInk(sampleDocument(during, *canvas, end)), "in-progress guide is not visible at the pointer");
    require(!guideInk(sampleDocument(during, *canvas, flippedStart)), "in-progress guide uses the stored Y axis instead of the pointer");
    sendPointer(*canvas, QEvent::MouseButtonRelease, viewPoint(*canvas, end), Qt::LeftButton, Qt::NoButton);
    const auto accepted = captureCanvas(*canvas);
    saveShot(accepted, "accepted.png");
    require(guideInk(sampleDocument(accepted, *canvas, start)) && guideInk(sampleDocument(accepted, *canvas, end)), "accepted guide is not visible");
    const auto anchor = viewPoint(*canvas, Point{(start.x + end.x) / 2, (start.y + end.y) / 2});
    canvas->zoomAt(canvas->zoom * 2, QPointF(anchor.x, anchor.y));
    canvas->panBy({28, -18});
    QApplication::processEvents();
    const auto zoomed = captureCanvas(*canvas);
    saveShot(zoomed, "zoomed.png");
    require(guideInk(sampleDocument(zoomed, *canvas, start)) && guideInk(sampleDocument(zoomed, *canvas, end)), "guide left the image after zoom and pan");
    require(!guideInk(sampleDocument(zoomed, *canvas, flippedStart)), "zoomed guide follows the flipped axis");
    field<QPushButton>(*dialog, "Clear Guides")->click();
    QApplication::processEvents();
    const auto cleared = captureCanvas(*canvas);
    saveShot(cleared, "cleared.png");
    require(!guideInk(sampleDocument(cleared, *canvas, start)) && !guideInk(sampleDocument(cleared, *canvas, end)), "Clear Guides left the guide visible");
    armGuides(*dialog);
    sendPointer(*canvas, QEvent::MouseButtonPress, viewPoint(*canvas, start), Qt::LeftButton, Qt::LeftButton);
    sendPointer(*canvas, QEvent::MouseMove, viewPoint(*canvas, end), Qt::NoButton, Qt::LeftButton);
    sendPointer(*canvas, QEvent::MouseButtonRelease, viewPoint(*canvas, end), Qt::LeftButton, Qt::NoButton);
    require(guideInk(sampleDocument(captureCanvas(*canvas), *canvas, end)), "second guide is not visible");
    dialog->reject();
    QApplication::processEvents();
    const auto cancelled = captureCanvas(*canvas);
    saveShot(cancelled, "cancelled.png");
    require(!guideInk(sampleDocument(cancelled, *canvas, start)) && !guideInk(sampleDocument(cancelled, *canvas, end)), "Cancel left the guide visible");
    require(f.a->document == untouched && f.a->history.undoCount() == 0, "Cancel changed the document");
    const auto source = f.a->document->layers.front().raster;
    const auto transform = f.a->document->layers.front().transform;
    filters::CameraRawGeometryGuide viewed{};
    rawModal(f, [&](QDialog& panel) {
        canvas->fit();
        const auto center = viewPoint(*canvas, Point{(start.x + end.x) / 2, (start.y + end.y) / 2});
        canvas->zoomAt(canvas->zoom * 2, QPointF(center.x, center.y));
        canvas->panBy({36, -12});
        QApplication::processEvents();
        const auto viewStart = viewPoint(*canvas, start), viewEnd = viewPoint(*canvas, end);
        viewed = guideFromDocument(transform, Point{viewStart.x, viewStart.y}, Point{viewEnd.x, viewEnd.y});
        armGuides(panel);
        sendPointer(*canvas, QEvent::MouseButtonPress, viewStart, Qt::LeftButton, Qt::LeftButton);
        sendPointer(*canvas, QEvent::MouseMove, viewEnd, Qt::NoButton, Qt::LeftButton);
        sendPointer(*canvas, QEvent::MouseButtonRelease, viewEnd, Qt::LeftButton, Qt::NoButton);
        const auto committedView = captureCanvas(*canvas);
        saveShot(committedView, "after.png");
        require(guideInk(sampleDocument(committedView, *canvas, start)), "guide is not visible while zoomed before OK");
    }, true);
    require(f.a->history.undoCount() == 1 && f.a->history.undoName() == "Camera Raw Filter", "OK did not commit the guided grade");
    const auto baked = f.a->document->layers.front().raster;
    const auto& remembered = f.a->toolState.filterSettings.cameraRaw;
    const auto expected = guideFromDocument(transform, start, end);
    require(remembered.geometry.upright == filters::CameraRawUpright::Guided && remembered.geometry.guides.size() == 1, "OK did not remember one guided line");
    const auto& guide = remembered.geometry.guides.front();
    require(std::abs(guide.startX - expected.startX) < 1e-4 && std::abs(guide.startY - expected.startY) < 1e-4 && std::abs(guide.endX - expected.endX) < 1e-4 && std::abs(guide.endY - expected.endY) < 1e-4, "remembered guide is not the layer-unit line");
    require(std::abs(guide.startX - viewed.startX) > 0.05 || std::abs(guide.startY - viewed.startY) > 0.05, "remembered guide follows view coordinates");
    auto painted = filters::renderCameraRaw(*source, remembered, {}, nullptr);
    require(painted.raster && baked && sameRaster(*painted.raster, *baked), "OK pixels differ from the guide correction");
    filters::CameraRawSettings unflipped = remembered;
    unflipped.geometry.guides[0] = {expected.startX, 1 - expected.startY, expected.endX, 1 - expected.endY};
    auto mirrored = filters::renderCameraRaw(*source, unflipped, {}, nullptr);
    require(mirrored.raster && differentRaster(*mirrored.raster, *baked), "Y-flipped guide coordinates produce the same correction");
    const auto closed = captureCanvas(*canvas);
    saveShot(closed, "after-ok.png");
    require(!guideInk(sampleDocument(closed, *canvas, start)) && !guideInk(sampleDocument(closed, *canvas, end)), "OK left the guide overlay visible");
}
}
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    const std::map<std::string, void (*)()> cases{{"identity_ok", identity_ok}, {"commit_ok", commit_ok}, {"commit_during_refinement", commit_during_refinement}, {"cancel_keeps", cancel_keeps}, {"hidden_group", hidden_group}, {"dock_and_menu", dock_and_menu}, {"glass_controls", glass_controls}, {"guided_lines", guided_lines}};
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

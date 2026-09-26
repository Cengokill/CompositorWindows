// Native presentation witness for TransformOverlay.drawShapeDraft. The source
// fills outside the document clip while keeping the canonical document intact.
#include "ui/MainWindow.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <cmath>
#include <filesystem>
#include <iostream>
using namespace compositor;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void pump(int milliseconds){QElapsedTimer timer;timer.start();while(timer.elapsed()<milliseconds){QApplication::processEvents();QThread::msleep(5);}}
QJsonArray color(QColor value){return {value.red(),value.green(),value.blue(),value.alpha()};}
QPoint devicePoint(const NativeCanvas& canvas,Point document){const auto view=canvas.viewMapping().toView(document);return {int(std::floor(view.x*canvas.devicePixelRatioF())),int(std::floor(view.y*canvas.devicePixelRatioF()))};}
QColor pixel(const QImage& image,QPoint point){check(image.rect().contains(point),"Witness sample lies outside framebuffer");return image.pixelColor(point);}
bool colorClose(QColor a,QColor b){return std::abs(a.red()-b.red())<=1&&std::abs(a.green()-b.green())<=1&&std::abs(a.blue()-b.blue())<=1&&std::abs(a.alpha()-b.alpha())<=1;}
void image(const QImage& value,const std::filesystem::path& path){check(value.save(QString::fromStdWString(path.wstring())),"PNG write failed");}
}
int main(int argc,char** argv){
    QApplication app(argc,argv);if(argc!=2){std::cerr<<"Usage: native_shape_witness <unique-evidence-directory>\n";return 2;}
    const auto directory=std::filesystem::path(argv[1]);std::filesystem::create_directories(directory);QJsonArray results;int failed=0;
    try{
        MainWindow window(true);Document document;document.id="native-shape";document.width=64;document.height=48;
        Layer layer;layer.id="base";layer.name="Base";layer.transform={0,0,64,48};layer.raster=Raster::filled(64,48,{90,140,190,255});document.layers={layer};
        auto& project=window.addProject(document);window.resize(1050,780);window.show();window.activateWindow();pump(200);
        auto& canvas=*project.canvas;canvas.setDisplayProfileOverride(directory/"deliberately-missing-profile.icc");canvas.zoomAt(4*canvas.devicePixelRatioF(),canvas.rect().center());canvas.showPixelGrid=false;
        for(auto* action:window.findChildren<QAction*>())if(action->text()=="Shape (U)"){action->trigger();break;}
        pump(100);auto* kind=window.findChild<QComboBox*>("shapeKind");check(kind,"Shape kind control missing");
        const auto before=canvas.captureRendered();check(!canvas.presentationConvertsColor()&&!canvas.presentationDiagnostic().isEmpty(),"Explicit sRGB fallback did not isolate presentation");
        image(before,directory/"canvas-before.png");
        for(int index=0;index<2;++index){
            kind->setCurrentIndex(index);const std::string name=index==0?"native_shape_rectangle_live":"native_shape_ellipse_live";
            const Point sample{-4,24};const auto at=devicePoint(canvas,sample);const auto baseline=pixel(before,at);
            canvas.pointerDown({-16,8},{});canvas.pointerMove({32,40},{});pump(40);
            const auto live=canvas.captureRendered();image(live,directory/(name+"-live.png"));
            const auto actual=pixel(live,at);const bool filled=colorClose(actual,QColor(0,0,0,255));
            const bool immutable=project.document==document&&project.history.undoCount()==0;
            canvas.pointerCancel();pump(30);const auto cancelled=canvas.captureRendered();image(cancelled,directory/(name+"-cancel.png"));
            const bool restored=colorClose(pixel(cancelled,at),baseline)&&project.document==document&&project.history.undoCount()==0;
            const bool pass=filled&&immutable&&restored;failed+=!pass;
            results.append(QJsonObject{{"name",QString::fromStdString(name)},{"pass",pass},{"outside_fill",filled},{"canonical_unchanged",immutable},{"cancel_restored",restored},{"sample_document",QJsonArray{sample.x,sample.y}},{"sample_device",QJsonArray{at.x(),at.y()}},{"actual_rgba",color(actual)},{"expected_rgba",QJsonArray{0,0,0,255}},{"before_rgba",color(baseline)}});
            std::cout<<(pass?"PASS ":"FAIL ")<<name<<" outside="<<filled<<" immutable="<<immutable<<" cancel="<<restored<<'\n';
        }
        image(window.grab().toImage(),directory/"window.png");QFile report(QString::fromStdWString((directory/"results.json").wstring()));check(report.open(QIODevice::WriteOnly),"Report write failed");
        report.write(QJsonDocument(QJsonObject{{"schema","NATIVE_SHAPE_OVERLAY_V1"},{"dpr",canvas.devicePixelRatioF()},{"tolerance",1},{"failed",failed},{"checks",results},{"presentation","explicit missing ICC -> sRGB fallback"},{"mac_differential",false}}).toJson());
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
    return failed?1:0;
}

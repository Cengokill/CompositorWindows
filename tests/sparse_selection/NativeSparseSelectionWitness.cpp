// Native representation/control witness. This does not calibrate CoreGraphics
// antialiasing or claim parity for the existing static marching-ants style.
#include "ui/MainWindow.h"
#include "editing/Selection.h"
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
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void pump(){QElapsedTimer timer;timer.start();while(timer.elapsed()<35){QApplication::processEvents();QThread::msleep(3);}}
void command(MainWindow& window,const char* id){for(auto* action:window.findChildren<QAction*>())if(action->property("commandId").toString()==id){require(action->isEnabled(),"Command disabled");action->trigger();pump();return;}throw std::runtime_error("Command absent");}
void save(const QImage& image,const std::filesystem::path& file){require(!image.isNull()&&image.save(QString::fromStdWString(file.wstring())),"Capture failed");}
QImage capture(NativeCanvas& canvas,const std::filesystem::path& file){pump();auto result=canvas.captureRendered();require(!canvas.presentationConvertsColor()&&!canvas.presentationDiagnostic().isEmpty(),"sRGB isolation absent");save(result,file);return result;}
int difference(QColor a,QColor b){return std::max({std::abs(a.red()-b.red()),std::abs(a.green()-b.green()),std::abs(a.blue()-b.blue()),std::abs(a.alpha()-b.alpha())});}
int contrast(const NativeCanvas& canvas,const QImage& image,Point point){const auto at=canvas.viewMapping().toView(point);const int x=int(std::floor(at.x*canvas.devicePixelRatioF())),y=int(std::floor(at.y*canvas.devicePixelRatioF()));int result=0;for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx){QPoint q(x+dx,y+dy);require(image.rect().contains(q),"Boundary outside captured viewport");result=std::max(result,difference(image.pixelColor(q),QColor(70,120,180,255)));}return result;}
QJsonArray boundaryContrast(const NativeCanvas& canvas,const QImage& image){QJsonArray result;for(const auto point:std::array<Point,4>{{{14980,15000},{15020,15000},{15000,14980},{15000,15020}}})result.append(contrast(canvas,image,point));return result;}
bool allAtLeast(const QJsonArray& values,int threshold){return std::all_of(values.begin(),values.end(),[=](const QJsonValue& v){return v.toInt()>=threshold;});}
bool allAtMost(const QJsonArray& values,int threshold){return std::all_of(values.begin(),values.end(),[=](const QJsonValue& v){return v.toInt()<=threshold;});}
}
int main(int argc,char** argv){QApplication application(argc,argv);if(argc!=2)return 2;const std::filesystem::path directory(argv[1]);std::filesystem::create_directories(directory);QJsonArray checks;int failures=0;double dpr=1;auto record=[&](const char* name,bool pass,QJsonObject detail=QJsonObject{}){detail.insert("name",name);detail.insert("pass",pass);checks.append(detail);failures+=!pass;std::cout<<(pass?"PASS ":"FAIL ")<<name<<'\n';};try{
 {
  NativeCanvas canvas(true);canvas.resize(600,460);canvas.setRaster(Raster::filled(100,80,{70,120,180,255}));canvas.setDisplayProfileOverride(directory/"missing.icc");canvas.show();pump();canvas.zoomAt(4*canvas.devicePixelRatioF(),canvas.rect().center());canvas.showPixelGrid=false;dpr=canvas.devicePixelRatioF();
  auto outline=std::make_shared<const editing::SelectionOutline>(editing::SelectionOutline::rectangle({20,20,40,30}));auto dense=outline->rasterize(100,80);auto sparse=GrayRaster::sampled(100,80,{20,20,40,30},[dense](int x,int y){return dense->pixel(x,y);},dense->retainedBytes(),outline);
  canvas.setSelection(dense,outline);auto* timer=canvas.findChild<QTimer*>("selectionAntsTimer");require(timer,"Selection timer missing");timer->stop();auto before=capture(canvas,directory/"dense.png");canvas.setSelection(sparse,outline);timer->stop();auto after=capture(canvas,directory/"sparse.png");require(before.size()==after.size(),"Representation capture sizes differ");QImage diff(before.size(),QImage::Format_RGBA8888);int maximum=0;int changed=0;for(int y=0;y<before.height();++y)for(int x=0;x<before.width();++x){const int delta=difference(before.pixelColor(x,y),after.pixelColor(x,y));maximum=std::max(maximum,delta);changed+=delta>0;diff.setPixelColor(x,y,QColor(delta,delta,delta,255));}save(diff,directory/"dense-sparse-diff.png");record("sparse_dense_rectangle_presentation",maximum==0,{{"maximum_channel_difference",maximum},{"different_pixels",changed},{"tolerance",0}});canvas.close();pump();
 }
 {
  MainWindow window(true);Document document;document.id="native-large-sparse";document.width=document.height=30000;Layer layer;layer.id="source";layer.transform={14950,14960,100,80};layer.raster=Raster::filled(100,80,{70,120,180,255});document.layers={layer};auto& project=window.addProject(document);window.resize(1180,880);window.show();window.activateWindow();command(window,"tool.marquee");auto& canvas=*project.canvas;canvas.setDisplayProfileOverride(directory/"missing.icc");canvas.zoomAt(4*canvas.devicePixelRatioF(),canvas.rect().center());canvas.showPixelGrid=false;canvas.setFocus();auto before=capture(canvas,directory/"large-before.png");
  canvas.pointerDown({14980,14980},{});canvas.pointerMove({15020,15020},{});auto draft=capture(canvas,directory/"large-draft.png");const bool draftUntouched=project.document==document&&project.history.undoCount()==0&&canvas.selectionDraft().has_value();canvas.pointerCancel();auto cancelled=capture(canvas,directory/"large-cancelled.png");record("large_sparse_draft_cancel",draftUntouched&&project.document==document&&project.history.undoCount()==0&&!canvas.selectionDraft()&&allAtMost(boundaryContrast(canvas,cancelled),1));
  canvas.pointerDown({14980,14980},{});canvas.pointerMove({15020,15020},{});canvas.pointerUp({15020,15020},{});auto committed=capture(canvas,directory/"large-committed.png");require(project.document&&project.document->selection&&project.document->selection->coverage,"Selection missing");const auto selection=project.document->selection;const auto coverage=selection->coverage;const auto bounds=coverage->nonzeroBounds();record("large_sparse_storage_bounds",coverage->pixels.empty()&&coverage->source&&coverage->validStorage()&&coverage->retainedBytes()<=16*1024*1024&&coverage->pixel(15000,15000)==255&&coverage->pixel(14979,15000)==0&&bounds.x==14980&&bounds.y==14980&&bounds.width==40&&bounds.height==40,{{"retained_bytes",qint64(coverage->retainedBytes())},{"dense_bytes",qint64(coverage->pixels.size())},{"maximum_retained_bytes",16*1024*1024}});
  const auto edges=boundaryContrast(canvas,committed);Raster::resetMaterializationCount();canvas.captureRendered();const auto materializations=Raster::materializationCount();record("large_sparse_committed_boundary",project.history.undoCount()==1&&allAtLeast(edges,15)&&materializations==0,{{"four_edge_maximum_contrasts",edges},{"minimum_contrast",15},{"capture_rgba_materializations",qint64(materializations)}});
  command(window,"edit.undo");auto undone=capture(canvas,directory/"large-undo.png");record("large_sparse_undo",project.document==document&&project.history.undoCount()==0&&allAtMost(boundaryContrast(canvas,undone),1));command(window,"edit.redo");auto redone=capture(canvas,directory/"large-redo.png");record("large_sparse_redo",project.document->selection==selection&&project.history.undoCount()==1&&allAtLeast(boundaryContrast(canvas,redone),15));
 }
 QFile report(QString::fromStdWString((directory/"results.json").wstring()));require(report.open(QIODevice::WriteOnly),"Report open failed");report.write(QJsonDocument(QJsonObject{{"schema","NATIVE_SPARSE_SELECTION_V1"},{"checks",checks},{"failed",failures},{"dpr",dpr},{"mac_differential",false},{"animated_ants_parity",false}}).toJson());
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}return failures?1:0;}

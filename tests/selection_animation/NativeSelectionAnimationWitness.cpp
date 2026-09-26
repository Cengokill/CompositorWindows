// Fixed source-derived before witness. TransformOverlay129-143 uses one white
// line with a continuous black [4,4] dash. EditorCanvas1685-1699 advances phase
// once per120ms, only for an existing nonempty selection.
#include "ui/NativeCanvas.h"
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
void pump(int milliseconds){QElapsedTimer timer;timer.start();while(timer.elapsed()<milliseconds){QApplication::processEvents();QThread::msleep(2);}}
QImage capture(NativeCanvas& canvas,const std::filesystem::path& file){auto image=canvas.captureRendered();require(!canvas.presentationConvertsColor()&&!canvas.presentationDiagnostic().isEmpty(),"sRGB isolation absent");require(image.save(QString::fromStdWString(file.wstring())),"Capture save failed");return image;}
// The line's two possible colors are distinguished at its center without
// treating CoreGraphics edge antialiasing as a Windows oracle.
bool whiteAt(const NativeCanvas& canvas,const QImage& image,Point point,bool vertical){const auto q=canvas.viewMapping().toView(point);const int x=int(std::floor(q.x*canvas.devicePixelRatioF())),y=int(std::floor(q.y*canvas.devicePixelRatioF()));int maximum=0;for(int offset=-1;offset<=1;++offset){const QPoint sample(x+(vertical?offset:0),y+(vertical?0:offset));require(image.rect().contains(sample),"Reference point not visible");const auto c=image.pixelColor(sample);maximum=std::max(maximum,c.red()+c.green()+c.blue());}return maximum>460;}
}
int main(int argc,char** argv){QApplication application(argc,argv);if(argc!=2)return 2;const std::filesystem::path directory(argv[1]);std::filesystem::create_directories(directory);QJsonArray checks;int failures=0;try{
 NativeCanvas canvas(true);canvas.resize(600,460);canvas.setRaster(Raster::filled(100,80,{70,120,180,255}));canvas.setDisplayProfileOverride(directory/"missing.icc");canvas.show();pump(50);canvas.zoomAt(4*canvas.devicePixelRatioF(),canvas.rect().center());canvas.showPixelGrid=false;
 //52x44 DIP edges deliberately carry four DIP phase across both corners.
 auto outline=std::make_shared<const editing::SelectionOutline>(editing::SelectionOutline::rectangle({35,30,13,11}));auto dense=outline->rasterize(100,80);auto sparse=GrayRaster::sampled(100,80,{35,30,13,11},[dense](int x,int y){return dense->pixel(x,y);},dense->retainedBytes(),outline);canvas.setSelection(sparse);
 auto record=[&](const char* name,bool pass,QJsonObject detail=QJsonObject{}){detail.insert("name",name);detail.insert("pass",pass);checks.append(detail);failures+=!pass;std::cout<<(pass?"PASS ":"FAIL ")<<name<<'\n';};
 const auto first=capture(canvas,directory/"phase-0.png");int best=100000,total=0;for(int direction:{-1,1})for(int phase=0;phase<8;++phase){int errors=0,count=0;for(int side=0;side<4;++side){const int length=side%2?44:52;const int previous=side==0?0:side==1?52:side==2?96:148;for(int distance=3;distance<length-3;++distance){const double at=distance+.5;const Point point=side==0?Point{35+at/4,30}:side==1?Point{48,30+at/4}:side==2?Point{48-at/4,41}:Point{35,41-at/4};int position=(direction*(previous+distance)+phase)%8;if(position<0)position+=8;const bool expectedWhite=position>=4;errors+=whiteAt(canvas,first,point,side%2!=0)!=expectedWhite;++count;}}best=std::min(best,errors);total=count;}
 record("selection_dash_period_and_continuity",best==0,{{"minimum_mismatched_samples",best},{"sample_count",total},{"allowed_mismatches",0},{"period_dips",8}});
 std::vector<QImage> frames{first};for(int index=1;index<=5;++index){pump(130);frames.push_back(capture(canvas,directory/("phase-"+std::to_string(index)+".png")));}int unique=0;for(size_t i=0;i<frames.size();++i){bool seen=false;for(size_t j=0;j<i;++j)seen=seen||frames[i]==frames[j];unique+=!seen;}record("selection_animation_advances",unique>=3,{{"unique_frames",unique},{"minimum_unique_frames",3},{"sample_interval_ms",130},{"samples",6}});
 canvas.setSelection({});auto absent=capture(canvas,directory/"absent-0.png");pump(390);auto absentLater=capture(canvas,directory/"absent-1.png");record("selection_empty_static",absent==absentLater);
 QFile output(QString::fromStdWString((directory/"results.json").wstring()));require(output.open(QIODevice::WriteOnly),"Report open failed");output.write(QJsonDocument(QJsonObject{{"schema","NATIVE_SELECTION_ANIMATION_V1"},{"checks",checks},{"failed",failures},{"dpr",canvas.devicePixelRatioF()},{"mac_differential",false}}).toJson());
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}return failures?1:0;}

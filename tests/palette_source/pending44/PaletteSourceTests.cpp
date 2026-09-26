#include "ui/MainWindow.h"
#include "ui/PaletteDialog.h"
#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLayout>
#include <QPointer>
#include <QScreen>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <cstdio>
#include <map>

using namespace compositor;
namespace {
QJsonArray checks;
int failures=0;
const char* source="CompositorTests/ColorPickerTests.swift";
void guard(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void observe(int line,bool value,const char* kind="expect"){
    checks.append(QJsonObject{{"source_file",source},{"source_line",line},{"kind",kind},{"passed",value}});
    if(!value){++failures;std::fprintf(stderr,"FAIL %s:%d\n",source,line);}
}
void required(int line,bool value){observe(line,value,"require");guard(value,"Original source requirement failed");}
template<class Tag,typename Tag::type Member>struct Access{friend typename Tag::type member(Tag){return Member;}};
struct Picker{using type=QPointer<PaletteDialog> MainWindow::*;friend type member(Picker);};
struct Foreground{using type=QColor MainWindow::*;friend type member(Foreground);};
struct Background{using type=QColor MainWindow::*;friend type member(Background);};
struct Open{using type=void(MainWindow::*)(bool);friend type member(Open);};
struct Begin{using type=bool(MainWindow::*)(Point,Qt::KeyboardModifiers);friend type member(Begin);};
struct Update{using type=bool(MainWindow::*)(Point,bool);friend type member(Update);};
template struct Access<Picker,&MainWindow::colorPicker_>;
template struct Access<Foreground,&MainWindow::foreground_>;
template struct Access<Background,&MainWindow::background_>;
template struct Access<Open,&MainWindow::openPalette>;
template struct Access<Begin,&MainWindow::beginPalette>;
template struct Access<Update,&MainWindow::updatePalette>;

Document document(int width,int height,std::shared_ptr<const Raster> raster={}){
    Document d;d.id=newId();d.width=width;d.height=height;
    if(raster){Layer l;l.id=newId();l.name="Source fixture";l.transform={0,0,double(width),double(height)};l.raster=std::move(raster);d.layers.push_back(std::move(l));}
    return d;
}
struct Session{
    MainWindow window{true};EditorProject& project;
    explicit Session(Document d):project(window.addProject(std::move(d))){window.resize(1000,750);window.show();QApplication::processEvents();}
    QPointer<PaletteDialog> picker(){return window.*member(Picker{});}
    QColor foreground(){return window.*member(Foreground{});}
    QColor background(){return window.*member(Background{});}
    void open(bool background){(window.*member(Open{}))(background);QApplication::processEvents();}
    void sample(Point point){guard((window.*member(Begin{}))(point,{}),"Native palette sampling begins");guard((window.*member(Update{}))(point,true),"Native palette sampling completes");}
    void close(bool commit){guard(!picker().isNull(),"Palette exists before close");if(commit)picker()->accept();else picker()->reject();QApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QApplication::processEvents();}
};
void sampling(){
    // Core Graphics fixture coordinates are bottom-up. The original expected
    // samples define red in native top rows and blue in native bottom rows.
    std::vector<Pixel> pixels(16);required(37,pixels.size()==16);
    for(int y=0;y<4;++y)for(int x=0;x<4;++x)pixels[size_t(y)*4+x]=y<2?Pixel{255,0,0,255}:Pixel{0,0,255,255};
    auto raster=Raster::fromRgba(4,4,reinterpret_cast<const uint8_t*>(pixels.data()),16);required(43,bool(raster));
    Session s(document(4,4,std::move(raster)));const auto before=s.project.document;
    const auto sample=[&](Point point){return effects_tools::sampleCompositeColor(*s.project.document,point,SoftwareRenderer());};
    auto top=sample({1.5,.5}),bottom=sample({1.5,3.5});
    observe(46,top&&top->hex()=="FF0000");observe(47,bottom&&bottom->hex()=="0000FF");observe(48,!sample({-1,1}));
    s.open(false);s.sample({2,3});observe(52,s.picker()&&s.picker()->color().hex()=="0000FF");observe(53,s.foreground()==Qt::black);
    s.close(false);observe(55,s.foreground()==Qt::black&&s.picker().isNull());
    s.open(true);s.sample({2,0});s.close(true);observe(60,s.background()==Qt::red&&s.foreground()==Qt::black);
    guard(s.project.document==before&&s.project.history.undoCount()==0,"Palette workflow preserves document and edit history");
}
void position(){
    Session s(document(4,4));s.open(false);required(68,!s.picker().isNull());
    QPointer<PaletteDialog> original=s.picker();required(69,original&&original->isVisible());
    auto* screen=original->screen();if(!screen)screen=QGuiApplication::primaryScreen();required(70,screen!=nullptr);
    const QPoint spot=screen->availableGeometry().topLeft()+QPoint(40,40);
    original->move(spot);QApplication::processEvents();guard(original->frameGeometry().topLeft()==spot,"Native top-left coordinate matches source screen placement");
    s.close(false);observe(75,original.isNull()||!original->isVisible());
    s.open(true);required(77,!s.picker().isNull());observe(78,s.picker()->frameGeometry().topLeft()==spot);
    s.open(false);required(81,!s.picker().isNull());observe(82,s.picker()->frameGeometry().topLeft()==spot);s.close(false);
}
class PaintObserver final:public QObject{
public:int paints=0;
protected:bool eventFilter(QObject*,QEvent* event)override{if(event->type()==QEvent::Paint)++paints;return false;}
};
void layout(){
    source="CompositorTests/FloatingPanelTests.swift";
    auto raster=Raster::filled(40,20,{255,0,0,255});required(15,bool(raster));
    Session s(document(40,20,std::move(raster)));s.open(false);required(46,!s.picker().isNull());
    PaintObserver observer;s.picker()->installEventFilter(&observer);s.picker()->update();QTest::qWait(400);
    const auto panel=s.picker();required(48,!panel.isNull());observe(49,panel->isVisible()&&panel->layout()!=nullptr);
    guard(observer.paints>0,"Visible Qt palette completed a real paint pass");s.close(false);
}
}
int main(int argc,char** argv){
    QApplication app(argc,argv);QTemporaryDir settings;guard(settings.isValid(),"Private test settings available");
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    const std::map<std::string,void(*)()> cases{{"sampling",sampling},{"position",position},{"layout",layout}};
    const std::string key=argc>1?argv[1]:"";bool complete=false;std::string error;
    try{guard(argc==3&&cases.contains(key),"Provide CASE NEW_REPORT.json");guard(!QFile::exists(QString::fromLocal8Bit(argv[2])),"Fresh output required");cases.at(key)();complete=true;}catch(const std::exception& e){error=e.what();}
    const bool passed=complete&&failures==0;
    QJsonObject result{{"schema","PALETTE_SOURCE_RESULT_V1"},{"case",QString::fromStdString(key)},{"status",passed?"passed":complete?"failed":"error"},{"complete",complete},{"source_expect_failures",failures},{"checks",checks},{"error",QString::fromStdString(error)},{"platform",QGuiApplication::platformName()},{"human_acceptance",false}};
    if(argc==3){QFile out(QString::fromLocal8Bit(argv[2]));if(!out.open(QIODevice::WriteOnly|QIODevice::NewOnly)||out.write(QJsonDocument(result).toJson())<0)return 2;}
    std::printf("%s %s checks=%lld failures=%d\n",passed?"PASS":"FAIL",key.c_str(),static_cast<long long>(checks.size()),failures);if(!error.empty())std::fprintf(stderr,"%s\n",error.c_str());return passed?0:complete?1:2;
}

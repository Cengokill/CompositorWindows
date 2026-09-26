#include "ui/MainWindow.h"
#include <QApplication>
#include <QComboBox>
#include <QCursor>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTest>
#include <array>
#include <cstdio>
#include <map>
using namespace compositor;
namespace {
QJsonArray checks;int failures{};QString capturePath;
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void check(const char* name,bool value){checks.append(QJsonObject{{"id",name},{"passed",value}});if(!value){++failures;std::fprintf(stderr,"FAIL %s\n",name);}}
QAction* command(MainWindow& w,const char* id){for(auto* a:w.findChildren<QAction*>())if(a->property("commandId").toString()==id&&a->isEnabled())return a;throw std::runtime_error(std::string("Enabled command missing: ")+id);}
struct Fixture {
 MainWindow window{true};EditorProject& project;
 Fixture():project(window.addProject([]{Document d;d.id=newId();d.width=d.height=100;Layer l;l.id=newId();l.transform={0,0,100,100};d.layers={l};return d;}())){window.show();QApplication::processEvents();}
 void icon(int k){command(window,k<2?"tool.lasso":"tool.marquee")->trigger();if(k<2){auto* c=window.findChild<QComboBox*>("lassoKind");require(c,"Lasso kind");c->setCurrentIndex(k);}else{bool found=false;for(auto* c:window.findChildren<QComboBox*>())if(c->accessibleName()=="Marquee shape"){c->setCurrentIndex(k-2);found=true;break;}require(found,"Marquee shape");}hover();}
 void mode(int m){auto* c=window.findChild<QComboBox*>("selectionMode");require(c,"Selection mode");c->setCurrentIndex(m);hover();}
 void hover(Qt::KeyboardModifiers m={}){require(bool(project.canvas->pointerHover),"Native hover callback");project.canvas->pointerHover(QPointF(12,12),m);}
 QCursor cursor(){return project.canvas->cursor();}
 QImage image(){return cursor().pixmap().toImage().convertToFormat(QImage::Format_ARGB32);}
};
int count(const QImage& image,QRectF logical,qreal dpr,bool black){int n=0;QRect box(int(logical.x()*dpr),int(logical.y()*dpr),int(logical.width()*dpr),int(logical.height()*dpr));box=box.intersected(image.rect());for(int y=box.top();y<=box.bottom();++y)for(int x=box.left();x<=box.right();++x){auto c=image.pixelColor(x,y);if(c.alpha()>160&&(black?c.red()<80:c.red()>180))++n;}return n;}
double blackCoverage(const QImage& image,QRectF logical,qreal dpr){
    const QRect box(qRound(logical.x()*dpr),qRound(logical.y()*dpr),qRound(logical.width()*dpr),qRound(logical.height()*dpr));
    require(image.rect().contains(box),"Badge observation lies inside actual image");
    double area=0;
    for(int y=box.top();y<=box.bottom();++y)for(int x=box.left();x<=box.right();++x){const auto c=image.pixelColor(x,y);area+=double(c.alpha())*(255-c.red())/(255.*255.);}
    return area/(dpr*dpr);
}
bool sourceStrokeArea(double observed,qreal dpr,bool present){
    // One logical unit of an interior straight segment times source width1.2.
    // Only the two edge samples per physical segment row may round down by
    // one byte: area error <=2/(255*dpr). No cap/intersection is sampled.
    const double expected=present?1.2:0;
    const double tolerance=present?2/(255.*dpr)+1e-9:0;
    std::fprintf(stdout,"badge area=%.17g expected=%.17g tolerance=%.17g\n",observed,expected,tolerance);
    return std::abs(observed-expected)<=tolerance;
}
void artwork(int k,int m){Fixture f;f.icon(k);f.mode(m);auto c=f.cursor();auto p=c.pixmap();const qreal dpr=f.project.canvas->devicePixelRatioF();check("bitmap_cursor",c.shape()==Qt::BitmapCursor&&!p.isNull());if(p.isNull())return;check("logical44x36",p.size()/p.devicePixelRatio()==QSize(44,36));check("device_scale",p.devicePixelRatio()==dpr);const auto hot=c.hotSpot();check("bounded_hotspot",hot.x()>=0&&hot.y()>=0&&hot.x()<20&&hot.y()<16);const auto image=f.image();require(image.save(capturePath+".png"),"Retain actual assigned cursor pixels");const QRectF icon(hot.x()+6,hot.y()+6,14,14);check("icon_black",count(image,icon,dpr,true)>3);check("icon_white",count(image,icon,dpr,false)>3);const QPointF center(hot.x()+24,hot.y()+13);check("badge_horizontal",sourceStrokeArea(blackCoverage(image,{center.x()+1,center.y()-1,1,2},dpr),dpr,m!=0));check("badge_vertical",sourceStrokeArea(blackCoverage(image,{center.x()-1,center.y()-2,2,1},dpr),dpr,m==1));}
void icon_distinct(){Fixture f;std::array<QImage,4> a;for(int k=0;k<4;++k){f.icon(k);f.mode(0);a[k]=f.image();require(!a[k].isNull(),"All four cursor images");}for(int k=0;k<4;++k)for(int j=0;j<k;++j)check("four_distinct_icons",a[k]!=a[j]);}
void modifier_modes(){Fixture f;f.icon(0);f.mode(0);auto original=f.image();f.hover(Qt::ShiftModifier);auto add=f.image();f.hover(Qt::AltModifier|Qt::ShiftModifier);auto subtract=f.image();f.hover();check("modifier_artwork",!original.isNull()&&original!=add&&add!=subtract&&subtract!=original);check("modifier_release_restores",f.image()==original);check("choice_unmodified",f.window.findChild<QComboBox*>("selectionMode")->currentIndex()==0);}
void remembered_choice(){Fixture f;f.icon(3);f.mode(1);auto add=f.image();f.hover(Qt::AltModifier);auto subtract=f.image();f.hover();check("remembered_add",!add.isNull()&&subtract!=add&&f.image()==add);}
void draft_locks_mode(){Fixture f;f.icon(0);f.mode(2);auto subtract=f.image();f.project.canvas->pointerDown({20,20},Qt::AltModifier);f.project.canvas->pointerMove({40,20},{});f.hover(Qt::ShiftModifier);check("draft_start_mode_locked",!subtract.isNull()&&f.image()==subtract);f.project.canvas->pointerCancel();f.hover();check("cancel_restores_choice",f.image()==subtract);}
void cache_reuse(){Fixture f;f.icon(0);f.mode(0);const auto first=f.cursor().pixmap().cacheKey();f.icon(1);f.icon(0);check("same_cursor_pixmap_cached",first!=0&&f.cursor().pixmap().cacheKey()==first);}
void hand_override(){Fixture f;f.icon(2);const auto before=f.image();QTest::keyPress(f.project.canvas,Qt::Key_Space);f.hover(Qt::ShiftModifier);check("temporary_hand_priority",f.cursor().shape()==Qt::OpenHandCursor);QTest::keyRelease(f.project.canvas,Qt::Key_Space);f.hover();check("hand_release_selection",!before.isNull()&&f.image()==before);}
void immutable_state(){Fixture f;const auto before=f.project.document;const auto n=f.project.history.undoCount();for(int k=0;k<4;++k)for(int m=0;m<3;++m){f.icon(k);f.mode(m);}check("cursor_no_document_edit",before==f.project.document);check("cursor_no_history",n==f.project.history.undoCount());}
void move_override(){Fixture f;f.icon(2);command(f.window,"selection.all")->trigger();f.project.canvas->pointerDown({20,20},{});check("moving_outline_priority",f.cursor().shape()==Qt::SizeAllCursor);f.project.canvas->pointerCancel();f.hover();check("move_cancel_selection_cursor",f.cursor().shape()==Qt::BitmapCursor);}
}
int main(int argc,char** argv){QApplication app(argc,argv);QString error;const std::string key=argc>1?argv[1]:"";bool complete=false;try{require(argc==3,"CASE ABS_NEW_REPORT");capturePath=QString::fromLocal8Bit(argv[2]);require(!QFile::exists(QString::fromLocal8Bit(argv[2])),"Fresh report");const std::map<std::string,void(*)()> other{{"icon_distinct",icon_distinct},{"modifier_modes",modifier_modes},{"remembered_choice",remembered_choice},{"draft_locks_mode",draft_locks_mode},{"cache_reuse",cache_reuse},{"hand_override",hand_override},{"immutable_state",immutable_state},{"move_override",move_override}};if(key.starts_with("artwork_")){require(key.size()==11&&key[8]>='0'&&key[8]<='3'&&key[10]>='0'&&key[10]<='2',"Artwork key");artwork(key[8]-'0',key[10]-'0');}else{require(other.contains(key),"Known case");other.at(key)();}complete=true;}catch(const std::exception& e){error=QString::fromUtf8(e.what());}const bool passed=complete&&!failures;QJsonObject report{{"schema","SELECTION_CURSOR_NATIVE_V1"},{"case",QString::fromStdString(key)},{"status",passed?"passed":complete?"failed":"error"},{"checks",checks},{"error",error},{"mac_pixels","blocked_reference"},{"original144_credit",false}};if(argc==3){QFile out(QString::fromLocal8Bit(argv[2]));if(!out.open(QIODevice::WriteOnly|QIODevice::NewOnly))return 2;out.write(QJsonDocument(report).toJson());}std::fprintf(stdout,"%s %s checks=%lld\n",passed?"PASS":"FAIL",key.c_str(),static_cast<long long>(checks.size()));return passed?0:complete?1:2;}

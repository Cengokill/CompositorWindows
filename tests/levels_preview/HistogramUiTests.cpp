#include "ui/AdjustmentDialog.h"
#include "effects_tools/Levels.h"
#include "effects/Adjustments.h"
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QPushButton>
#include <QTimer>
#include <iostream>
#include <stdexcept>
using namespace compositor;
int main(int argc,char**argv){QApplication app(argc,argv);try{
    Document d;d.id="capped-levels-ui";d.width=9001;d.height=3;Layer l;l.id="source";l.transform={0,0,9001,3};std::vector<Pixel> pixels(27003);for(int y=0;y<3;++y)for(int x=0;x<9001;++x){const auto v=uint8_t(y==1?0:x%2?180:80);pixels[size_t(y)*9001+x]={v,v,v,255};}l.raster=Raster::fromRgba(9001,3,reinterpret_cast<const uint8_t*>(pixels.data()),9001*4);d.layers.push_back(l);const auto before=d;
    QTimer timer;timer.setInterval(20);QElapsedTimer elapsed;elapsed.start();bool selected=false,timedOut=false;QObject::connect(&timer,&QTimer::timeout,[&]{auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)return;auto* automatic=dialog->findChild<QPushButton*>("levelsAutoContrast");auto* buttons=dialog->findChild<QDialogButtonBox*>();if(!automatic||!buttons)return;if(!selected&&automatic->isEnabled()){selected=true;automatic->click();}else if(selected&&buttons->button(QDialogButtonBox::Apply)->isEnabled()){buttons->button(QDialogButtonBox::Apply)->click();}else if(elapsed.elapsed()>10000){timedOut=true;dialog->reject();}});
    std::string settings;AdjustmentDialogOptions options;options.onApply=[&](const std::string& value){settings=value;};timer.start();auto result=showAdjustmentDialog(nullptr,d,"source","Levels",false,false,options);timer.stop();if(timedOut||!selected||!result||settings.empty())throw std::runtime_error("Levels auto histogram did not finish");const auto range=effects_tools::levelsFromAdjustmentJson(settings).ranges[0];std::cout<<"actual_black="<<range.black<<" actual_white="<<range.white<<" expected_black=80 expected_white=180\n";if(range.black!=80||range.white!=180)throw std::runtime_error("Auto Levels did not use the source8000 preview histogram");const auto& output=result->document.layers[0];if(d!=before||output.transform!=l.transform||output.raster->width!=9001||output.raster->height!=3||output.raster->rgba()!=effects::applyAdjustment(l.raster,settings)->rgba())throw std::runtime_error("Capped histogram changed immutable original or source-sized Apply");std::cout<<"PASS ordinary_levels_preview_cap_auto_apply\n";return 0;
}catch(const std::exception&e){std::cout<<"FAIL ordinary_levels_preview_cap_auto_apply: "<<e.what()<<'\n';return 1;}}

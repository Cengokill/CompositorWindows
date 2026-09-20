#include "ui/PaletteDialog.h"
#include <QApplication>
#include <QLineEdit>
#include <QSpinBox>
#include <QTest>
#include <iostream>
#include <stdexcept>
using namespace compositor;
int main(int argc,char**argv){QApplication app(argc,argv);try{
 auto require=[](bool ok,const char*message){if(!ok)throw std::runtime_error(message);};
 PaletteDialog dialog({1,0,0},"Palette test");dialog.show();QApplication::processEvents();
 auto*hex=dialog.findChild<QLineEdit*>("paletteHex");require(hex,"Hex field absent");hex->setFocus();hex->setText("#3aF");QTest::keyClick(hex,Qt::Key_Tab);require(dialog.color()==effects_tools::PaletteColor{51/255.,170/255.,1},"Hex edit did not quantize correctly");
 hex->setFocus();hex->setText("invalid");QTest::keyClick(hex,Qt::Key_Tab);require(hex->text()=="33AAFF","Invalid hex did not restore current color");
 dialog.findChild<QSpinBox*>("paletteRGB1")->setValue(64);require(dialog.color().green==64/255.,"RGB channel edit failed");
 dialog.sample({0,1,0});auto*field=dialog.findChild<QWidget*>("paletteField");QTest::mouseClick(field,Qt::LeftButton,{},QPoint(128,128));auto mid=dialog.color();require(mid.green>mid.red&&mid.green>mid.blue&&mid.green>=.49&&mid.green<=.51,"Saturation/brightness mouse control failed");
 dialog.sample({0,0,0});QTest::mouseClick(field,Qt::LeftButton,{},QPoint(256-1,0));require(dialog.color().green>.99&&dialog.color().red<.01,"Black sampling lost retained hue");
 std::cout<<"PASS palette hex, invalid input, RGB, mouse field and hue preservation\n";return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}

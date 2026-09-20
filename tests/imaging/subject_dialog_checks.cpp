#include "SubjectDialog.h"
#include "wic_codec.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <iostream>
using namespace compositor::imaging;
int main(int argc,char** argv){QApplication app(argc,argv);try{if(argc!=3)throw std::runtime_error("Usage: subject_dialog_checks model.onnx image.png");auto source=WicCodec::decode(argv[2]).image;auto unchanged=source.pixels;
    QTimer::singleShot(100,[]{auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(dialog)dialog->reject();});
    if(showSubjectDialog(nullptr,source,nullptr,argv[1]))throw std::runtime_error("Cancel unexpectedly returned a mask");
    if(source.pixels!=unchanged)throw std::runtime_error("Cancel changed source pixels");
    GrayMask existing{source.width,source.height,source.width,std::vector<std::uint8_t>(std::size_t(source.width)*source.height,128)};bool timeout=false,applied=false;QTimer poll,deadline;poll.setInterval(50);deadline.setSingleShot(true);
    QObject::connect(&deadline,&QTimer::timeout,[&]{timeout=true;if(auto* d=qobject_cast<QDialog*>(QApplication::activeModalWidget()))d->reject();});
    QObject::connect(&poll,&QTimer::timeout,[&]{auto* d=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!d)return;auto* quality=d->findChild<QComboBox*>("backgroundQuality");auto* apply=d->findChild<QPushButton*>("applySubjectMask");if(!quality||!apply||!apply->isEnabled()||applied)return;quality->setCurrentIndex(1);for(auto* spin:d->findChildren<QSpinBox*>()){if(spin->accessibleName()=="Refine"&&(spin->minimum()!=0||spin->maximum()!=40||spin->value()!=12))throw std::runtime_error("Refine controls changed");if(spin->accessibleName()=="Contrast"&&(spin->minimum()!=0||spin->maximum()!=100||spin->value()!=25))throw std::runtime_error("Contrast controls changed");if(spin->accessibleName()=="Shift Edge"&&(spin->minimum()!=-10||spin->maximum()!=10||spin->value()!=0))throw std::runtime_error("Shift controls changed");}applied=true;apply->click();});
    poll.start();deadline.start(30000);auto mask=showSubjectDialog(nullptr,source,&existing,argv[1]);poll.stop();deadline.stop();if(timeout||!applied||!mask)throw std::runtime_error("Advanced Apply did not complete");if(mask->width!=source.width||mask->height!=source.height)throw std::runtime_error("Committed mask dimensions differ");for(auto p:mask->pixels)if(p>128)throw std::runtime_error("Existing mask was not multiplied");if(source.pixels!=unchanged)throw std::runtime_error("Dialog mutated source");std::cout<<"Subject dialog Cancel and Advanced Apply passed; full-resolution combined mask; source preserved\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

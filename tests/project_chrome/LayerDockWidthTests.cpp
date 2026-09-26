#include "ui/MainWindow.h"
#include <QApplication>
#include <QDockWidget>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <iostream>
using namespace compositor;
int main(int argc,char** argv){QApplication app(argc,argv);if(argc!=4)return 2;const QString key=QString::fromLocal8Bit(argv[1]),root=QString::fromLocal8Bit(argv[2]),output=QString::fromLocal8Bit(argv[3]);QCoreApplication::setOrganizationName("CompositorFixture");QCoreApplication::setApplicationName("ProjectChrome");QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root);QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,root);QJsonObject report{{"case",key},{"settings_root",root}};bool passed=false;
try{QSettings settings;if(key!="restart_read")settings.clear();if(key=="restored_width")settings.setValue("layersPanelWidth",318);settings.sync();MainWindow w(true);w.show();QTest::qWait(200);auto* dock=w.findChild<QDockWidget*>("layersDock");if(!dock)throw std::runtime_error("missing layers dock");report["initial_width"]=dock->width();report["minimum"]=dock->minimumWidth();report["maximum"]=dock->maximumWidth();
if(key=="default_width")passed=dock->width()==252;
else if(key=="restored_width")passed=dock->width()==318;
else if(key=="resize_is_saved"||key=="restart_write"){w.resizeDocks({dock},{318},Qt::Horizontal);QTest::qWait(250);settings.sync();report["resized_width"]=dock->width();report["stored_value"]=settings.value("layersPanelWidth").toDouble();passed=dock->width()==318&&settings.value("layersPanelWidth").toDouble()==318;}
else if(key=="restart_read")passed=dock->width()==318;
else if(key=="width_bounds"){w.resizeDocks({dock},{100},Qt::Horizontal);QTest::qWait(200);const int low=dock->width();w.resizeDocks({dock},{500},Qt::Horizontal);QTest::qWait(200);const int high=dock->width();report["low_width"]=low;report["high_width"]=high;passed=low==202&&high==352;}
else throw std::runtime_error("unknown case");report["settings_file"]=settings.fileName();report["status"]=passed?"passed":"failed";
}catch(const std::exception& e){report["status"]="error";report["error"]=e.what();}QFile f(output);if(!f.open(QIODevice::WriteOnly)||f.write(QJsonDocument(report).toJson())<0)return 2;std::cout<<(passed?"PASS ":"FAIL ")<<key.toStdString()<<'\n';return passed?0:1;}
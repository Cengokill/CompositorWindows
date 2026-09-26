// Additional source-initial-state regression; not part of the original179 checks.
#include "ui/MainWindow.h"
#include "effects/Adjustments.h"
#include "persistence/ProjectStore.h"
#include <QApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <iostream>
using namespace compositor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
QJsonObject parsed(const std::string& value){const auto json=QJsonDocument::fromJson(QByteArray::fromStdString(value));require(json.isObject(),"Stored adjustment is valid JSON");return json.object();}
void run(QJsonObject& observations){
    QTemporaryDir directory(QFileInfo(QString::fromUtf8(__FILE__)).dir().filePath("fixtures/initial-XXXXXX"));require(directory.isValid(),"Owned fixture directory");directory.setAutoRemove(false);observations["fixture"]=directory.path();
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,directory.path());
    MainWindow window(true);Document document;document.id=newId();document.width=document.height=2;Layer image;image.id=newId();image.transform={0,0,2,2};image.raster=Raster::filled(2,2,{255,255,255,255});document.layers.push_back(image);auto& project=window.addProject(document);window.show();QTest::qWait(20);
    QAction* command=nullptr;for(auto* action:window.findChildren<QAction*>())if(action->property("commandId").toString()=="adjust.new.exposure"&&action->isVisible())command=action;
    require(command&&command->isEnabled(),"New Exposure command enabled");command->trigger();
    QDialog* panel=nullptr;ui::EditPanelSession* controller=nullptr;QElapsedTimer deadline;deadline.start();
    for(;;){for(auto* candidate:window.findChildren<QDialog*>())if(candidate->objectName()=="adjustmentDialog"&&candidate->isVisible())panel=candidate;if(panel){for(auto* object:window.findChildren<QObject*>())if(auto* candidate=dynamic_cast<ui::EditPanelSession*>(object);candidate&&candidate->panel()==panel)controller=candidate;auto* buttons=panel->findChild<QDialogButtonBox*>();if(controller&&buttons&&buttons->button(QDialogButtonBox::Apply)->isEnabled())break;}require(deadline.elapsed()<10000,"Exposure editor becomes ready");QTest::qWait(5);}
    require(project.document&&project.document->layers.size()==2,"New live layer was created before editing");
    const auto created=parsed(project.document->layers.back().adjustmentJson);observations["created"]=created;
    // LayerAdjustment.swift:38-40 declares optionals; addAdjustment:111-115
    // assigns only Gradient Map and Grain, so a new Exposure keeps nil/omitted.
    require(!created.contains("exposureSettings"),"Source new Exposure exposureSettings is nil and omitted from encoded JSON");
    require(created==parsed(effects::defaultAdjustmentJson("Exposure")),"Created Exposure retains all source LayerAdjustment defaults");
    const auto original=controller->originalAdjustmentSettings();require(original&&parsed(*original)==created,"Editor captured the exact initial optional metadata");
    for(const auto& value:std::array<std::pair<const char*,double>,3>{{{"Exposure",0},{"Offset",0},{"Gamma",1}}}){QDoubleSpinBox* field=nullptr;for(auto* item:panel->findChildren<QDoubleSpinBox*>())if(item->accessibleName()==value.first)field=item;require(field&&field->value()==value.second,"Computed Exposure controls use source defaults despite absent stored optional");}
    panel->reject();QApplication::processEvents();require(parsed(project.document->layers.back().adjustmentJson)==created&&project.history.undoCount()==1,"Cancel preserves omitted metadata and only creation history");
    ProjectStore store(makeWicProjectCodec());const auto path=std::filesystem::path(directory.filePath("Initial.comp").toStdWString());store.save(path,*project.document,project.active);const auto loaded=store.load(path);require(parsed(loaded.document.layers.back().adjustmentJson)==created,"Project roundtrip preserves omitted optional metadata");
}
}
int main(int argc,char** argv){QApplication app(argc,argv);QDir().mkpath(QFileInfo(QString::fromUtf8(__FILE__)).dir().filePath("fixtures"));QJsonObject observations;QString error;int code=0;try{require(argc==3&&QString::fromLocal8Bit(argv[1])=="exposure_optional","Use exposure_optional NEW_OUTPUT.json");run(observations);}catch(const std::exception& failure){error=QString::fromUtf8(failure.what());code=1;}if(argc==3){QFile file(QString::fromLocal8Bit(argv[2]));if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly))return 2;file.write(QJsonDocument(QJsonObject{{"case","exposure_optional"},{"status",code?"failed":"passed"},{"error",error},{"observations",observations}}).toJson());}std::cout<<(code?"FAIL":"PASS")<<" exposure_optional "<<error.toStdString()<<'\n';return code;}

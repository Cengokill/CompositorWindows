#include "ui/MainWindow.h"
#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>
#include <stdexcept>
using namespace compositor;
namespace {
void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
Document blank(int width,int height){Document d;d.id=newId();d.width=width;d.height=height;return d;}
QAction* exportAction(MainWindow& window){for(auto* action:window.findChildren<QAction*>())if(action->text().contains("Export Image")){require(action->isEnabled(),"Export action disabled before test");return action;}throw std::runtime_error("Export action absent");}
QByteArray read(const QString& path){QFile file(path);require(file.open(QIODevice::ReadOnly),"Cannot read native export evidence");return file.readAll();}
void drive(MainWindow& window,const std::function<void()>& callback){
    std::exception_ptr error;QElapsedTimer elapsed;elapsed.start();QTimer timer;timer.setInterval(10);
    QObject::connect(&timer,&QTimer::timeout,&window,[&]{try{if(elapsed.elapsed()>15000)throw std::runtime_error("Native export dialog timed out");callback();}catch(...){error=std::current_exception();timer.stop();for(auto*widget:QApplication::topLevelWidgets())if(auto*dialog=qobject_cast<QDialog*>(widget))dialog->reject();}});
    timer.start();exportAction(window)->trigger();timer.stop();if(error)std::rethrow_exception(error);
}
template<class T>T* control(QDialog* dialog,const char* name){auto* result=dialog->findChild<T*>(name);require(result,"Missing actual export control");return result;}
void selectFileSoon(const QString& filename,int remaining=100){
    QTimer::singleShot(10,[filename,remaining]{for(auto* widget:QApplication::topLevelWidgets())if(auto*file=qobject_cast<QFileDialog*>(widget)){file->selectFile(filename);QMetaObject::invokeMethod(file,"accept",Qt::QueuedConnection);return;}if(remaining)selectFileSoon(filename,remaining-1);});
}
}
int main(int argc,char**argv){
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);QApplication app(argc,argv);QTemporaryDir settings;
    QCoreApplication::setOrganizationName("CompositorExportTests");QCoreApplication::setApplicationName("IsolatedExportUi");
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    int passed=0,failed=0;auto test=[&](const char* name,const std::function<void()>& run){try{run();++passed;std::cout<<"PASS "<<name<<'\n';}catch(const std::exception&error){++failed;std::cout<<"FAIL "<<name<<": "<<error.what()<<'\n';}};
    test("oversized_preflight",[]{MainWindow window(true);auto&project=window.addProject(blank(30000,30000));const auto before=project.document;bool message=false;Raster::resetMaterializationCount();drive(window,[&]{require(!window.findChild<QDialog*>("imageExportDialog"),"Oversized export opened renderer dialog");for(auto*box:window.findChildren<QMessageBox*>()){require(box->text().contains("100 megapixels"),"Oversized export error lost source limit");message=true;box->accept();}});require(message&&project.document==before&&!project.projectBusy&&project.history.undoCount()==0&&Raster::materializationCount()==0,"Oversized export changed project or allocated raster");});
    test("cancel_active_worker",[]{MainWindow window(true);auto&project=window.addProject(blank(10000,10000));const auto before=project.document;bool cancelled=false;int events=0;Raster::resetMaterializationCount();drive(window,[&]{auto*dialog=window.findChild<QDialog*>("imageExportDialog");if(!dialog)return;++events;require(project.projectBusy,"Project editable during export");auto*label=control<QLabel>(dialog,"exportEncodedSize");if(label->text().startsWith("Encoding")&&dialog->property("readyExportPath").toString().isEmpty()){cancelled=true;control<QDialogButtonBox>(dialog,"exportButtons")->button(QDialogButtonBox::Cancel)->click();}});require(cancelled&&events>2&&!project.projectBusy&&project.document==before&&project.history.undoCount()==0&&Raster::materializationCount()==0,"Worker cancellation changed project or blocked GUI");});
    test("supersede_and_cancel_ready",[]{MainWindow window(true);auto&project=window.addProject(blank(1000,650));const auto before=project.document;int state=0;bool ready=false;drive(window,[&]{auto*dialog=window.findChild<QDialog*>("imageExportDialog");if(!dialog)return;auto*buttons=control<QDialogButtonBox>(dialog,"exportButtons");if(state==0&&buttons->button(QDialogButtonBox::Save)->isEnabled()){auto old=dialog->property("readyExportGeneration").toULongLong();control<QComboBox>(dialog,"exportFormat")->setCurrentIndex(1);auto*quality=control<QSlider>(dialog,"exportQuality");require(quality->minimum()==0&&quality->maximum()==100,"JPEG source quality range changed");quality->setValue(10);quality->setValue(100);quality->setValue(0);require(!buttons->button(QDialogButtonBox::Save)->isEnabled(),"Stale encoded result remained saveable");dialog->setProperty("oldGeneration",old);state=1;}else if(state==1&&buttons->button(QDialogButtonBox::Save)->isEnabled()){require(dialog->property("readyExportGeneration").toULongLong()>dialog->property("oldGeneration").toULongLong(),"Superseded generation published");require(dialog->property("readyExportPath").toString().endsWith(".jpg"),"JPEG result did not replace PNG");require(!control<QLabel>(dialog,"exportPreview")->pixmap().isNull(),"Bounded encoded preview missing");ready=true;buttons->button(QDialogButtonBox::Cancel)->click();}});require(ready&&project.document==before&&!project.projectBusy&&project.history.undoCount()==0,"Cancel-ready mutated source/history");});
    test("exact_encoded_file_save",[]{QTemporaryDir output;const auto destination=output.filePath(QString::fromUtf8("export-画像.jpg"));MainWindow window(true);auto&project=window.addProject(blank(128,128));const auto before=project.document;QByteArray expected;bool selected=false,submitted=false;drive(window,[&]{auto*dialog=window.findChild<QDialog*>("imageExportDialog");if(!dialog||submitted)return;if(!selected){control<QComboBox>(dialog,"exportFormat")->setCurrentIndex(1);control<QSlider>(dialog,"exportQuality")->setValue(31);selected=true;}auto*save=control<QDialogButtonBox>(dialog,"exportButtons")->button(QDialogButtonBox::Save);if(save->isEnabled()){expected=read(dialog->property("readyExportPath").toString());submitted=true;selectFileSoon(destination);save->click();}});require(submitted&&!expected.isEmpty()&&read(destination)==expected,"Saved bytes differ from encoded preview file");require(QSettings().value("jpegExportQuality").toDouble()==.31,"JPEG quality was not retained on export");require(project.document==before&&!project.projectBusy&&project.history.undoCount()==0,"Export changed source/history");});
    std::cout<<"Export UI "<<passed<<" passed, "<<failed<<" failed\n";return failed?1:0;
}

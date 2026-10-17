#include "ui/MainWindow.h"
#include <QApplication>
#include <QAction>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
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
}
int main(int argc,char**argv){
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);QApplication app(argc,argv);QTemporaryDir settings;
    QCoreApplication::setOrganizationName("CompositorExportTests");QCoreApplication::setApplicationName("IsolatedExportUi");
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    int passed=0,failed=0;auto test=[&](const char* name,const std::function<void()>& run){try{run();++passed;std::cout<<"PASS "<<name<<'\n';}catch(const std::exception&error){++failed;std::cout<<"FAIL "<<name<<": "<<error.what()<<'\n';}};
    test("oversized_preflight",[]{MainWindow window(true);auto&project=window.addProject(blank(30000,30000));const auto before=project.document;bool message=false;Raster::resetMaterializationCount();drive(window,[&]{require(!window.findChild<QDialog*>("imageExportDialog"),"Oversized export opened renderer dialog");for(auto*box:window.findChildren<QMessageBox*>()){require(box->text().contains("100 megapixels"),"Oversized export error lost source limit");message=true;box->accept();}});require(message&&project.document==before&&!project.projectBusy&&project.history.undoCount()==0&&Raster::materializationCount()==0,"Oversized export changed project or allocated raster");});
    test("cancel_active_worker",[]{MainWindow window(true);auto&project=window.addProject(blank(10000,10000));const auto before=project.document;bool cancelled=false;int events=0;Raster::resetMaterializationCount();drive(window,[&]{auto*dialog=window.findChild<QDialog*>("imageExportDialog");if(!dialog)return;++events;require(project.projectBusy,"Project editable during export");auto*label=control<QLabel>(dialog,"exportEncodedSize");if(label->text().startsWith("Encoding")&&dialog->property("readyExportPath").toString().isEmpty()){cancelled=true;control<QDialogButtonBox>(dialog,"exportButtons")->button(QDialogButtonBox::Cancel)->click();}});require(cancelled&&events>2&&!project.projectBusy&&project.document==before&&project.history.undoCount()==0&&Raster::materializationCount()==0,"Worker cancellation changed project or blocked GUI");});
    test("supersede_and_cancel_ready",[]{MainWindow window(true);auto&project=window.addProject(blank(1000,650));const auto before=project.document;int state=0;bool ready=false;drive(window,[&]{auto*dialog=window.findChild<QDialog*>("imageExportDialog");if(!dialog)return;auto*buttons=control<QDialogButtonBox>(dialog,"exportButtons");if(state==0&&buttons->button(QDialogButtonBox::Save)->isEnabled()){auto old=dialog->property("readyExportGeneration").toULongLong();control<QComboBox>(dialog,"exportFormat")->setCurrentIndex(1);auto*quality=control<QSlider>(dialog,"exportQuality");require(quality->minimum()==0&&quality->maximum()==100,"JPEG source quality range changed");quality->setValue(10);quality->setValue(100);quality->setValue(0);require(!buttons->button(QDialogButtonBox::Save)->isEnabled(),"Stale encoded result remained saveable");dialog->setProperty("oldGeneration",old);state=1;}else if(state==1&&buttons->button(QDialogButtonBox::Save)->isEnabled()){require(dialog->property("readyExportGeneration").toULongLong()>dialog->property("oldGeneration").toULongLong(),"Superseded generation published");require(dialog->property("readyExportPath").toString().endsWith(".jpg"),"JPEG result did not replace PNG");require(!control<QLabel>(dialog,"exportPreview")->pixmap().isNull(),"Bounded encoded preview missing");ready=true;buttons->button(QDialogButtonBox::Cancel)->click();}});require(ready&&project.document==before&&!project.projectBusy&&project.history.undoCount()==0,"Cancel-ready mutated source/history");});
    test("exact_encoded_file_save",[]{QTemporaryDir output;const auto destination=output.filePath(QString::fromUtf8("export-画像.jpg"));MainWindow window(true);auto&project=window.addProject(blank(128,128));const auto before=project.document;QByteArray expected;bool selected=false,submitted=false;drive(window,[&]{auto*dialog=window.findChild<QDialog*>("imageExportDialog");if(!dialog||submitted)return;if(!selected){control<QComboBox>(dialog,"exportFormat")->setCurrentIndex(1);control<QSlider>(dialog,"exportQuality")->setValue(31);selected=true;}auto*save=control<QDialogButtonBox>(dialog,"exportButtons")->button(QDialogButtonBox::Save);if(save->isEnabled()){expected=read(dialog->property("readyExportPath").toString());submitted=true;window.setProperty("testNativeSavePath",destination);save->click();}});require(submitted&&!expected.isEmpty()&&read(destination)==expected,"Saved bytes differ from encoded preview file");require(QSettings().value("jpegExportQuality").toDouble()==.31,"JPEG quality was not retained on export");require(project.document==before&&!project.projectBusy&&project.history.undoCount()==0,"Export changed source/history");});
    test("jpeg_export_replaces_png_suffix",[]{QTemporaryDir output;const auto typed=output.filePath("photo.png");const auto expected=output.filePath("photo.jpg");MainWindow window(true);auto&project=window.addProject(blank(64,64));const auto before=project.document;QByteArray preview;bool submitted=false;drive(window,[&]{auto*dialog=window.findChild<QDialog*>("imageExportDialog");if(!dialog||submitted)return;control<QComboBox>(dialog,"exportFormat")->setCurrentIndex(1);auto*save=control<QDialogButtonBox>(dialog,"exportButtons")->button(QDialogButtonBox::Save);if(save->isEnabled()){preview=read(dialog->property("readyExportPath").toString());submitted=true;window.setProperty("testNativeSavePath",typed);save->click();}});require(submitted&&!preview.isEmpty()&&QFileInfo::exists(expected)&&read(expected)==preview,"JPEG export did not replace a PNG suffix");require(!QFileInfo::exists(typed),"JPEG export concatenated .jpg onto .png");require(project.document==before&&!project.projectBusy&&project.history.undoCount()==0,"Suffix replacement mutated the project");});
    test("pdf_export_scale_background_and_save",[]{
        // A 300 x 200 px canvas at 150 DPI is a 2 x 1.33 in page; 50% makes it 1 x 0.67 in, 150 x 100 px.
        QTemporaryDir output;const auto typed=output.filePath("sheet.png");const auto expected=output.filePath("sheet.pdf");
        auto document=blank(300,200);document.resolution=150;MainWindow window(true);auto&project=window.addProject(std::move(document));const auto before=project.document;
        QByteArray whiteFile,preview;int step=0;bool submitted=false;const QColor chosen(10,20,230);
        drive(window,[&]{
            auto*dialog=window.findChild<QDialog*>("imageExportDialog");if(!dialog||submitted)return;
            auto*format=control<QComboBox>(dialog,"exportFormat");auto*scale=control<QComboBox>(dialog,"exportScale");auto*matte=control<QPushButton>(dialog,"exportMatte");
            auto*page=control<QLabel>(dialog,"exportPageSize");auto*preview_=control<QLabel>(dialog,"exportPreview");auto*save=control<QDialogButtonBox>(dialog,"exportButtons")->button(QDialogButtonBox::Save);
            if(step==0){
                require(format->count()==3&&format->itemText(2)=="PDF"&&format->itemText(0)=="PNG"&&format->itemText(1)=="JPEG","Format order changed");
                require(!scale->isEnabled()&&!matte->isEnabled()&&page->isHidden(),"PNG offered PDF-only controls");
                format->setCurrentIndex(2);
                require(scale->isEnabled()&&matte->isEnabled()&&!page->isHidden()&&scale->currentText()=="100%","PDF controls not offered");
                require(page->text()=="2 × 1.33 in at 150 DPI (300 × 200 px)","Page size not shown at 100%");
                scale->setCurrentIndex(1);require(page->text()=="1 × 0.67 in at 150 DPI (150 × 100 px)","Page size ignored the scale");
                require(!save->isEnabled(),"Stale encoded result remained saveable");step=1;
            }else if(step==1&&save->isEnabled()){
                const auto ready=dialog->property("readyExportPath").toString();require(ready.endsWith(".pdf"),"PDF result did not replace PNG");
                whiteFile=read(ready);require(whiteFile.startsWith("%PDF-")&&whiteFile.contains("/MediaBox [0 0 72.0000 48.0000]"),"Preview file is not a half-scale one-page PDF");
                const auto shown=preview_->pixmap();require(!shown.isNull()&&shown.width()==150&&shown.height()==100&&shown.toImage().pixelColor(0,0)==QColor(Qt::white),"PDF preview is not the white-backed scaled page");
                // QColorDialog::getColor blocks in a nested loop where this timer is not re-entered, so a second timer accepts it.
                QTimer accept;accept.setInterval(10);
                QObject::connect(&accept,&QTimer::timeout,&window,[&]{for(auto*widget:QApplication::topLevelWidgets())if(auto*colors=qobject_cast<QColorDialog*>(widget)){accept.stop();colors->setCurrentColor(chosen);colors->accept();}});
                accept.start();matte->click();accept.stop();
                step=3;
            }else if(step==3&&save->isEnabled()){
                const auto ready=dialog->property("readyExportPath").toString();preview=read(ready);
                require(preview!=whiteFile&&preview.contains("/MediaBox [0 0 72.0000 48.0000]"),"Background change did not re-encode the PDF");
                require(preview_->pixmap().toImage().pixelColor(0,0)==chosen,"PDF preview did not show the chosen background");
                submitted=true;window.setProperty("testNativeSavePath",typed);save->click();
            }
        });
        require(submitted&&QFileInfo::exists(expected)&&read(expected)==preview,"Saved PDF differs from the encoded preview file");require(!QFileInfo::exists(typed),"PDF export kept the typed .png name");
        require(project.document==before&&!project.projectBusy&&project.history.undoCount()==0,"PDF export changed source/history");
    });
    test("pdf_export_cancel_keeps_destination",[]{
        QTemporaryDir output;const auto existing=output.filePath("existing.pdf");const QByteArray original("preserve original pdf");
        {QFile file(existing);require(file.open(QIODevice::WriteOnly),"Cannot create existing destination");file.write(original);}
        MainWindow window(true);auto&project=window.addProject(blank(200,120));const auto before=project.document;bool cancelled=false;
        drive(window,[&]{
            auto*dialog=window.findChild<QDialog*>("imageExportDialog");if(!dialog||cancelled)return;
            control<QComboBox>(dialog,"exportFormat")->setCurrentIndex(2);
            auto*buttons=control<QDialogButtonBox>(dialog,"exportButtons");
            if(buttons->button(QDialogButtonBox::Save)->isEnabled()&&dialog->property("readyExportPath").toString().endsWith(".pdf")){cancelled=true;buttons->button(QDialogButtonBox::Cancel)->click();}
        });
        require(cancelled&&read(existing)==original&&QDir(output.path()).entryList(QDir::Files).size()==1,"Cancelling a ready PDF export touched the destination");
        require(project.document==before&&!project.projectBusy&&project.history.undoCount()==0,"Cancelled PDF export changed source/history");
    });
    std::cout<<"Export UI "<<passed<<" passed, "<<failed<<" failed\n";return failed?1:0;
}

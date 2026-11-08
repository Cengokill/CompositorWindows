#include "ui/MainWindow.h"
#include "imaging/cmyk.h"
#include "../cmyk/Fixtures.h"
#include "../cmyk/ProfileFixture.h"
#include "imaging/wic_codec.h"
#include "imaging/hdr_codec.h"
#include "imaging/gif_codec.h"
#include <QApplication>
#include <QAction>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
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
    test("channel_pack_cancel",[]{MainWindow window(true);auto& project=window.addProject(blank(32,32));const auto before=project.document;int stage=0;drive(window,[&]{auto* dialog=window.findChild<QDialog*>("imageExportDialog");if(!dialog)return;auto* format=control<QComboBox>(dialog,"exportFormat");auto* buttons=control<QDialogButtonBox>(dialog,"exportButtons");if(stage==0){format->setCurrentIndex(format->findText("Channel Pack"));control<QComboBox>(dialog,"exportChannelRed")->setCurrentIndex(1);stage=1;}else if(stage==1&&buttons->button(QDialogButtonBox::Save)->isEnabled()){require(dialog->property("readyExportPath").toString().endsWith(".png"),"Channel Pack did not prepare PNG");stage=2;buttons->button(QDialogButtonBox::Cancel)->click();}});require(stage==2&&project.document==before&&!project.projectBusy&&project.history.undoCount()==0,"Channel Pack cancel changed project");});
    test("oversized_preflight",[]{MainWindow window(true);auto&project=window.addProject(blank(30000,30000));const auto before=project.document;bool message=false;Raster::resetMaterializationCount();drive(window,[&]{require(!window.findChild<QDialog*>("imageExportDialog"),"Oversized export opened renderer dialog");for(auto*box:window.findChildren<QMessageBox*>()){require(box->text().contains("100 megapixels"),"Oversized export error lost source limit");message=true;box->accept();}});require(message&&project.document==before&&!project.projectBusy&&project.history.undoCount()==0&&Raster::materializationCount()==0,"Oversized export changed project or allocated raster");});
    test("cancel_active_worker",[]{MainWindow window(true);auto&project=window.addProject(blank(10000,10000));const auto before=project.document;bool cancelled=false;int events=0;Raster::resetMaterializationCount();drive(window,[&]{auto*dialog=window.findChild<QDialog*>("imageExportDialog");if(!dialog)return;++events;require(project.projectBusy,"Project editable during export");auto*label=control<QLabel>(dialog,"exportEncodedSize");if(label->text().startsWith("Encoding")&&dialog->property("readyExportPath").toString().isEmpty()){cancelled=true;control<QDialogButtonBox>(dialog,"exportButtons")->button(QDialogButtonBox::Cancel)->click();}});require(cancelled&&events>2&&!project.projectBusy&&project.document==before&&project.history.undoCount()==0&&Raster::materializationCount()==0,"Worker cancellation changed project or blocked GUI");});
    test("supersede_and_cancel_ready",[]{MainWindow window(true);auto&project=window.addProject(blank(1000,650));const auto before=project.document;int state=0;bool ready=false;drive(window,[&]{auto*dialog=window.findChild<QDialog*>("imageExportDialog");if(!dialog)return;auto*buttons=control<QDialogButtonBox>(dialog,"exportButtons");if(state==0&&buttons->button(QDialogButtonBox::Save)->isEnabled()){auto old=dialog->property("readyExportGeneration").toULongLong();control<QComboBox>(dialog,"exportFormat")->setCurrentIndex(1);auto*quality=control<QSlider>(dialog,"exportQuality");require(quality->minimum()==0&&quality->maximum()==100,"JPEG source quality range changed");quality->setValue(10);quality->setValue(100);quality->setValue(0);require(!buttons->button(QDialogButtonBox::Save)->isEnabled(),"Stale encoded result remained saveable");dialog->setProperty("oldGeneration",old);state=1;}else if(state==1&&buttons->button(QDialogButtonBox::Save)->isEnabled()){require(dialog->property("readyExportGeneration").toULongLong()>dialog->property("oldGeneration").toULongLong(),"Superseded generation published");require(dialog->property("readyExportPath").toString().endsWith(".jpg"),"JPEG result did not replace PNG");require(!control<QLabel>(dialog,"exportPreview")->pixmap().isNull(),"Bounded encoded preview missing");ready=true;buttons->button(QDialogButtonBox::Cancel)->click();}});require(ready&&project.document==before&&!project.projectBusy&&project.history.undoCount()==0,"Cancel-ready mutated source/history");});
    test("exact_encoded_file_save",[]{QTemporaryDir output;const auto destination=output.filePath(QString::fromUtf8("export-画像.jpg"));MainWindow window(true);auto&project=window.addProject(blank(128,128));const auto before=project.document;QByteArray expected;bool selected=false,submitted=false;drive(window,[&]{auto*dialog=window.findChild<QDialog*>("imageExportDialog");if(!dialog||submitted)return;if(!selected){control<QComboBox>(dialog,"exportFormat")->setCurrentIndex(1);control<QSlider>(dialog,"exportQuality")->setValue(31);selected=true;}auto*save=control<QDialogButtonBox>(dialog,"exportButtons")->button(QDialogButtonBox::Save);if(save->isEnabled()){expected=read(dialog->property("readyExportPath").toString());submitted=true;window.setProperty("testNativeSavePath",destination);save->click();}});require(submitted&&!expected.isEmpty()&&read(destination)==expected,"Saved bytes differ from encoded preview file");require(QSettings().value("jpegExportQuality").toDouble()==.31,"JPEG quality was not retained on export");require(project.document==before&&!project.projectBusy&&project.history.undoCount()==0,"Export changed source/history");});
    test("gif_export_composites_and_preserves_timing",[]{
        QTemporaryDir output;const auto destination=output.filePath("animated.gif");auto d=blank(2,2);Layer animated;animated.id=newId();animated.transform={0,0,2,2};
        auto red=Raster::filled(2,2,{255,0,0,255}),blue=Raster::filled(2,2,{0,0,255,255});animated.raster=red;animated.animation=GifAnimation{{{red,70},{blue,130}},0};d.layers.push_back(animated);
        MainWindow window(true);auto& project=window.addProject(d);project.active=animated.id;const auto before=project.document;bool selected=false,saved=false;
        drive(window,[&]{auto*dialog=window.findChild<QDialog*>("imageExportDialog");if(!dialog||saved)return;auto*format=control<QComboBox>(dialog,"exportFormat");if(!selected){const auto index=format->findText("GIF");require(index>=0,"GIF export format missing");format->setCurrentIndex(index);selected=true;return;}auto*save=control<QDialogButtonBox>(dialog,"exportButtons")->button(QDialogButtonBox::Save);if(save->isEnabled()&&dialog->property("readyExportPath").toString().endsWith(".gif")){window.setProperty("testNativeSavePath",destination);saved=true;save->click();}});
        require(saved,"GIF export was not saved");auto decoded=imaging::decodeGif(std::filesystem::path(destination.toStdWString()));require(decoded.frames.size()==2&&decoded.frames[0].delayCentiseconds==7&&decoded.frames[1].delayCentiseconds==13,"GIF export frame timing changed");require(decoded.frames[0].image.pixels[0]==255&&decoded.frames[1].image.pixels[2]==255,"GIF output pixels changed");require(project.document==before&&!project.projectBusy&&project.history.undoCount()==0,"GIF export mutated project history");
    });
    test("gif_animated_preview",[]{
        auto d=blank(2,2);Layer animated;animated.id=newId();animated.transform={0,0,2,2};
        auto red=Raster::filled(2,2,{255,0,0,255}),blue=Raster::filled(2,2,{0,0,255,255});animated.raster=red;animated.animation=GifAnimation{{{red,80},{blue,120}},0};d.layers.push_back(animated);
        MainWindow window(true);auto& project=window.addProject(d);const auto before=project.document;int stage=0;bool finished=false;
        drive(window,[&]{auto*dialog=window.findChild<QDialog*>("imageExportDialog");if(!dialog||finished)return;auto*format=control<QComboBox>(dialog,"exportFormat");auto*play=control<QPushButton>(dialog,"exportAnimatedPreview");
            if(stage==0){require(!play->isVisible(),"GIF preview visible for PNG");format->setCurrentIndex(format->findText("GIF"));stage=1;}
            else if(stage==1&&play->isEnabled()){require(play->isVisible()&&dialog->property("animatedPreviewFrame").toInt()==0,"GIF preview not ready");play->click();require(play->text()=="Pause","GIF preview did not start");stage=2;}
            else if(stage==2&&dialog->property("animatedPreviewFrame").toInt()==1){auto*label=control<QLabel>(dialog,"exportPreview");require(!label->pixmap().isNull()&&label->pixmap().toImage().pixelColor(0,0).blue()>200,"Animated frame not displayed");play->click();require(play->text()=="Play","GIF preview did not pause");const auto width=label->pixmap().width();control<QSlider>(dialog,"exportPreviewZoom")->setValue(50);require(label->pixmap().width()<width,"Preview zoom did not update paused frame");format->setCurrentIndex(0);require(!play->isVisible()&&!play->isEnabled(),"GIF playback remained available after switching format");stage=3;}
            else if(stage==3){finished=true;control<QDialogButtonBox>(dialog,"exportButtons")->button(QDialogButtonBox::Cancel)->click();}
        });require(finished&&project.document==before&&!project.projectBusy&&project.history.undoCount()==0,"Animated preview changed document or history");
    });
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
                require(format->count()==6&&format->itemText(5)=="Channel Pack"&&format->itemText(2)=="PDF"&&format->itemText(0)=="PNG"&&format->itemText(1)=="JPEG"&&format->itemText(3)=="TIFF"&&format->itemText(4)=="OpenEXR","Expected export formats in order");
                auto*quality=control<QSlider>(dialog,"exportQuality");
                require(!scale->isEnabled()&&!matte->isEnabled()&&page->isHidden()&&scale->isHidden()&&quality->isHidden(),"PNG offered PDF-only or JPEG-only controls");
                format->setCurrentIndex(2);
                require(scale->isEnabled()&&matte->isEnabled()&&!page->isHidden()&&!scale->isHidden()&&quality->isHidden()&&scale->currentText()=="100%","PDF controls not offered");
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
    test("cmyk_export_generation_save_and_cancel",[]{
        cmyk_test::ProfileFile fixture;const auto profile=imaging::readCmykProfile(std::filesystem::path(fixture.path.toStdWString()));
        QTemporaryDir output;MainWindow window(true);auto d=blank(64,32);d.cmykProfile=profile;auto& project=window.addProject(d);const auto before=project.document;
        QByteArray expected;const auto destination=output.filePath("CMYK.tif");int stage=0;
        drive(window,[&]{auto* dialog=window.findChild<QDialog*>("imageExportDialog");if(!dialog)return;auto* save=control<QDialogButtonBox>(dialog,"exportButtons")->button(QDialogButtonBox::Save);
            if(stage==0){auto* mode=control<QComboBox>(dialog,"exportColorMode");require(!mode->isEnabled()&&mode->currentIndex()==0,"PNG did not default to RGB");control<QComboBox>(dialog,"exportFormat")->setCurrentIndex(3);mode->setCurrentIndex(1);require(!save->isEnabled(),"RGB preview remained saveable after CMYK choice");stage=1;}
            else if(stage==1&&save->isEnabled()){expected=read(dialog->property("readyExportPath").toString());window.setProperty("testNativeSavePath",destination);stage=2;save->click();}
        });
        require(stage==2&&read(destination)==expected,"CMYK Save did not use prepared bytes");require(imaging::WicCodec::sourceCmykProfile(std::filesystem::path(destination.toStdWString()))->sha256==profile->sha256,"TIFF export lost CMYK profile");
        bool cancelled=false;drive(window,[&]{auto* dialog=window.findChild<QDialog*>("imageExportDialog");if(!dialog)return;control<QComboBox>(dialog,"exportFormat")->setCurrentIndex(1);control<QComboBox>(dialog,"exportColorMode")->setCurrentIndex(1);auto* buttons=control<QDialogButtonBox>(dialog,"exportButtons");if(buttons->button(QDialogButtonBox::Save)->isEnabled()){cancelled=true;buttons->button(QDialogButtonBox::Cancel)->click();}});
        require(cancelled&&project.document==before&&project.history.undoCount()==0&&!project.projectBusy&&read(destination)==expected,"Cancelled CMYK export changed document/destination");
    });
    test("cmyk_profile_menu_undo_noop_and_preference",[]{
        MainWindow window(true);auto& project=window.addProject(blank(64,32));auto* load=window.findChild<QAction*>("viewCmykProfile");auto* proof=window.findChild<QAction*>("viewCmykProof");require(load&&proof,"CMYK menu commands missing");
        cmyk_test::ProfileFile fixture;const auto file=fixture.path;
        auto choose=[&](bool cancel){window.setProperty("testCmykProfileCancel",cancel);window.setProperty("testCmykProfilePath",cancel?QString{}:file);load->trigger();window.setProperty("testCmykProfileCancel",false);window.setProperty("testCmykProfilePath",QString{});};
        choose(true);require(!project.document->cmykProfile&&project.history.undoCount()==0,"Profile picker Cancel changed history");choose(false);require(project.document->cmykProfile&&project.history.undoCount()==1,"Profile menu did not make one edit");
        const auto profile=project.document->cmykProfile;choose(false);require(project.history.undoCount()==1,"Same ICC profile created a no-op edit");
        proof->trigger();require(project.proofColors&&project.history.undoCount()==1,"Proof toggle changed history");proof->trigger();require(!project.proofColors,"Proof toggle did not restore normal view");
        project.document=project.history.undo()->document;require(!project.document->cmykProfile,"Undo lost original profile state");project.document=project.history.redo()->document;require(project.document->cmykProfile==profile,"Redo lost selected profile");
        const auto stored=QSettings().value("cmyk/fallbackBytes").toByteArray();require(stored.size()==qsizetype(profile->bytes.size()),"Accepted fallback preference missing");
        QTemporaryDir source;const auto unprofiled=source.filePath("fallback.psd");const auto bytes=cmyk_test::psd();{QFile file(unprofiled);require(file.open(QIODevice::WriteOnly),"Write fallback fixture");file.write(reinterpret_cast<const char*>(bytes.data()),qint64(bytes.size()));}
        MainWindow reopened(true);bool prompted=false;QTimer answer;answer.setInterval(10);QObject::connect(&answer,&QTimer::timeout,&reopened,[&]{if(auto* message=reopened.findChild<QMessageBox*>("missingCmykProfile")){prompted=true;message->done(QMessageBox::Cancel);}});answer.start();reopened.openPath(unprofiled);answer.stop();auto* name=reopened.findChild<QAction*>("viewCmykProfileName");require(!prompted&&name&&name->text().contains(QString::fromStdString(profile->name)),"Saved fallback was not restored in a new window import");
    });
    test("exr_export_preserves_highlights_and_corrects_suffix",[]{
        QTemporaryDir output;MainWindow window(true);auto d=blank(8,4);Layer layer;layer.id=newId();layer.name="HDR";layer.transform={0,0,8,4};auto pixels=makeDeep(8,4);for(int y=0;y<4;++y)for(int x=0;x<8;++x)pixels->setPixel(x,y,{2,1,.5f,1});layer.deep=pixels;layer.raster=rasterFromDeep(*pixels);d.layers={layer};auto& project=window.addProject(d);const auto before=project.document;
        window.setProperty("testNativeSavePath",output.filePath("bright.png"));bool saved=false;
        drive(window,[&]{auto* dialog=window.findChild<QDialog*>("imageExportDialog");if(!dialog||saved)return;control<QComboBox>(dialog,"exportFormat")->setCurrentIndex(4);require(!control<QSlider>(dialog,"exportQuality")->isEnabled()&&!control<QPushButton>(dialog,"exportMatte")->isEnabled(),"EXR enabled SDR quality/matte controls");auto* buttons=control<QDialogButtonBox>(dialog,"exportButtons");if(buttons->button(QDialogButtonBox::Save)->isEnabled()&&dialog->property("readyExportPath").toString().endsWith(".exr")){saved=true;buttons->button(QDialogButtonBox::Save)->click();}});
        auto result=imaging::decodeExr(std::filesystem::path(output.filePath("bright.exr").toStdWString()));require(saved&&result->pixel(7,0)[0]>1.99f&&!QFile::exists(output.filePath("bright.png")),"EXR export lost highlights or used wrong suffix");require(project.document==before&&project.history.undoCount()==0&&!project.projectBusy,"EXR export mutated source/history");
    });
    std::cout<<"Export UI "<<passed<<" passed, "<<failed<<" failed\n";return failed?1:0;
}

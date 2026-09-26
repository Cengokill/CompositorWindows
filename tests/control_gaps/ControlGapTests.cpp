#include "ui/MainWindow.h"
#include "ui/PaletteDialog.h"
#include "imaging/wic_codec.h"
#include <QApplication>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QInputDialog>
#include <QPushButton>
#include <QTimer>
#include <QEventLoop>
#include <filesystem>
#include <iostream>
#include <map>

using namespace compositor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void trigger(MainWindow& window,const char* id){for(auto* action:window.findChildren<QAction*>())if(action->property("commandId")==id){require(action->isEnabled(),"source command enabled");action->trigger();QApplication::processEvents();return;}throw std::runtime_error(std::string("missing command ")+id);}
Document document(const char* name="Pixels"){Document d;d.id=newId();d.width=40;d.height=20;Layer l;l.id=newId();l.name=name;l.transform={0,0,40,20};l.raster=Raster::filled(40,20,{100,50,25,255});d.layers.push_back(l);return d;}
QTabWidget* tabs(MainWindow& window){auto* result=window.findChild<QTabWidget*>();require(result,"project tabs");return result;}
PaletteDialog* picker(MainWindow& window){for(auto* widget:window.findChildren<QWidget*>())if(auto* dialog=dynamic_cast<PaletteDialog*>(widget);dialog&&dialog->isVisible())return dialog;throw std::runtime_error("visible palette picker");}
std::string color(MainWindow& window){return picker(window)->color().hex();}
void sample(MainWindow& window,const char* hex){const auto rgb=effects_tools::PaletteColor::fromHex(hex);require(rgb.has_value(),"fixture color");picker(window)->sample(*rgb);}
void closePicker(MainWindow& window,bool accept){if(accept)picker(window)->accept();else picker(window)->reject();QApplication::processEvents();}

void palette_retarget(bool accept){
 MainWindow window(true);auto& project=window.addProject(document());window.show();QApplication::processEvents();const auto before=project.document;
 trigger(window,"palette.foreground");require(color(window)=="000000","initial foreground black");picker(window)->move(120,140);QApplication::processEvents();const auto position=picker(window)->pos();sample(window,"FF0000");
 trigger(window,"palette.background");require(picker(window)->pos()==position,"retarget preserves picker position");require(picker(window)->windowTitle()=="Color Picker (Background Color)","retarget changes picker target title");require(color(window)=="FFFFFF","retarget loads original background rather than foreground draft");sample(window,"0000FF");closePicker(window,accept);
 trigger(window,"palette.foreground");require(color(window)=="000000","abandoned foreground draft is not committed");require(picker(window)->pos()==position,"reopen retains picker position");closePicker(window,false);
 trigger(window,"palette.background");require(color(window)==(accept?"0000FF":"FFFFFF"),"only accepted current target commits");closePicker(window,false);require(project.document==before&&project.history.undoCount()==0,"palette edit leaves document/history unchanged");
}
void palette_retarget_apply(){palette_retarget(true);}
void palette_retarget_cancel(){palette_retarget(false);}
void palette_same_target_reset(){MainWindow window(true);auto& project=window.addProject(document());window.show();trigger(window,"palette.foreground");picker(window)->move(130,150);const auto position=picker(window)->pos();sample(window,"FF0000");trigger(window,"palette.foreground");require(picker(window)->pos()==position&&color(window)=="000000","opening same swatch replaces working state with stored palette color");closePicker(window,false);require(project.history.undoCount()==0,"reopening picker is not an image edit");}

void copyCase(bool collision,bool cancel){
 MainWindow window(true);auto& source=window.addProject(document("Source"),"Source");auto& first=window.addProject(document("First"),collision?"New Project":"Same title");auto& second=window.addProject(document("Second"),"Same title");window.show();QApplication::processEvents();tabs(window)->setCurrentWidget(source.page);QApplication::processEvents();
 const auto original=source.document,firstBefore=first.document,secondBefore=second.document;const auto tabCount=tabs(window)->count();bool visited=false;std::exception_ptr failure;
 QTimer::singleShot(0,&window,[&]{auto* dialog=qobject_cast<QInputDialog*>(QApplication::activeModalWidget());if(!dialog)return;try{visited=true;auto* combo=dialog->findChild<QComboBox*>();require(combo&&combo->count()==3,"new project and two current destinations");combo->setCurrentIndex(collision?1:2);if(cancel)dialog->reject();else dialog->accept();}catch(...){failure=std::current_exception();dialog->reject();}});
 trigger(window,"layer.copy_project");require(visited,"actual destination dialog opened");if(failure)std::rethrow_exception(failure);require(source.document==original&&source.history.undoCount()==0,"copy preserves source document/history");
 if(cancel){require(tabs(window)->count()==tabCount&&first.document==firstBefore&&second.document==secondBefore&&first.history.undoCount()==0&&second.history.undoCount()==0,"destination Cancel changes no project/history");require(tabs(window)->currentWidget()==source.page,"Cancel preserves current project");return;}
 auto& target=collision?first:second;auto& other=collision?second:first;const auto& otherBefore=collision?secondBefore:firstBefore;require(tabs(window)->count()==tabCount,"existing New Project title is not the new-project sentinel");require(target.document->layers.size()==2&&other.document==otherBefore,"selected destination identity receives copy despite equal labels");require(target.history.undoCount()==1&&other.history.undoCount()==0,"only selected destination has one undo entry");require(tabs(window)->currentWidget()==target.page,"selected destination becomes current");const auto& copied=target.document->layers.back();require(copied.id!=source.active&&copied.raster==source.document->layers.front().raster,"copied layer remaps ID and shares immutable raster");
 trigger(window,"edit.undo");require(target.document==(collision?firstBefore:secondBefore),"Undo restores exact chosen destination");
}
void copy_duplicate_title(){copyCase(false,false);}
void copy_new_title_collision(){copyCase(true,false);}
void copy_cancel(){copyCase(false,true);}

void backgroundCase(bool existing,bool cancel){
 MainWindow window(true);auto input=document();const auto root=std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();const auto image=imaging::WicCodec::decode(root/L"evidence/imaging/astronaut.png").image;auto& layer=input.layers.front();layer.raster=Raster::fromRgba(int(image.width),int(image.height),image.pixels.data(),image.stride);
 if(existing){auto mask=std::make_shared<GrayRaster>();mask->width=mask->height=1;mask->pixels={255};layer.mask=Mask{mask,false,false,Transform{3,4,30,12}};}
 auto& project=window.addProject(input);window.show();trigger(window,"tool.move");require(!project.maskSelected,"before targets image");const auto before=project.document;const auto oldMask=before->layers.front().mask;bool visited=false,applied=false;std::exception_ptr failure;QTimer poll;poll.setInterval(20);QElapsedTimer elapsed;elapsed.start();
 QEventLoop loop;bool finished=false;
 QObject::connect(&poll,&QTimer::timeout,&window,[&]{QPointer<QDialog> dialog;for(auto* candidate:window.findChildren<QDialog*>())if(candidate->isVisible()&&candidate->windowTitle()=="Remove Background"){dialog=candidate;break;}if(!dialog)dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());try{if(elapsed.elapsed()>60000)throw std::runtime_error("real subject Apply exceeded frozen60-second deadline");if(!dialog)return;if(!visited){QObject::connect(dialog,&QDialog::finished,&window,[&](int){finished=true;loop.quit();});visited=true;}if(cancel){poll.stop();dialog->reject();return;}auto* apply=dialog->findChild<QPushButton*>("applySubjectMask");require(apply,"actual SubjectDialog Apply");if(!applied&&apply->isEnabled()){applied=true;apply->click();}}catch(...){failure=std::current_exception();poll.stop();if(dialog)dialog->reject();loop.quit();}});
 poll.start();trigger(window,"filter.subject");if(!finished&&!failure)loop.exec();poll.stop();require(visited,"actual subject dialog opened");if(failure)std::rethrow_exception(failure);
 if(cancel){require(project.document==before&&project.history.undoCount()==0&&!project.maskSelected,"subject Cancel preserves exact document/history/edit target");return;}
 require(applied&&project.document->layers.front().mask.has_value(),"real CPU provider commits a mask");require(project.maskSelected,"source Filters469 selects newly committed mask target");require(project.history.undoCount()==1&&project.history.undoName()=="Remove Background","subject Apply commits one history step");const auto& output=project.document->layers.front();require(output.raster==before->layers.front().raster&&output.transform==before->layers.front().transform,"subject Apply preserves immutable image and transform");require(output.mask->enabled,"subject Apply enables mask");if(oldMask)require(output.mask->linked==oldMask->linked&&output.mask->placement==oldMask->placement,"replacement preserves existing mask placement/link metadata");require(output.mask->raster->width==int(image.width)&&output.mask->raster->height==int(image.height),"committed mask has original source resolution");
 const auto after=project.document;trigger(window,"edit.undo");require(project.document==before,"subject Undo restores exact pre-edit document");trigger(window,"edit.redo");require(project.document==after,"subject Redo restores exact committed mask bytes");
}
void background_mask_target(){backgroundCase(false,false);}
void background_existing_target(){backgroundCase(true,false);}
void background_cancel(){backgroundCase(true,true);}
}
int main(int argc,char** argv){QApplication app(argc,argv);const std::map<std::string,void(*)()> cases{{"palette_retarget_apply",palette_retarget_apply},{"palette_retarget_cancel",palette_retarget_cancel},{"palette_same_target_reset",palette_same_target_reset},{"copy_duplicate_title",copy_duplicate_title},{"copy_new_title_collision",copy_new_title_collision},{"copy_cancel",copy_cancel},{"background_mask_target",background_mask_target},{"background_existing_target",background_existing_target},{"background_cancel",background_cancel}};try{require(argc==2&&cases.contains(argv[1]),"provide named case");cases.at(argv[1])();std::cout<<"PASS "<<argv[1]<<'\n';return 0;}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}

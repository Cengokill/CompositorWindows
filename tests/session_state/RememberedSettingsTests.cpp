#include "ui/MainWindow.h"
#include "ui/EditPanelSession.h"
#include <QEventLoop>
#include <QApplication>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QInputDialog>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTimer>
#include <iostream>
#include <map>
#include <stdexcept>
using namespace compositor;
namespace {
void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
template<class T>T* field(QObject& root,const char* name){if(auto* c=root.findChild<T*>(name))return c;for(auto* c:root.findChildren<T*>())if(c->accessibleName()==name)return c;throw std::runtime_error(std::string("missing control ")+name);}
QAction* command(MainWindow& w,const char* id){for(auto* a:w.findChildren<QAction*>())if(a->property("commandId").toString()==id)return a;throw std::runtime_error(std::string("missing command ")+id);}
Document document(){Document d;d.id=newId();d.width=40;d.height=20;Layer l;l.id=newId();l.transform={0,0,40,20};std::vector<Pixel> pixels(800,Pixel{128,128,128,255});l.raster=Raster::fromRgba(40,20,reinterpret_cast<const uint8_t*>(pixels.data()),160);d.layers.push_back(l);return d;}
struct Fixture {
 MainWindow window{true};EditorProject* a;EditorProject* b;
 Fixture(){a=&window.addProject(document(),"A");b=&window.addProject(document(),"B");select(a);}
 void select(EditorProject* p){window.findChild<QTabWidget*>()->setCurrentWidget(p->page);require(window.findChild<QTabWidget*>()->currentWidget()==p->page,"tab switch accepted");}
 void trigger(const char* id){auto* action=command(window,id);require(action->isEnabled(),"command must be enabled");action->trigger();}
 void modal(const char* id,std::function<void(QDialog&)> configure,bool apply=false){
  std::exception_ptr failure;bool visited=false,finished=false,submitted=false;QPointer<QDialog> panel;
  QMetaObject::Connection completion;QEventLoop wait;QTimer poll;poll.setInterval(5);QElapsedTimer elapsed;elapsed.start();
  auto owned=[&](QObject* value){for(auto* current=value;current;current=current->parent())if(current==&window)return true;return false;};
  auto discover=[&]()->QDialog*{
   for(auto* object:window.findChildren<QObject*>())if(auto* session=dynamic_cast<ui::EditPanelSession*>(object))
    if(session->canvas()==window.canvas()&&session->panel()&&session->panel()->isVisible())return session->panel();
   auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());return dialog&&owned(dialog)?dialog:nullptr;
  };
  QObject::connect(&poll,&QTimer::timeout,&window,[&]{
   try{
    if(elapsed.elapsed()>15000)throw std::runtime_error("Panel did not finish within frozen 15-second limit");
    auto* dialog=panel?panel.data():discover();if(!dialog)return;
    if(!visited){visited=true;panel=dialog;completion=QObject::connect(dialog,&QDialog::finished,&wait,[&]{finished=true;wait.quit();});configure(*dialog);if(!apply){dialog->reject();return;}}
    auto* box=dialog->findChild<QDialogButtonBox*>();
    if(!submitted&&box&&box->button(QDialogButtonBox::Apply)&&box->button(QDialogButtonBox::Apply)->isEnabled()){submitted=true;box->button(QDialogButtonBox::Apply)->click();}
   }catch(...){failure=std::current_exception();poll.stop();if(panel)panel->reject();wait.quit();}
  });
  poll.start();trigger(id);if(!finished&&!failure)wait.exec();poll.stop();QObject::disconnect(completion);
  if(failure)std::rethrow_exception(failure);require(visited,"actual owned panel opened");require(finished,"actual panel finished before assertions");
 }
};
void lasso_return(){Fixture f;f.trigger("tool.polygon");f.trigger("tool.move");f.trigger("tool.lasso");auto* rail=f.window.findChild<QToolBar*>("tools");bool polygon=false;for(auto* a:rail->actions())if(a->property("editorTool").toInt()==int(ProjectTool::Polygon)&&a->isChecked())polygon=true;std::cout<<"returned_polygon="<<polygon<<'\n';require(polygon,"EditorSession.lassoKind survives leaving Lasso; pressing L selects remembered Polygonal");}
void crop_ratio(){Fixture f;const auto before=f.a->document;f.trigger("tool.crop");auto* ratio=field<QComboBox>(f.window,"cropRatioChoice");require(ratio->count()==5&&ratio->currentText()=="Free","Crop source five ratios, new entry Free");ratio->setCurrentText("4:3");f.trigger("tool.crop");require(ratio->currentText()=="4:3","same Crop with draft retains ratio");f.trigger("tool.move");f.select(f.b);require(ratio->currentText()=="Free","other project crop default");f.select(f.a);require(ratio->currentText()=="4:3","tab restore retains remembered field before Crop entry");f.trigger("tool.crop");require(ratio->currentText()=="Free","fresh Crop draft resets Free");require(f.a->document==before&&f.a->history.undoCount()==0,"ratio choices do not mutate document or history");}
void amounts_independent(){Fixture f;f.trigger("selection.all");auto amount=[&](const char* id,double expected,std::optional<double> next){bool visited=false;QTimer::singleShot(0,&f.window,[&]{auto* dialog=f.window.findChild<QInputDialog*>();if(!dialog)return;visited=true;std::cout<<id<<" initial="<<dialog->doubleValue()<<'\n';const bool matches=dialog->doubleValue()==expected;dialog->setProperty("expectedInitial",matches);if(next&&matches){dialog->setDoubleValue(*next);dialog->accept();}else dialog->reject();});f.trigger(id);require(visited,"amount dialog visited");};
 amount("selection.expand",1,4);double actual=-1;f.modal("selection.expand",[&](QDialog& d){actual=static_cast<QInputDialog&>(d).doubleValue();});std::cout<<"remembered_expand="<<actual<<'\n';require(actual==4,"accepted Expand amount is remembered");f.modal("selection.contract",[&](QDialog& d){require(static_cast<QInputDialog&>(d).doubleValue()==1,"Contract independent default1");});
}
void pixel_apply_cancel(){Fixture f;f.modal("filter.gaussian",[](QDialog& d){field<QDoubleSpinBox>(d,"Radius")->setValue(3);},true);require(f.a->history.undoCount()==1,"filter Apply one history step");double value=0;f.modal("filter.gaussian",[&](QDialog& d){value=field<QDoubleSpinBox>(d,"Radius")->value();field<QDoubleSpinBox>(d,"Radius")->setValue(7);});std::cout<<"remembered_radius="<<value<<'\n';require(value==3,"FilterTests22 remembers radius3 on Apply");f.modal("filter.gaussian",[&](QDialog& d){require(field<QDoubleSpinBox>(d,"Radius")->value()==3,"Cancel retains prior applied radius");});f.select(f.b);f.modal("filter.gaussian",[&](QDialog& d){require(field<QDoubleSpinBox>(d,"Radius")->value()==1,"other project radius1");});f.select(f.a);f.modal("filter.gaussian",[&](QDialog& d){require(field<QDoubleSpinBox>(d,"Radius")->value()==3,"return to prior applied radius");});}
void adjustment_apply_cancel(){Fixture f;f.modal("adjust.exposure",[](QDialog& d){field<QDoubleSpinBox>(d,"Exposure")->setValue(1.25);},true);double value=0;f.modal("adjust.exposure",[&](QDialog& d){value=field<QDoubleSpinBox>(d,"Exposure")->value();field<QDoubleSpinBox>(d,"Exposure")->setValue(2.5);});std::cout<<"remembered_exposure="<<value<<'\n';require(value==1.25,"destructive Exposure Apply remembered");f.modal("adjust.exposure",[](QDialog& d){require(field<QDoubleSpinBox>(d,"Exposure")->value()==1.25,"cancel retains exposure");});f.modal("adjust.new.exposure",[](QDialog& d){require(field<QDoubleSpinBox>(d,"Exposure")->value()==0,"new live adjustment defaults independently");});f.select(f.b);f.modal("adjust.exposure",[](QDialog& d){require(field<QDoubleSpinBox>(d,"Exposure")->value()==0,"other project exposure default");});}
void control_tab_isolation(){Fixture f;f.trigger("selection.all");auto* lasso=field<QComboBox>(f.window,"lassoKind");auto* expand=field<QSpinBox>(f.window,"selectionExpandAmount");auto* contract=field<QSpinBox>(f.window,"selectionContractAmount");require(lasso->currentIndex()==0&&expand->value()==1&&contract->value()==1,"source remembered defaults");require(expand->minimum()==1&&expand->maximum()==500&&contract->minimum()==1&&contract->maximum()==500,"both ranges1..500");lasso->setCurrentIndex(1);expand->setValue(12);contract->setValue(8);f.trigger("tool.move");f.select(f.b);require(lasso->currentIndex()==0&&expand->value()==1&&contract->value()==1,"fresh project control defaults");QSignalSpy l(lasso,&QComboBox::currentIndexChanged),e(expand,&QSpinBox::valueChanged),c(contract,&QSpinBox::valueChanged);f.select(f.a);require(lasso->currentIndex()==1&&expand->value()==12&&contract->value()==8,"restore each remembered control");require(l.empty()&&e.empty()&&c.empty(),"tab restoration does not emit user edits");require(f.a->history.undoCount()==1&&f.b->history.undoCount()==0,"only SelectAll changes history");}
}
int main(int argc,char** argv){QApplication app(argc,argv);const std::map<std::string,void(*)()> cases{{"lasso_return",lasso_return},{"crop_ratio",crop_ratio},{"amounts_independent",amounts_independent},{"pixel_apply_cancel",pixel_apply_cancel},{"adjustment_apply_cancel",adjustment_apply_cancel},{"control_tab_isolation",control_tab_isolation}};try{require(argc==2&&cases.contains(argv[1]),"provide named case");cases.at(argv[1])();std::cout<<"PASS "<<argv[1]<<'\n';return 0;}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}


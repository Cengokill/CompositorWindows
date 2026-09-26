#include "ui/ProjectChrome.h"
#include <QApplication>
#include <QAccessible>
#include <QDockWidget>
#include <QMainWindow>
#include <QTabBar>
#include <QTabWidget>
#include <QTreeWidget>
#include <iostream>
#include <stdexcept>
namespace {void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}}
int main(int argc,char** argv){QApplication app(argc,argv);if(argc!=2)return 2;const QString key=QString::fromLocal8Bit(argv[1]);try{
    QAccessible::Id remembered{};
    {QMainWindow window;auto* tabs=new QTabWidget;tabs->setTabsClosable(true);window.setCentralWidget(tabs);auto* dock=new QDockWidget;auto* tree=new QTreeWidget;dock->setWidget(tree);window.addDockWidget(Qt::RightDockWidgetArea,dock);
    auto* a=new QWidget;auto* b=new QWidget;auto* c=new QWidget;tabs->addTab(a,"Alpha *");tabs->addTab(b,"Beta & Literal");tabs->addTab(c,"Gamma");
    compositor::ui::installProjectChrome(&window,tabs,dock,tree);compositor::ui::refreshProjectTabs(tabs,true,{"Alpha","Beta & Literal","Gamma"});
    auto* bar=QAccessible::queryAccessibleInterface(tabs->tabBar());require(bar&&bar->role()==QAccessible::PageTabList&&bar->text(QAccessible::Name)=="Project tabs","named tab container missing");
    auto* selection=bar->selectionInterface();require(selection&&selection->selectedItemCount()==1,"single selection interface missing");auto* beta=bar->child(1);remembered=QAccessible::uniqueId(beta);
    require(beta->role()==QAccessible::PageTab&&beta->text(QAccessible::Name)=="Beta & Literal","tab title changed or incorrectly interpreted mnemonic");
    if(key=="selection"){
        require(selection->select(beta)&&tabs->currentWidget()==b,"accessible selection did not select actual page");require(selection->selectedItemCount()==1&&selection->selectedItem(0)==beta&&beta->state().selected,"selected provider state incorrect");
        require(!selection->clear()&&!selection->selectAll()&&!selection->unselect(beta),"single-selection tab list accepted invalid deselection");
        require(tabs->tabText(0)=="Alpha *"&&tabs->tabText(1)=="Beta & Literal","provider mutated visual tab labels");
    }else if(key=="pending_selection"){
        compositor::ui::refreshProjectTabs(tabs,false,{"Alpha","Beta & Literal","Gamma"});require(!bar->child(0)->state().disabled&&beta->state().disabled,"pending switch policy disagrees with source active-tab exception");
        require(!selection->select(beta)&&tabs->currentWidget()==a,"pending selection switched page");beta->actionInterface()->doAction(QAccessibleActionInterface::pressAction());require(tabs->currentWidget()==a,"pending Invoke switched page");
        compositor::ui::refreshProjectTabs(tabs,true,{"Alpha","Beta & Literal","Gamma"});require(selection->select(beta)&&tabs->currentWidget()==b,"selection did not resume after cancellation");
    }else if(key=="identity_lifetime"){
        tabs->removeTab(0);compositor::ui::refreshProjectTabs(tabs,true,{"Beta & Literal","Gamma"});require(QAccessible::accessibleInterface(remembered)==bar->child(0),"page identity changed after earlier tab removed");
        auto* retained=QAccessible::accessibleInterface(remembered);require(retained&&selection->select(retained)&&tabs->currentWidget()==b,"retained provider selected reindexed sibling");
        tabs->removeTab(0);compositor::ui::refreshProjectTabs(tabs,true,{"Gamma"});retained=QAccessible::accessibleInterface(remembered);require(!retained||!retained->isValid(),"removed page retained valid provider");
        delete a;delete b;
    }else throw std::runtime_error("unknown provider case");
    }
    if(key=="identity_lifetime")require(QAccessible::accessibleInterface(remembered)==nullptr,"registered virtual child survived owner teardown");
    std::cout<<"PASS project_tab_provider."<<key.toStdString()<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}

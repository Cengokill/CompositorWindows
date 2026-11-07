#include "ui/MainWindow.h"
#include <QApplication>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QToolBar>
#include <QToolButton>
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace compositor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
QToolButton* tool(MainWindow& window,const char* id){
    for(auto* button:window.findChildren<QToolButton*>())
        if(button->defaultAction()&&button->defaultAction()->property("commandId")==id)return button;
    throw std::runtime_error("Tool button missing");
}
void waitWidth(QToolBar* bar,int width){
    require(QTest::qWaitFor([=]{return bar->width()==width;},std::chrono::seconds(3)),"Rail did not change columns");
}
}
int main(int argc,char** argv){
    QApplication app(argc,argv);
    QTemporaryDir settings;
    QCoreApplication::setOrganizationName("CompositorFixture");
    QCoreApplication::setApplicationName("ResponsiveToolRail");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    try{
        MainWindow window(true);
        Document document;document.id=newId();document.width=64;document.height=64;
        auto& project=window.addProject(document);
        auto* bar=window.findChild<QToolBar*>("tools");
        auto* rail=window.findChild<QScrollArea*>("responsiveToolRail");
        require(bar&&rail,"Rail missing");
        window.resize(1500,1100);window.show();waitWidth(bar,56);
        auto* move=tool(window,"tool.move");auto* marquee=tool(window,"tool.marquee");
        auto* brush=tool(window,"tool.brush");
        require(move->y()<marquee->y(),"Tall rail is not one column");
        QTest::mouseClick(brush,Qt::LeftButton);
        require(brush->defaultAction()->isChecked(),"Brush not selected");
        window.resize(1500,600);waitWidth(bar,90);
        require(move->y()==marquee->y()&&move->x()<marquee->x(),"Compact rail order incorrect");
        require(brush->defaultAction()->isChecked(),"Selection lost on resize");
        require(project.history.undoCount()==0,"Resize changed history");
        window.resize(1500,1100);waitWidth(bar,56);
        require(move->y()<marquee->y()&&brush->defaultAction()->isChecked(),"Tall layout did not restore");
        rail->setFixedHeight(120);waitWidth(bar,90);
        require(QTest::qWaitFor([&]{return rail->verticalScrollBar()->maximum()>0;},std::chrono::seconds(3)),"Compact rail does not scroll");
        rail->verticalScrollBar()->setValue(rail->verticalScrollBar()->maximum());
        auto* palette=window.findChild<QWidget*>("paletteSwatches");
        require(palette&&palette->isVisible()&&!palette->visibleRegion().isEmpty(),"Palette unreachable when scrolled");
        std::cout<<"PASS responsive tool rail\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}

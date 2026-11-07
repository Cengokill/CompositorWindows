#include "ui/MainWindow.h"
#include <QApplication>
#include <QRegion>
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
bool fullyVisible(QWidget* widget){return QRegion(widget->rect()).subtracted(widget->visibleRegion()).isEmpty();}
QToolButton* tool(MainWindow& window,const char* id){
    for(auto* button:window.findChildren<QToolButton*>())
        if(button->defaultAction()&&button->defaultAction()->property("commandId")==id)return button;
    throw std::runtime_error("Tool button missing");
}
void paletteFullyVisible(MainWindow& window, QScrollArea* rail){
    auto* palette=window.findChild<QWidget*>("paletteSwatches");
    auto* foreground=tool(window,"palette.foreground");
    require(palette&&foreground,"Palette controls missing");
    require(QTest::qWaitFor([&]{
        rail->verticalScrollBar()->setValue(rail->verticalScrollBar()->maximum());
        return rail->viewport()->width() >= rail->widget()->width() &&
               fullyVisible(palette) && fullyVisible(foreground);
    },std::chrono::seconds(3)),"Palette or foreground swatch is clipped");
}
void waitColumns(QToolButton* first,QToolButton* second,int columns){
    require(QTest::qWaitFor([=]{
        return columns==1 ? first->y()<second->y()
                          : first->y()==second->y()&&first->x()<second->x();
    },std::chrono::seconds(3)),"Rail did not change columns");
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
        window.resize(1500,1100);window.show();
        auto* move=tool(window,"tool.move");auto* marquee=tool(window,"tool.marquee");
        waitColumns(move,marquee,1);
        paletteFullyVisible(window,rail);
        auto* brush=tool(window,"tool.brush");
        require(move->y()<marquee->y(),"Tall rail is not one column");
        QTest::mouseClick(brush,Qt::LeftButton);
        require(brush->defaultAction()->isChecked(),"Brush not selected");
        window.resize(1500,600);waitColumns(move,marquee,2);
        require(move->y()==marquee->y()&&move->x()<marquee->x(),"Compact rail order incorrect");
        paletteFullyVisible(window,rail);
        for(auto* button:rail->findChildren<QToolButton*>()){
            if(!button->property("editorToolButton").toBool())continue;
            rail->ensureWidgetVisible(button,0,0);
            require(QTest::qWaitFor([&]{return fullyVisible(button);},std::chrono::seconds(3)),"Tool button is clipped");
        }
        require(brush->defaultAction()->isChecked(),"Selection lost on resize");
        require(project.history.undoCount()==0,"Resize changed history");
        window.resize(1500,1100);waitColumns(move,marquee,1);
        require(move->y()<marquee->y()&&brush->defaultAction()->isChecked(),"Tall layout did not restore");
        paletteFullyVisible(window,rail);
        rail->setFixedHeight(120);waitColumns(move,marquee,2);
        require(QTest::qWaitFor([&]{return rail->verticalScrollBar()->maximum()>0;},std::chrono::seconds(3)),"Compact rail does not scroll");
        rail->verticalScrollBar()->setValue(rail->verticalScrollBar()->maximum());
        paletteFullyVisible(window,rail);
        const int previousWidth=bar->width();
        bar->setStyleSheet(bar->styleSheet()+"QToolBar#tools { padding-left: 13px; padding-right: 17px; }");
        require(QTest::qWaitFor([&]{return bar->width()>previousWidth;},std::chrono::seconds(3)),"Rail width did not follow toolbar style margins");
        paletteFullyVisible(window,rail);
        rail->setMinimumHeight(0);rail->setMaximumHeight(QWIDGETSIZE_MAX);
        window.resize(1500,1100);waitColumns(move,marquee,1);
        paletteFullyVisible(window,rail);
        std::cout<<"PASS responsive tool rail\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}

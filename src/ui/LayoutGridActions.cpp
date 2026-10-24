#include "MainWindow.h"
#include <QAction>
#include <QColor>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <algorithm>
#include <cmath>

namespace compositor {
namespace {
// Cancel discards every edit. The grid's visibility is not part of this dialog and keeps its current value.
class GridSettingsDialog final:public QDialog {
public:
    GridSettingsDialog(const editing::LayoutGrid& grid,QWidget* parent):QDialog(parent),value_(grid){
        setWindowTitle("Grid Settings");
        auto*form=new QFormLayout(this);
        spacing_=new QDoubleSpinBox(this);spacing_->setRange(1,100000);spacing_->setDecimals(2);spacing_->setSuffix(" px");spacing_->setValue(grid.spacing);spacing_->setAccessibleName("Grid spacing");
        form->addRow("Major spacing",spacing_);
        subdivisions_=new QSpinBox(this);subdivisions_->setRange(1,64);subdivisions_->setValue(grid.subdivisions);subdivisions_->setAccessibleName("Grid subdivisions");
        form->addRow("Subdivisions",subdivisions_);
        opacity_=new QSpinBox(this);opacity_->setRange(0,100);opacity_->setSuffix(" %");opacity_->setValue(int(std::lround(grid.opacity*100)));opacity_->setAccessibleName("Grid opacity");
        form->addRow("Opacity",opacity_);
        color_=new QPushButton(this);color_->setAccessibleName("Grid color");
        setColor(grid.color);
        connect(color_,&QPushButton::clicked,this,[this]{
            const auto current=QColor(value_.color[0],value_.color[1],value_.color[2]);
            const auto picked=QColorDialog::getColor(current,this,"Grid Color");
            if(picked.isValid())setColor({uint8_t(picked.red()),uint8_t(picked.green()),uint8_t(picked.blue())});
        });
        form->addRow("Color",color_);
        auto*buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,this);
        connect(buttons,&QDialogButtonBox::accepted,this,&QDialog::accept);
        connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
        form->addRow(buttons);
    }
    editing::LayoutGrid result() const{
        auto grid=value_;
        grid.spacing=spacing_->value();grid.subdivisions=subdivisions_->value();grid.opacity=opacity_->value()/100.;
        return grid;
    }
private:
    void setColor(std::array<uint8_t,3> value){
        value_.color=value;
        color_->setText(QColor(value[0],value[1],value[2]).name());
    }
    editing::LayoutGrid value_;
    QDoubleSpinBox* spacing_{};
    QSpinBox* subdivisions_{};
    QSpinBox* opacity_{};
    QPushButton* color_{};
};
}

void MainWindow::setupLayoutGridActions(QMenu* view){
    auto*layout=action(view,"Layout Grid",{},[this]{
        if(!canvas())return;
        auto grid=canvas()->layoutGrid();grid.visible=!grid.visible;canvas()->setLayoutGrid(grid);refreshSnapControls();
    });
    layout->setObjectName("viewLayoutGrid");layout->setCheckable(true);
    action(view,"Grid Settings…",{},[this]{showGridSettings();})->setObjectName("viewGridSettings");
    view->addSeparator();
    auto*snap=action(view,"Snap",{},[this]{auto policy=snapPolicy_;policy.enabled=!policy.enabled;setSnapPolicy(policy);});
    snap->setObjectName("viewSnap");snap->setCheckable(true);
    auto*targets=view->addMenu("Snap To");
    const auto target=[this,targets](const char* id,const char* label,bool editing::SnapPolicy::*field){
        auto*item=action(targets,label,{},[this,field]{auto policy=snapPolicy_;policy.*field=!(policy.*field);setSnapPolicy(policy);});
        item->setObjectName(id);item->setCheckable(true);
    };
    target("snapGrid","Grid",&editing::SnapPolicy::grid);
    target("snapGuides","Guides",&editing::SnapPolicy::guides);
    target("snapLayerEdges","Layer Edges",&editing::SnapPolicy::layerEdges);
    target("snapLayerCenters","Layer Centers",&editing::SnapPolicy::layerCenters);
    target("snapDocumentEdges","Document Edges",&editing::SnapPolicy::documentEdges);
    target("snapDocumentCenter","Document Center",&editing::SnapPolicy::documentCenter);
    refreshSnapControls();
}

void MainWindow::refreshSnapControls(){
    const auto setAction=[this](const char* name,bool value){if(auto*item=findChild<QAction*>(name)){const QSignalBlocker block(item);item->setChecked(value);}};
    setAction("viewSnap",snapPolicy_.enabled);
    setAction("snapGrid",snapPolicy_.grid);setAction("snapGuides",snapPolicy_.guides);
    setAction("snapLayerEdges",snapPolicy_.layerEdges);setAction("snapLayerCenters",snapPolicy_.layerCenters);
    setAction("snapDocumentEdges",snapPolicy_.documentEdges);setAction("snapDocumentCenter",snapPolicy_.documentCenter);
    setAction("viewLayoutGrid",canvas()&&canvas()->layoutGrid().visible);
    if(auto*item=findChild<QAction*>("viewGridSettings"))item->setEnabled(canvas()!=nullptr);
    if(auto*box=findChild<QCheckBox*>("transformSnapping")){const QSignalBlocker block(box);box->setChecked(snapPolicy_.enabled);}
}

void MainWindow::setSnapPolicy(const editing::SnapPolicy& value){
    if(snapPolicy_==value)return;
    snapPolicy_=value;
    if(canvas())canvas()->setSnapGuides();
    rememberSnapAndGrid(snapPolicy_,canvas()?canvas()->layoutGrid():editing::LayoutGrid{});
    refreshSnapControls();
}

void MainWindow::showGridSettings(){
    auto*view=canvas();
    if(!view)return;
    GridSettingsDialog dialog(view->layoutGrid(),this);
    if(dialog.exec()!=QDialog::Accepted)return;
    const auto next=dialog.result();
    view->setLayoutGrid(next);
    rememberSnapAndGrid(snapPolicy_,next);
}

std::optional<editing::SnapLines> MainWindow::snapLines(const Document& document,std::span<const std::string> excluded){
    if(!snapPolicy_.enabled)return {};
    const auto grid=canvas()?canvas()->layoutGrid():editing::LayoutGrid{};
    return editing_transform::collectSnapTargets({double(document.width),double(document.height)},editing_transform::visiblePlacements(document),excluded,document.guides,snapPolicy_,grid);
}

Point MainWindow::snapGesturePoint(Point point,Qt::KeyboardModifiers modifiers,std::span<const std::string> excluded){
    auto*p=current();auto*view=canvas();
    if(!p||!p->document||!view)return point;
    std::optional<editing::SnapLines> lines;
    if(!modifiers.testFlag(Qt::ControlModifier))lines=snapLines(*p->document,excluded);
    if(!lines){view->setSnapGuides();return point;}
    const auto snapped=editing::snapPoint(point,*lines,8/std::max(view->pointsPerPixel(),.0001));
    view->setSnapGuides(snapped.x,snapped.y);
    return snapped.point;
}
}

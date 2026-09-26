#include "MainWindow.h"
#include <QStatusBar>
#include <cmath>
#include <numbers>

namespace compositor {
void MainWindow::beginGradient(Point point){
    auto*p=current();auto*layer=active();
    if(!p||!p->document||!layer||p->selected.size()!=1||(!p->maskSelected&&(layer->group||!layer->adjustmentJson.empty())))return;
    if(!drawingOriginal_){
        if(!ui::commandEnabled(ui::CommandGate::Paint,commandState()))return;
        finishOpacityEdit();drawingOriginal_=*layer;gradientOwner_=p;gradientMask_=p->maskSelected;
        gradientStart_=gradientEnd_=point;gradientHandle_=1;
    }else{
        const auto radius=10/canvas()->pointsPerPixel();
        if(std::hypot(point.x-gradientEnd_.x,point.y-gradientEnd_.y)<=radius)gradientHandle_=1;
        else if(std::hypot(point.x-gradientStart_.x,point.y-gradientStart_.y)<=radius)gradientHandle_=0;
        else{gradientStart_=gradientEnd_=point;gradientHandle_=1;}
    }
    refreshGradient();
}
void MainWindow::updateGradient(Point point,Qt::KeyboardModifiers modifiers,bool finish){
    if(!drawingOriginal_||gradientHandle_<0)return;
    const auto anchor=gradientHandle_==0?gradientEnd_:gradientStart_;
    if(modifiers.testFlag(Qt::ShiftModifier)){
        const auto dx=point.x-anchor.x,dy=point.y-anchor.y;
        const auto angle=std::round(std::atan2(dy,dx)/(std::numbers::pi/4))*(std::numbers::pi/4);
        const auto length=std::hypot(dx,dy);point={anchor.x+std::cos(angle)*length,anchor.y+std::sin(angle)*length};
    }
    (gradientHandle_==0?gradientStart_:gradientEnd_)=point;
    if(finish)gradientHandle_=-1;
    if(finish&&std::hypot(gradientEnd_.x-gradientStart_.x,gradientEnd_.y-gradientStart_.y)<.5){cancelGradient();return;}
    refreshGradient();
}
void MainWindow::refreshGradient(){
    if(!drawingOriginal_||!gradientOwner_||!gradientOwner_->document)return;
    auto&d=*gradientOwner_->document;
    auto layer=std::find_if(d.layers.begin(),d.layers.end(),[&](const Layer&l){return l.id==drawingOriginal_->id;});
    if(layer==d.layers.end()){cancelGradient();return;}
    try{
        if(std::hypot(gradientEnd_.x-gradientStart_.x,gradientEnd_.y-gradientStart_.y)<.5)gradientOwner_->gradientPreview.reset();
        else{
            auto fg=foreground_,bg=background_;
            if(gradientMask_){fg=maskPaintWhite_?Qt::white:Qt::black;bg=maskPaintWhite_?Qt::black:Qt::white;}
            gradientOwner_->gradientPreview=std::make_shared<editing::GradientPreview>(*drawingOriginal_,d,gradientStart_,gradientEnd_,gradientSettings_,
                Pixel{uint8_t(fg.red()),uint8_t(fg.green()),uint8_t(fg.blue()),255},
                Pixel{uint8_t(bg.red()),uint8_t(bg.green()),uint8_t(bg.blue()),255},gradientMask_);
        }
        if(gradientOwner_==current())refresh();
    }catch(const std::exception&e){cancelGradient();statusBar()->showMessage(e.what());}
}
void MainWindow::applyGradient(){
    auto*owner=gradientOwner_;if(!drawingOriginal_||!owner)return;
    if(std::hypot(gradientEnd_.x-gradientStart_.x,gradientEnd_.y-gradientStart_.y)<.5){cancelGradient();return;}
    if(!owner->document||!owner->gradientPreview){cancelGradient();return;}
    bool began=false;
    try{
        auto next=*owner->document;auto target=std::find_if(next.layers.begin(),next.layers.end(),[&](const Layer& layer){return layer.id==drawingOriginal_->id;});
        if(target==next.layers.end()){cancelGradient();return;}
        if(*target!=*drawingOriginal_)throw std::runtime_error("The gradient target changed during preview");
        *target=owner->gradientPreview->materializeLayer();validateDocument(next);
        owner->history.begin(gradientMask_?"Gradient Mask":"Gradient",owner->document,owner->active);began=true;
        owner->document=std::move(next);owner->history.end(owner->document,owner->active);owner->gradientPreview.reset();
        drawingOriginal_.reset();gradientOwner_=nullptr;gradientHandle_=-1;refresh(false);
    }catch(const std::exception& e){
        if(began)if(auto snapshot=owner->history.cancel()){owner->document=std::move(snapshot->document);owner->active=std::move(snapshot->activeLayer);}
        cancelGradient();statusBar()->showMessage(e.what());
    }
}
void MainWindow::cancelGradient(){
    auto*owner=gradientOwner_;if(!drawingOriginal_||!owner)return;
    owner->gradientPreview.reset();
    drawingOriginal_.reset();gradientOwner_=nullptr;gradientHandle_=-1;
    if(owner==current())refresh();
}
}

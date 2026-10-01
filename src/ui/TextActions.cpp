#include "MainWindow.h"
#include "PaletteDialog.h"
#include "PropertyControls.h"
#include "text/TextStyle.h"
#include <QApplication>
#include <QFontComboBox>
#include <QToolBar>
#include <QToolButton>
#include <QLabel>
#include <QSignalBlocker>
#include <QClipboard>
#include <QGuiApplication>
#include <QStatusBar>
#include <QInputMethodEvent>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

namespace compositor {
namespace {
QString unitsOf(const std::string& value){return QString::fromUtf8(value.data(),int(value.size()));}
int previousUnit(const QString& text,int index){if(index<=0)return 0;const int previous=index-1;if(previous>0&&text.at(previous).isLowSurrogate()&&text.at(previous-1).isHighSurrogate())return previous-1;return previous;}
int nextUnit(const QString& text,int index){if(index>=text.size())return text.size();if(text.at(index).isHighSurrogate()&&index+1<text.size()&&text.at(index+1).isLowSurrogate())return index+2;return index+1;}
int lineEdge(const text::TextLayout& layout,int index,bool start){
    if(layout.carets.empty())return 0;
    index=std::clamp(index,0,int(layout.carets.size())-1);
    const int line=layout.carets[size_t(index)].line;
    int edge=index;
    for(int caret=0;caret<int(layout.carets.size());++caret)if(layout.carets[size_t(caret)].line==line&&(start?caret<edge:caret>edge))edge=caret;
    return edge;
}
int adjacentLine(const text::TextLayout& layout,int index,int direction){
    if(layout.carets.empty())return 0;
    index=std::clamp(index,0,int(layout.carets.size())-1);
    const int line=layout.carets[size_t(index)].line;
    const float x=layout.carets[size_t(index)].x;
    int neighbor=direction<0?-1:1000000;
    for(const auto& caret:layout.carets){if(direction<0&&caret.line<line)neighbor=std::max(neighbor,caret.line);if(direction>0&&caret.line>line)neighbor=std::min(neighbor,caret.line);}
    if(neighbor<0||neighbor>100000)return index;
    int best=index;float distance=1e30f;
    for(int caret=0;caret<int(layout.carets.size());++caret)if(layout.carets[size_t(caret)].line==neighbor){const float delta=std::abs(layout.carets[size_t(caret)].x-x);if(delta<distance){distance=delta;best=caret;}}
    return best;
}
std::string layerNameFor(const std::string& value){
    auto text=unitsOf(value);text.replace('\n',' ');text.replace('\r',' ');
    const auto name=text.split(QRegularExpression("\\s+"),Qt::SkipEmptyParts).join(' ');
    auto trimmed=name.isEmpty()?QString("Text"):name;
    if(trimmed.size()>80)trimmed.truncate(80);
    return trimmed.toUtf8().toStdString();
}
bool containsText(const Transform& transform,Point point){
    if(!(transform.width>0)||!(transform.height>0))return false;
    const auto unit=transform.toUnit(point);
    return unit.x>=-0.02&&unit.y>=-0.02&&unit.x<=1.02&&unit.y<=1.02;
}
}
void MainWindow::setupTypeControls(){
    auto* bar=addToolBar("Type Options");bar->setObjectName("typeOptions");
    auto* fonts=new QFontComboBox;fonts->setObjectName("textFont");fonts->setAccessibleName("Text font");fonts->setFixedWidth(210);
    fonts->setCurrentFont(QFont(QString::fromStdString(textDefaults_.fontFamily)));
    auto* size=new ui::PropertyNumber;size->setObjectName("textSize");size->setAccessibleName("Text size");size->setRange(1,1000);size->setDecimals(0);size->setValue(textDefaults_.fontSize);size->setSuffix(" px");
    size->releaseFocus=[this]{if(canvas())canvas()->setFocus();};
    auto* color=new QToolButton;color->setObjectName("textColor");color->setAccessibleName("Text color");color->setFixedSize(36,22);
    auto* applyForeground=new QAction("Apply Text Color",this);applyForeground->setObjectName("textApplyForeground");
    auto* cancel=bar->addAction("Cancel");cancel->setObjectName("textCancel");
    auto* done=bar->addAction("Done");done->setObjectName("textDone");
    bar->addWidget(fonts);bar->addWidget(size);bar->addWidget(color);bar->addAction(cancel);bar->addAction(done);
    connect(fonts,&QFontComboBox::currentFontChanged,this,[this](const QFont& font){if(!refreshing_)applyTextFont(font.family().toStdString());});
    connect(fonts,&QComboBox::highlighted,this,[this,fonts](int index){if(index>=0)previewTextFont(fonts->itemText(index).toStdString());});
    connect(fonts,qOverload<int>(&QComboBox::activated),this,[this](int){fontChoiceKept_=true;keepTextFontPreview();});
    connect(size,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this](double value){if(!refreshing_)applyTextSize(value);});
    connect(color,&QToolButton::clicked,this,[this]{
        if(!textSession_)return;
        const auto& style=textSession_->style;
        auto* dialog=new PaletteDialog({style.red,style.green,style.blue},"Text Color",this);
        connect(dialog,&QDialog::finished,this,[this,dialog](int result){if(result==QDialog::Accepted&&textSession_)applyTextColor(dialog->color().red,dialog->color().green,dialog->color().blue);dialog->deleteLater();});
        dialog->show();
    });
    connect(applyForeground,&QAction::triggered,this,[this]{applyTextColor(foreground_.redF(),foreground_.greenF(),foreground_.blueF());});
    connect(cancel,&QAction::triggered,this,[this]{cancelText();});
    connect(done,&QAction::triggered,this,[this]{finishText();});
    addAction(applyForeground);
}
void MainWindow::refreshTypeControls(){
    auto* fonts=findChild<QFontComboBox*>("textFont");auto* size=findChild<QDoubleSpinBox*>("textSize");auto* color=findChild<QToolButton*>("textColor");
    auto* previousFocus=QApplication::focusWidget();
    const bool editingTypeField=previousFocus&&((fonts&&(previousFocus==fonts||fonts->isAncestorOf(previousFocus)))||(size&&(previousFocus==size||size->isAncestorOf(previousFocus))));
    const bool editing=textSession_&&textSession_->owner==current();
    const TextContent& style=editing?textSession_->style:textDefaults_;
    if(fonts&&!(editing&&textSession_->fontPreviewOriginal)){const QSignalBlocker block(fonts);const int start=editing?std::min(textSession_->caret,textSession_->anchor):0;const int length=editing?std::abs(textSession_->caret-textSession_->anchor):0;const auto face=length>0?text::uniformFont(style,start,length):text::fontAt(style,std::max(0,start-1));if(!face.empty())fonts->setCurrentFont(QFont(QString::fromStdString(face)));fonts->setEnabled(tool_==Tool::Text||editing);}
    else if(fonts)fonts->setEnabled(true);
    if(size){const QSignalBlocker block(size);size->setValue(style.fontSize);size->setEnabled(tool_==Tool::Text||editing);}
    if(color){color->setEnabled(editing);color->setStyleSheet(QString("background:%1;border:1px solid palette(mid);").arg(QColor::fromRgbF(style.red,style.green,style.blue).name()));}
    for(const char* name:{"textCancel","textDone"})if(auto* action=findChild<QAction*>(name)){action->setVisible(editing);action->setEnabled(editing);}
    if(previousFocus&&!editingTypeField&&previousFocus!=QApplication::focusWidget())previousFocus->setFocus(Qt::OtherFocusReason);
    if(auto* view=canvas();view&&tool_==Tool::Text&&!spaceHeld_)view->setCursor(Qt::IBeamCursor);
}
void MainWindow::publishTextEdit(){
    if(!textSession_||!textSession_->owner)return;
    auto& session=*textSession_;
    TextContent shown=session.style;
    if(!session.preedit.empty()){auto withMark=shown;if(text::replaceText(withMark,session.caret,0,session.preedit))shown=std::move(withMark);}
    try{session.layout=text::layoutText(shown);}catch(const std::exception& error){statusBar()->showMessage(error.what());return;}
    const auto& layout=session.layout;
    Transform placed{session.originX,session.originY,double(std::max(layout.width,1)),double(std::max(layout.height,1))};
    Layer* existing=nullptr;
    if(!session.layerId.empty()&&session.owner->document){
        auto found=std::find_if(session.owner->document->layers.begin(),session.owner->document->layers.end(),[&](const Layer& layer){return layer.id==session.layerId;});
        if(found!=session.owner->document->layers.end()){existing=&*found;placed=found->transform;const double scaleX=found->raster&&found->raster->width?found->transform.width/found->raster->width:1;const double scaleY=found->raster&&found->raster->height?found->transform.height/found->raster->height:1;placed.width=std::max(1.,layout.width*scaleX);placed.height=std::max(1.,layout.height*scaleY);}
    }
    auto map=[&](float x,float y){return placed.fromUnit({layout.width?double(x)/layout.width:0,layout.height?double(y)/layout.height:0});};
    NativeCanvas::TextCaretOverlay overlay;
    overlay.originX=placed.x;overlay.originY=placed.y;overlay.width=placed.width;overlay.height=placed.height;
    if(!existing)overlay.raster=layout.raster;
    const int visualCaret=std::clamp(session.caret+text::utf16Length(session.preedit),0,std::max(0,int(layout.carets.size())-1));
    overlay.caret=visualCaret;
    for(const auto& caret:layout.carets){const auto top=map(caret.x,caret.top),bottom=map(caret.x,caret.top+caret.height);overlay.carets.push_back({top.x,top.y,bottom.x,bottom.y});}
    const int start=std::min(session.caret,session.anchor),end=std::max(session.caret,session.anchor);
    for(int index=start;index<end&&index+1<int(layout.carets.size());++index){
        const auto& left=layout.carets[size_t(index)];const auto& right=layout.carets[size_t(index+1)];
        if(left.line!=right.line)continue;
        const float x=std::min(left.x,right.x),width=std::abs(right.x-left.x);
        const auto origin=map(x,left.top),opposite=map(x+width,left.top+left.height);
        overlay.selection.push_back({std::min(origin.x,opposite.x),std::min(origin.y,opposite.y),std::abs(opposite.x-origin.x),std::abs(opposite.y-origin.y)});
    }
    if(existing){
        auto preview=std::make_shared<LayerRenderPreview>();preview->layer=*existing;preview->layer.transform=placed;preview->layer.text=session.style;
        preview->layer.raster=layout.raster?layout.raster:Raster::filled(1,1,{0,0,0,0});
        preview->identity=std::make_shared<int>(0);preview->damageComparedWith=[](const LayerRenderPreview*){return std::nullopt;};
        session.owner->textPreview=std::move(preview);
    }else session.owner->textPreview.reset();
    if(session.owner->canvas)session.owner->canvas->setTextOverlay(std::move(overlay));
    if(session.owner==current())refresh(true,false);
}
void MainWindow::beginText(Point point,bool forceNew,bool extend){
    if(!canEditLayers()||!current()||!current()->document)return;
    if(textSession_&&textSession_->owner==current()&&!forceNew){
        Transform placed{textSession_->originX,textSession_->originY,double(std::max(textSession_->layout.width,1)),double(std::max(textSession_->layout.height,1))};
        if(!textSession_->layerId.empty()){auto found=std::find_if(current()->document->layers.begin(),current()->document->layers.end(),[&](const Layer& layer){return layer.id==textSession_->layerId;});if(found!=current()->document->layers.end()){placed=found->transform;placed.width=std::max(1.,double(textSession_->layout.width));placed.height=std::max(1.,double(textSession_->layout.height));}}
        if(containsText(placed,point)&&!textSession_->layout.carets.empty()){
            const auto unit=placed.toUnit(point);
            const float x=float(unit.x*textSession_->layout.width),y=float(unit.y*textSession_->layout.height);
            int best=0;float distance=1e30f;
            for(int index=0;index<int(textSession_->layout.carets.size());++index){const auto& caret=textSession_->layout.carets[size_t(index)];const float dx=caret.x-x,dy=caret.top+caret.height*.5f-y,delta=dx*dx+dy*dy;if(delta<distance){distance=delta;best=index;}}
            textSession_->caret=best;if(!extend)textSession_->anchor=best;textSession_->preedit.clear();
            if(canvas())canvas()->setFocus();publishTextEdit();return;
        }
    }
    if(textSession_&&!finishText())return;
    auto* project=current();if(!project||!project->document)return;
    TextSession session;session.owner=project;session.originX=point.x;session.originY=point.y;session.style=textDefaults_;
    if(!forceNew){
        for(auto layer=project->document->layers.rbegin();layer!=project->document->layers.rend();++layer){
            if(!layer->visible||layer->group||!layer->text||!containsText(layer->transform,point))continue;
            session.layerId=layer->id;session.style=*layer->text;session.originX=layer->transform.x;session.originY=layer->transform.y;
            project->active=layer->id;project->selected={layer->id};project->maskSelected=false;break;
        }
    }
    if(session.layerId.empty()){session.style.value.clear();session.style.colorRuns.clear();session.style.fontRuns.clear();session.style.red=foreground_.redF();session.style.green=foreground_.greenF();session.style.blue=foreground_.blueF();session.style.alpha=foreground_.alphaF();}
    const int count=text::utf16Length(session.style.value);session.caret=session.anchor=session.layerId.empty()?0:count;
    textSession_=std::move(session);tool_=Tool::Text;if(canvas())canvas()->setFocus();publishTextEdit();
}
bool MainWindow::finishText(){
    if(!textSession_)return true;
    endTextFontPreview();
    auto session=std::move(*textSession_);textSession_.reset();
    auto* project=session.owner;
    auto clear=[&]{if(project){project->textPreview.reset();if(project->canvas)project->canvas->setTextOverlay({});}};
    if(!project||!project->document){clear();return true;}
    const bool blank=unitsOf(session.style.value).trimmed().isEmpty();
    auto commit=[&](const char* name,const std::function<void(Document&)>& change){
        project->history.begin(name,project->document,project->active);
        try{change(*project->document);validateDocument(*project->document);project->history.end(project->document,project->active);}
        catch(...){if(auto snapshot=project->history.cancel()){project->document=std::move(snapshot->document);project->active=std::move(snapshot->activeLayer);}textSession_=std::move(session);publishTextEdit();throw;}
    };
    try{
        if(session.layerId.empty()){
            if(blank){clear();if(project==current())refresh(false,false);return true;}
            if(!text::textRunsValid(session.style))throw std::runtime_error("Invalid text");
            auto drawn=text::rasterize(session.style);
            commit("New Text Layer",[&](Document& document){Layer layer;layer.id=newId();layer.name=layerNameFor(session.style.value);layer.text=session.style;layer.raster=drawn.raster;layer.transform={session.originX,session.originY,double(drawn.width),double(drawn.height)};document.layers.push_back(std::move(layer));project->active=document.layers.back().id;project->selected={project->active};project->maskSelected=false;});
        }else{
            auto found=std::find_if(project->document->layers.begin(),project->document->layers.end(),[&](const Layer& layer){return layer.id==session.layerId;});
            if(found==project->document->layers.end()){clear();if(project==current())refresh(false,false);return true;}
            if(blank){const auto id=session.layerId;commit("Edit Text",[&](Document& document){auto layer=std::find_if(document.layers.begin(),document.layers.end(),[&](const Layer& item){return item.id==id;});if(layer!=document.layers.end())document.layers.erase(layer);if(project->active==id){project->active=document.layers.empty()?"":document.layers.back().id;project->selected={project->active};}});}
            else if(found->text&&*found->text==session.style){clear();if(project==current())refresh(false,false);return true;}
            else{
                if(!text::textRunsValid(session.style))throw std::runtime_error("Invalid text");
                auto drawn=text::rasterize(session.style);
                const double scaleX=found->raster&&found->raster->width?found->transform.width/found->raster->width:1;
                const double scaleY=found->raster&&found->raster->height?found->transform.height/found->raster->height:1;
                const auto id=session.layerId;
                commit("Edit Text",[&](Document& document){auto layer=std::find_if(document.layers.begin(),document.layers.end(),[&](const Layer& item){return item.id==id;});if(layer==document.layers.end())return;layer->text=session.style;layer->raster=drawn.raster;layer->transform.width=std::max(1.,drawn.width*scaleX);layer->transform.height=std::max(1.,drawn.height*scaleY);});
            }
        }
        textDefaults_.fontFamily=session.style.fontFamily;textDefaults_.fontSize=session.style.fontSize;
        clear();if(project==current())refresh();return true;
    }catch(const std::exception& error){if(!textSession_)textSession_=std::move(session);statusBar()->showMessage(error.what());if(textSession_)publishTextEdit();return false;}
}
void MainWindow::cancelText(){
    if(!textSession_)return;auto* project=textSession_->owner;textSession_.reset();
    if(project){project->textPreview.reset();if(project->canvas)project->canvas->setTextOverlay({});}
    if(project==current())refresh(false,false);
}
bool MainWindow::undoTextTyping(bool redo){
    if(!textSession_)return false;
    endTextFontPreview();
    auto& from=redo?textSession_->redo:textSession_->undo;auto& to=redo?textSession_->undo:textSession_->redo;
    if(from.empty())return true;
    to.push_back({textSession_->style,textSession_->caret,textSession_->anchor});
    const auto step=from.back();from.pop_back();
    textSession_->style=step.style;textSession_->caret=step.caret;textSession_->anchor=step.anchor;textSession_->preedit.clear();
    publishTextEdit();return true;
}
void MainWindow::previewTextFont(const std::string& family){
    if(!textSession_||family.empty())return;
    if(!textSession_->fontPreviewOriginal)textSession_->fontPreviewOriginal=textSession_->style;
    auto next=*textSession_->fontPreviewOriginal;
    text::setTextFont(next,family,std::min(textSession_->caret,textSession_->anchor),std::abs(textSession_->caret-textSession_->anchor));
    if(!text::textRunsValid(next))return;
    textSession_->style=std::move(next);publishTextEdit();
}
void MainWindow::keepTextFontPreview(){
    if(!textSession_||!textSession_->fontPreviewOriginal)return;
    textSession_->undo.push_back({*textSession_->fontPreviewOriginal,textSession_->caret,textSession_->anchor});
    textSession_->redo.clear();textSession_->fontPreviewOriginal.reset();
}
void MainWindow::endTextFontPreview(){
    if(!textSession_||!textSession_->fontPreviewOriginal)return;
    textSession_->style=*textSession_->fontPreviewOriginal;textSession_->fontPreviewOriginal.reset();publishTextEdit();
}
void MainWindow::applyTextFont(const std::string& family){
    if(!textSession_){textDefaults_.fontFamily=family;return;}
    if(textSession_->fontPreviewOriginal)return;
    auto next=textSession_->style;
    text::setTextFont(next,family,std::min(textSession_->caret,textSession_->anchor),std::abs(textSession_->caret-textSession_->anchor));
    if(next.fontFamily==textSession_->style.fontFamily&&next.fontRuns==textSession_->style.fontRuns)return;
    if(!text::textRunsValid(next))return;
    textSession_->undo.push_back({textSession_->style,textSession_->caret,textSession_->anchor});textSession_->redo.clear();
    textSession_->style=std::move(next);publishTextEdit();
}
void MainWindow::applyTextSize(double size){
    if(!textSession_){text::setTextSize(textDefaults_,size);return;}
    endTextFontPreview();
    auto next=textSession_->style;text::setTextSize(next,size);
    if(next.fontSize==textSession_->style.fontSize&&next.fontRuns==textSession_->style.fontRuns)return;
    textSession_->undo.push_back({textSession_->style,textSession_->caret,textSession_->anchor});textSession_->redo.clear();
    textSession_->style=std::move(next);publishTextEdit();
}
void MainWindow::applyTextColor(double red,double green,double blue){
    if(!textSession_)return;
    endTextFontPreview();
    auto next=textSession_->style;
    text::setTextColor(next,red,green,blue,std::min(textSession_->caret,textSession_->anchor),std::abs(textSession_->caret-textSession_->anchor));
    if(next==textSession_->style)return;
    if(!text::textRunsValid(next))return;
    textSession_->undo.push_back({textSession_->style,textSession_->caret,textSession_->anchor});textSession_->redo.clear();
    textSession_->style=std::move(next);publishTextEdit();
}
void MainWindow::handleTextInput(QInputMethodEvent* event){
    if(!textSession_||!event)return;
    if(!event->commitString().isEmpty()){
        const auto committed=event->commitString().toUtf8().toStdString();
        textSession_->preedit.clear();
        textSession_->undo.push_back({textSession_->style,textSession_->caret,textSession_->anchor});textSession_->redo.clear();
        const int start=std::min(textSession_->caret,textSession_->anchor),length=std::abs(textSession_->caret-textSession_->anchor);
        if(!text::replaceText(textSession_->style,start,length,committed))textSession_->undo.pop_back();
        else textSession_->caret=textSession_->anchor=start+text::utf16Length(committed);
    }
    textSession_->preedit=event->preeditString().toUtf8().toStdString();
    publishTextEdit();
}
bool MainWindow::handleTextKey(QKeyEvent* event){
    if(!textSession_||!event||textSession_->owner!=current())return false;
    const auto key=event->key();const auto modifiers=event->modifiers();
    const bool shift=modifiers.testFlag(Qt::ShiftModifier),control=modifiers.testFlag(Qt::ControlModifier),alt=modifiers.testFlag(Qt::AltModifier);
    auto move=[&](int index){textSession_->caret=index;if(!shift)textSession_->anchor=index;textSession_->preedit.clear();publishTextEdit();};
    if(key==Qt::Key_Escape){cancelText();return true;}
    if((key==Qt::Key_Return||key==Qt::Key_Enter)&&control&&!alt){finishText();return true;}
    if(control&&!alt&&!shift&&(key==Qt::Key_Z||key==Qt::Key_Y)){undoTextTyping(key==Qt::Key_Y);return true;}
    if(control&&!alt&&shift&&key==Qt::Key_Z){undoTextTyping(true);return true;}
    const auto text=unitsOf(textSession_->style.value);
    if(control&&!alt&&key==Qt::Key_A){textSession_->anchor=0;textSession_->caret=text.size();textSession_->preedit.clear();publishTextEdit();return true;}
    if(control&&!alt&&(key==Qt::Key_C||key==Qt::Key_X||key==Qt::Key_V)){
        const int start=std::min(textSession_->caret,textSession_->anchor),length=std::abs(textSession_->caret-textSession_->anchor);
        if(key!=Qt::Key_V)QGuiApplication::clipboard()->setText(text.mid(start,length));
        if(key==Qt::Key_C)return true;
        const auto pasted=key==Qt::Key_V?QGuiApplication::clipboard()->text():QString();
        textSession_->undo.push_back({textSession_->style,textSession_->caret,textSession_->anchor});textSession_->redo.clear();textSession_->preedit.clear();
        if(!text::replaceText(textSession_->style,start,length,pasted.toUtf8().toStdString()))textSession_->undo.pop_back();
        else textSession_->caret=textSession_->anchor=start+text::utf16Length(pasted.toUtf8().toStdString());
        publishTextEdit();return true;
    }
    if(key==Qt::Key_Left||key==Qt::Key_Right||key==Qt::Key_Up||key==Qt::Key_Down||key==Qt::Key_Home||key==Qt::Key_End){
        int index=textSession_->caret;
        if(key==Qt::Key_Left)index=previousUnit(text,index);
        else if(key==Qt::Key_Right)index=nextUnit(text,index);
        else if(key==Qt::Key_Up)index=adjacentLine(textSession_->layout,index,-1);
        else if(key==Qt::Key_Down)index=adjacentLine(textSession_->layout,index,1);
        else if(key==Qt::Key_Home)index=lineEdge(textSession_->layout,index,true);
        else index=lineEdge(textSession_->layout,index,false);
        move(index);return true;
    }
    if(key==Qt::Key_Backspace||key==Qt::Key_Delete){
        int start=std::min(textSession_->caret,textSession_->anchor),length=std::abs(textSession_->caret-textSession_->anchor);
        if(length==0){if(key==Qt::Key_Backspace){const int previous=previousUnit(text,textSession_->caret);length=textSession_->caret-previous;start=previous;}else length=nextUnit(text,textSession_->caret)-textSession_->caret;}
        if(length<=0)return true;
        textSession_->undo.push_back({textSession_->style,textSession_->caret,textSession_->anchor});textSession_->redo.clear();textSession_->preedit.clear();
        if(!text::replaceText(textSession_->style,start,length,{}))textSession_->undo.pop_back();
        else textSession_->caret=textSession_->anchor=start;
        publishTextEdit();return true;
    }
    if((key==Qt::Key_Return||key==Qt::Key_Enter)&&!control){
        textSession_->undo.push_back({textSession_->style,textSession_->caret,textSession_->anchor});textSession_->redo.clear();textSession_->preedit.clear();
        const int start=std::min(textSession_->caret,textSession_->anchor);
        if(!text::replaceText(textSession_->style,start,std::abs(textSession_->caret-textSession_->anchor),"\n"))textSession_->undo.pop_back();
        else textSession_->caret=textSession_->anchor=start+1;
        publishTextEdit();return true;
    }
    QString typed=event->text();
    if(typed.isEmpty()&&!control&&!alt){
        if(key>=Qt::Key_A&&key<=Qt::Key_Z)typed=QChar(shift?key:key-Qt::Key_A+'a');
        else if(key>=Qt::Key_0&&key<=Qt::Key_9)typed=QChar('0'+key-Qt::Key_0);
        else if(key==Qt::Key_Space)typed=QChar(' ');
    }
    if(!control&&!alt&&!typed.isEmpty()){
        if(typed.at(0).isPrint()||typed==QString(QChar('\t'))){
            textSession_->undo.push_back({textSession_->style,textSession_->caret,textSession_->anchor});textSession_->redo.clear();textSession_->preedit.clear();
            const int start=std::min(textSession_->caret,textSession_->anchor);
            const auto utf8=typed.toUtf8().toStdString();
            if(!text::replaceText(textSession_->style,start,std::abs(textSession_->caret-textSession_->anchor),utf8))textSession_->undo.pop_back();
            else textSession_->caret=textSession_->anchor=start+text::utf16Length(utf8);
            publishTextEdit();return true;
        }
    }
    return false;
}
}

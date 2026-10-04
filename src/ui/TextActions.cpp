#include "MainWindow.h"
#include "PaletteDialog.h"
#include "PropertyControls.h"
#include "text/TextStyle.h"
#include <QApplication>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QComboBox>
#include <QAbstractItemView>
#include <QLineEdit>
#include <QMouseEvent>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QButtonGroup>
#include <QLabel>
#include <QSignalBlocker>
#include <QClipboard>
#include <QGuiApplication>
#include <QStatusBar>
#include <QInputMethodEvent>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <functional>

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
bool shown(const Document& document,const std::string& id){const auto entries=layers::entries(document);return std::any_of(entries.begin(),entries.end(),[&](const layers::Entry& entry){return entry.id==id&&entry.visible;});}
Transform textFrame(EditorProject* owner,const std::string& layerId,double originX,double originY,int layoutWidth,int layoutHeight){
    Transform placed{originX,originY,double(std::max(layoutWidth,1)),double(std::max(layoutHeight,1))};
    if(layerId.empty()||!owner||!owner->document)return placed;
    auto found=std::find_if(owner->document->layers.begin(),owner->document->layers.end(),[&](const Layer& layer){return layer.id==layerId;});
    if(found==owner->document->layers.end())return placed;
    placed=found->transform;
    placed.x=originX;placed.y=originY;
    const double scaleX=found->raster&&found->raster->width?found->transform.width/found->raster->width:1;
    const double scaleY=found->raster&&found->raster->height?found->transform.height/found->raster->height:1;
    placed.width=std::max(1.,layoutWidth*scaleX);placed.height=std::max(1.,layoutHeight*scaleY);
    return placed;
}
bool wordUnit(const QString& text,int index);
int wordForward(const QString& text,int index){
    while(index<text.size()&&wordUnit(text,index))index=nextUnit(text,index);
    while(index<text.size()&&!wordUnit(text,index))index=nextUnit(text,index);
    return index;
}
int wordBackward(const QString& text,int index){
    if(index<=0)return 0;
    index=previousUnit(text,index);
    while(index>0&&!wordUnit(text,index))index=previousUnit(text,index);
    while(index>0&&wordUnit(text,previousUnit(text,index)))index=previousUnit(text,index);
    return index;
}
bool hitsVisibleText(const Document& document,Point point){
    for(auto layer=document.layers.rbegin();layer!=document.layers.rend();++layer){
        if(layer->group||!layer->text||!shown(document,layer->id)||!containsText(layer->transform,point))continue;
        return true;
    }
    return false;
}
int lineAt(const text::TextLayout& layout,float y){
    if(layout.carets.empty())return 0;
    for(const auto& caret:layout.carets)if(y>=caret.top&&y<caret.top+std::max(caret.height,1.f))return caret.line;
    int line=layout.carets.front().line;float best=1e30f;
    for(const auto& caret:layout.carets){const float delta=std::abs(caret.top+caret.height*.5f-y);if(delta<best){best=delta;line=caret.line;}}
    return line;
}
int caretIndex(const text::TextLayout& layout,float x,float y){
    if(layout.carets.empty())return 0;
    const int line=lineAt(layout,y);
    int best=0;float distance=1e30f;
    for(int index=0;index<int(layout.carets.size());++index)if(layout.carets[size_t(index)].line==line){const float delta=std::abs(layout.carets[size_t(index)].x-x);if(delta<distance){distance=delta;best=index;}}
    return best;
}
int characterIndex(const text::TextLayout& layout,float x,float y){
    const int boundary=caretIndex(layout,x,y);
    const int line=layout.carets.empty()?0:layout.carets[size_t(std::clamp(boundary,0,int(layout.carets.size())-1))].line;
    for(int index=0;index+1<int(layout.carets.size());++index){
        const auto& left=layout.carets[size_t(index)];const auto& right=layout.carets[size_t(index+1)];
        if(left.line!=line||right.line!=line)continue;
        const float lo=std::min(left.x,right.x),hi=std::max(left.x,right.x);
        if(x>=lo&&x<hi)return index;
    }
    return boundary>0?boundary-1:boundary;
}
bool wordUnit(const QString& text,int index){if(index<0||index>=text.size())return false;const auto character=text.at(index);if(character.isLowSurrogate())return false;return character.isLetterOrNumber()||character==QLatin1Char('_')||character==QLatin1Char('\'');}
void openComboOnFieldClick(QComboBox* combo){
    if(!combo)return;
    auto* opener=new QObject(combo);
    opener->setObjectName("comboFieldPopup");
    auto press=[=](QObject* watched,QEvent* event)->bool{
        if(event->type()!=QEvent::MouseButtonPress)return false;
        auto* mouse=static_cast<QMouseEvent*>(event);
        if(mouse->button()!=Qt::LeftButton||!combo->isEnabled())return false;
        if(combo->view()&&combo->view()->isVisible())return false;
        if(watched==combo->lineEdit()||watched==combo){
            QTimer::singleShot(0,combo,[combo]{
                if(!combo->isEnabled())return;
                combo->setProperty("typePopupQuiet",true);
                combo->showPopup();
                combo->setProperty("typePopupQuiet",false);
            });
            return watched==combo->lineEdit();
        }
        return false;
    };
    struct Filter final:QObject{
        std::function<bool(QObject*,QEvent*)> handle;
        using QObject::QObject;
        bool eventFilter(QObject* watched,QEvent* event) override {return handle&&handle(watched,event)?true:QObject::eventFilter(watched,event);}
    };
    auto* filter=new Filter(opener);filter->handle=press;
    combo->installEventFilter(filter);
    if(combo->lineEdit())combo->lineEdit()->installEventFilter(filter);
}
std::pair<int,int> wordRange(const QString& text,int index){
    if(text.isEmpty())return {0,0};
    index=std::clamp(index,0,int(text.size())-1);
    if(text.at(index).isLowSurrogate())index=previousUnit(text,index);
    int start=index,end=nextUnit(text,index);
    if(wordUnit(text,index)){while(start>0&&wordUnit(text,previousUnit(text,start)))start=previousUnit(text,start);while(end<text.size()&&wordUnit(text,end))end=nextUnit(text,end);}
    else if(text.at(index).isSpace()){while(start>0&&text.at(previousUnit(text,start)).isSpace())start=previousUnit(text,start);while(end<text.size()&&text.at(end).isSpace())end=nextUnit(text,end);}
    return {start,end};
}
}
void MainWindow::setupTypeControls(){
    auto* bar=addToolBar("Type Options");bar->setObjectName("typeOptions");
    auto* fonts=new QFontComboBox;fonts->setObjectName("textFont");fonts->setAccessibleName("Text font");fonts->setFixedWidth(180);
    fonts->setEditable(true);fonts->lineEdit()->setReadOnly(true);fonts->lineEdit()->setPlaceholderText("Multiple");
    fonts->setCurrentFont(QFont(QString::fromStdString(textDefaults_.fontFamily)));
    auto* styles=new QComboBox;styles->setObjectName("textStyle");styles->setAccessibleName("Text style");styles->setFixedWidth(120);
    styles->setEditable(true);styles->lineEdit()->setReadOnly(true);styles->lineEdit()->setPlaceholderText("Multiple");
    openComboOnFieldClick(fonts);openComboOnFieldClick(styles);
    textFontBox_=fonts;textStyleBox_=styles;
    auto* size=new ui::PropertyNumber;size->setObjectName("textSize");size->setAccessibleName("Text size");size->setRange(1,1000);size->setDecimals(0);size->setValue(textDefaults_.fontSize);size->setSuffix(" px");
    size->releaseFocus=[this]{focusTextCanvas();};
    auto* color=new QToolButton;color->setObjectName("textColor");color->setAccessibleName("Text color");color->setFixedSize(36,22);
    auto* align=new QButtonGroup(bar);align->setObjectName("textAlign");align->setExclusive(true);
    auto addAlign=[&](const char* name,const QString& label,TextAlignment value){auto* button=new QToolButton;button->setObjectName(name);button->setAccessibleName(label);button->setText(label);button->setCheckable(true);button->setAutoRaise(true);align->addButton(button,int(value));bar->addWidget(button);return button;};
    auto* tracking=new ui::PropertyNumber;tracking->setObjectName("textTracking");tracking->setAccessibleName("Tracking");tracking->setRange(-100,1000);tracking->setDecimals(0);tracking->setValue(0);tracking->releaseFocus=[this]{focusTextCanvas();};
    auto* leading=new QLineEdit;leading->setObjectName("textLeading");leading->setAccessibleName("Leading");leading->setPlaceholderText("Auto");leading->setFixedWidth(64);
    auto* applyForeground=new QAction("Apply Text Color",this);applyForeground->setObjectName("textApplyForeground");
    auto* cancel=bar->addAction("Cancel");cancel->setObjectName("textCancel");
    auto* done=bar->addAction("Done");done->setObjectName("textDone");
    bar->addWidget(fonts);bar->addWidget(styles);bar->addWidget(size);bar->addWidget(color);
    addAlign("textAlignLeft","Align left",TextAlignment::Left);addAlign("textAlignCenter","Align center",TextAlignment::Center);addAlign("textAlignRight","Align right",TextAlignment::Right);
    bar->addWidget(tracking);bar->addWidget(leading);bar->addAction(cancel);bar->addAction(done);
    connect(fonts,&QFontComboBox::currentFontChanged,this,[this,fonts](const QFont& font){if(refreshing_||fonts->property("typePopupQuiet").toBool()||(fonts->view()&&fonts->view()->isVisible()))return;applyTextFont(font.family().toStdString());});
    connect(fonts,&QComboBox::highlighted,this,[this,fonts](int index){if(index<0||fonts->property("typePopupQuiet").toBool()||!fonts->view()||!fonts->view()->isVisible()||index==fonts->currentIndex())return;previewTextFont(fonts->itemText(index).toStdString());});
    connect(fonts,qOverload<int>(&QComboBox::activated),this,[this,fonts](int index){
        if(index<0)return;
        fontChoiceKept_=true;
        keepTextFontPreview();
        if(!refreshing_)applyTextFont(fonts->itemText(index).toStdString());
        if(!refreshing_)publishTextEdit();
        QTimer::singleShot(0,this,[this]{focusTextCanvas();});
    });
    connect(styles,&QComboBox::highlighted,this,[this,styles](int index){if(index<0||styles->property("typePopupQuiet").toBool()||!styles->view()||!styles->view()->isVisible()||index==styles->currentIndex())return;previewTextStyle(styles->itemText(index).toStdString());});
    connect(styles,qOverload<int>(&QComboBox::activated),this,[this,styles](int index){
        if(index<0)return;
        fontChoiceKept_=true;
        keepTextFontPreview();
        if(!refreshing_)applyTextStyle(styles->itemText(index).toStdString());
        if(!refreshing_)publishTextEdit();
        QTimer::singleShot(0,this,[this]{focusTextCanvas();});
    });
    connect(styles,qOverload<int>(&QComboBox::currentIndexChanged),this,[this,styles](int index){
        if(refreshing_||index<0||styles->property("typePopupQuiet").toBool()||(styles->view()&&styles->view()->isVisible()))return;
        applyTextStyle(styles->itemText(index).toStdString());
    });
    connect(size,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this](double value){if(!refreshing_)applyTextSize(value);});
    connect(align,&QButtonGroup::idClicked,this,[this](int id){if(!refreshing_)applyTextAlignment(TextAlignment(id));});
    connect(tracking,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this](double value){if(!refreshing_)applyTextTracking(value);});
    connect(leading,&QLineEdit::editingFinished,this,[this,leading]{if(refreshing_)return;auto typed=leading->text().trimmed();applyTextLeading(typed.isEmpty()?0:typed.toDouble());focusTextCanvas();});
    connect(color,&QToolButton::clicked,this,[this]{
        if(!textSession_)return;
        endTextFontPreview();
        const int start=std::min(textSession_->caret,textSession_->anchor),length=std::abs(textSession_->caret-textSession_->anchor);
        double red=0,green=0,blue=0;text::colorAt(textSession_->style,length>0?start:std::max(0,start-1),red,green,blue);
        colorBeforePicker_=textSession_->style;
        auto* dialog=new PaletteDialog({red,green,blue},"Text Color",this);
        dialog->onPreview=[this](effects_tools::PaletteColor color){previewTextColor(color.red,color.green,color.blue);};
        connect(dialog,&QDialog::finished,this,[this,dialog](int result){
            if(textSession_&&colorBeforePicker_){
                const auto before=*colorBeforePicker_;
                textSession_->style=before;
                if(result==QDialog::Accepted)applyTextColor(dialog->color().red,dialog->color().green,dialog->color().blue);
                else publishTextEdit();
            }
            colorBeforePicker_.reset();dialog->deleteLater();focusTextCanvas();
        });
        dialog->show();
    });
    connect(applyForeground,&QAction::triggered,this,[this]{applyTextColor(foreground_.redF(),foreground_.greenF(),foreground_.blueF());});
    connect(cancel,&QAction::triggered,this,[this]{cancelText();});
    connect(done,&QAction::triggered,this,[this]{finishText();});
    addAction(applyForeground);
}
void MainWindow::refreshTypeControls(){
    auto* fonts=findChild<QFontComboBox*>("textFont");auto* styles=findChild<QComboBox*>("textStyle");auto* size=findChild<QDoubleSpinBox*>("textSize");auto* color=findChild<QToolButton*>("textColor");
    auto* tracking=findChild<QDoubleSpinBox*>("textTracking");auto* leading=findChild<QLineEdit*>("textLeading");
    auto* previousFocus=QApplication::focusWidget();
    const bool editingTypeField=previousFocus&&((fonts&&(previousFocus==fonts||fonts->isAncestorOf(previousFocus)))||(styles&&(previousFocus==styles||styles->isAncestorOf(previousFocus)))||(size&&(previousFocus==size||size->isAncestorOf(previousFocus)))||(tracking&&(previousFocus==tracking||tracking->isAncestorOf(previousFocus)))||leading==previousFocus);
    const bool editing=textSession_&&textSession_->owner==current();
    const TextContent& style=editing?textSession_->style:textDefaults_;
    const int start=editing?std::min(textSession_->caret,textSession_->anchor):0,length=editing?std::abs(textSession_->caret-textSession_->anchor):0;
    const auto family=length>0?text::uniformFont(style,start,length):text::fontAt(style,std::max(0,start-1));
    const auto variant=length>0?text::uniformStyle(style,start,length):text::styleAt(style,std::max(0,start-1));
    const bool mixed=length>0&&family.empty();
    if(fonts){const QSignalBlocker block(fonts);if(const int marker=fonts->findText("Multiple");marker>=0&&!mixed)fonts->removeItem(marker);if(mixed){if(fonts->findText("Multiple")<0)fonts->insertItem(0,"Multiple");fonts->setCurrentIndex(fonts->findText("Multiple"));}else if(!family.empty()&&!(editing&&textSession_->fontPreviewOriginal))fonts->setCurrentFont(QFont(QString::fromStdString(family)));fonts->setEnabled(tool_==Tool::Text||editing);}
    else if(fonts)fonts->setEnabled(true);
    if(styles){const QSignalBlocker block(styles);
        if(mixed){if(styles->findText("Multiple")<0){styles->clear();styles->addItem("Multiple");}styles->setCurrentIndex(styles->findText("Multiple"));}
        else if(!(editing&&textSession_->fontPreviewOriginal)){
            QString listed=QString::fromStdString(family.empty()?style.fontFamily:family);
            if(listed.isEmpty()&&fonts)listed=fonts->currentFont().family();
            auto names=QFontDatabase::styles(listed);
            if(names.isEmpty()&&fonts)names=QFontDatabase::styles(fonts->currentFont().family());
            if(names.isEmpty())names=QStringList{"Regular","Bold","Italic","Bold Italic"};
            if(styles->count()!=names.size()||(styles->count()&&styles->itemText(0)!=names.value(0))){styles->clear();for(const auto& name:names)styles->addItem(name);}
            QString wanted=QString::fromStdString(variant.empty()?std::string("Regular"):variant);
            int found=styles->findText(wanted);if(found<0)found=styles->findText(wanted,Qt::MatchContains);if(found>=0)styles->setCurrentIndex(found);
        }
        styles->setEnabled(tool_==Tool::Text||editing);}
    if(size){const QSignalBlocker block(size);size->setValue(style.fontSize);size->setEnabled(tool_==Tool::Text||editing);}
    if(color){double red=style.red,green=style.green,blue=style.blue;if(editing)text::colorAt(style,length>0?start:std::max(0,start-1),red,green,blue);color->setEnabled(editing);color->setStyleSheet(QString("background:%1;border:1px solid palette(mid);").arg(QColor::fromRgbF(red,green,blue).name()));}
    if(auto* group=findChild<QButtonGroup*>("textAlign")){const QSignalBlocker block(group);if(auto* button=group->button(int(style.alignment)))button->setChecked(true);for(auto* button:group->buttons())button->setEnabled(editing);}
    if(tracking){const QSignalBlocker block(tracking);tracking->setValue(style.tracking);tracking->setEnabled(editing);}
    if(leading&&previousFocus!=leading){const QSignalBlocker block(leading);leading->setText(style.leading>0?QString::number(style.leading):QString());leading->setEnabled(editing);}
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
    Layer* existing=nullptr;
    Transform placed=textFrame(session.owner,session.layerId,session.originX,session.originY,layout.width,layout.height);
    if(!session.layerId.empty()&&session.owner->document){
        auto found=std::find_if(session.owner->document->layers.begin(),session.owner->document->layers.end(),[&](const Layer& layer){return layer.id==session.layerId;});
        if(found!=session.owner->document->layers.end())existing=&*found;
    }
    auto map=[&](float x,float y){return placed.fromUnit({layout.width?double(x)/layout.width:0,layout.height?double(y)/layout.height:0});};
    NativeCanvas::TextCaretOverlay overlay;
    overlay.originX=placed.x;overlay.originY=placed.y;overlay.width=placed.width;overlay.height=placed.height;
    if(!existing)overlay.raster=layout.raster;
    const int visualCaret=std::clamp(session.caret+text::utf16Length(session.preedit),0,std::max(0,int(layout.carets.size())-1));
    overlay.caret=visualCaret;
    for(const auto& caret:layout.carets){const auto top=map(caret.x,caret.top),bottom=map(caret.x,caret.top+caret.height);overlay.carets.push_back({top.x,top.y,bottom.x,bottom.y});}
    const int start=std::min(session.caret,session.anchor),end=std::max(session.caret,session.anchor);
    if(end>start){
        for(const auto& cluster:layout.clusters){
            if(cluster.start+cluster.length<=start||cluster.start>=end)continue;
            const int clusterEnd=cluster.start+std::max(cluster.length,1);
            const int overlapStart=std::max(start,cluster.start),overlapEnd=std::min(end,clusterEnd);
            if(overlapEnd<=overlapStart)continue;
            const float span=float(std::max(cluster.length,1));
            const float left=cluster.x+cluster.width*float(overlapStart-cluster.start)/span;
            const float right=std::max(left+1.f,cluster.x+cluster.width*float(overlapEnd-cluster.start)/span);
            const float top=cluster.top,bottom=top+std::max(cluster.height,1.f);
            const auto a=map(left,top),b=map(right,top),c=map(right,bottom),d=map(left,bottom);
            overlay.selection.push_back({a.x,a.y,b.x,b.y,c.x,c.y,d.x,d.y});
        }
        if(overlay.selection.empty()&&!layout.carets.empty()){
            const int limit=std::max(0,int(layout.carets.size())-1);
            int index=std::clamp(start,0,limit);const int stop=std::clamp(end,0,limit);
            while(index<stop){
                const int line=layout.carets[size_t(index)].line;int last=index;
                while(last<stop&&last+1<=limit&&layout.carets[size_t(last+1)].line==line)++last;
                const auto& from=layout.carets[size_t(index)];const auto& to=layout.carets[size_t(last)];
                float left=std::min(from.x,to.x),right=std::max(from.x,to.x);
                if(last==index&&index<end){for(const auto& caret:layout.carets)if(caret.line==line)right=std::max(right,caret.x);if(right<=left+0.5f)right=left+std::max(from.height*.4f,1.f);}
                if(right>left+0.5f){
                    const float top=from.top,bottom=top+std::max(from.height,1.f);
                    const auto a=map(left,top),b=map(right,top),c=map(right,bottom),d=map(left,bottom);
                    overlay.selection.push_back({a.x,a.y,b.x,b.y,c.x,c.y,d.x,d.y});
                }
                if(last+1<=index)break;index=last+1;
            }
        }
    }
    overlay.showFrame=true;
    const Point corners[4]={placed.fromUnit({0,0}),placed.fromUnit({1,0}),placed.fromUnit({1,1}),placed.fromUnit({0,1})};
    for(int i=0;i<4;++i)overlay.frame[size_t(i)]=corners[i];
    const auto mid=[](Point a,Point b){return Point{(a.x+b.x)*.5,(a.y+b.y)*.5};};
    overlay.handles={corners[0],mid(corners[0],corners[1]),corners[1],mid(corners[1],corners[2]),corners[2],mid(corners[2],corners[3]),corners[3],mid(corners[3],corners[0])};
    if(existing){
        auto preview=std::make_shared<LayerRenderPreview>();preview->layer=*existing;preview->layer.transform=placed;preview->layer.text=session.style;
        preview->layer.raster=layout.raster?layout.raster:Raster::filled(1,1,{0,0,0,0});
        preview->identity=std::make_shared<int>(0);preview->damageComparedWith=[](const LayerRenderPreview*){return std::nullopt;};
        session.owner->textPreview=std::move(preview);
    }else session.owner->textPreview.reset();
    if(session.owner->canvas)session.owner->canvas->setTextOverlay(std::move(overlay));
    if(session.fontPreviewOriginal){if(session.owner->canvas)session.owner->canvas->update();return;}
    if(session.owner==current())refresh(true,false);
}
void MainWindow::beginText(Point point,bool forceNew,bool extend,int clickCount){
    if(!canEditLayers()||!current()||!current()->document)return;
    auto place=[&](TextSession& session)->bool{
        const auto text=unitsOf(session.style.value);
        const auto unit=textFrame(session.owner,session.layerId,session.originX,session.originY,session.layout.width,session.layout.height).toUnit(point);
        const float x=float(unit.x*std::max(session.layout.width,1)),y=float(unit.y*std::max(session.layout.height,1));
        session.preedit.clear();
        if(clickCount>=2&&!text.isEmpty()){
            if(clickCount>=3){const int index=caretIndex(session.layout,x,y);session.anchor=lineEdge(session.layout,index,true);session.caret=lineEdge(session.layout,index,false);}
            else{const auto range=wordRange(text,characterIndex(session.layout,x,y));session.anchor=range.first;session.caret=range.second;}
            return false;
        }
        const int index=session.layout.carets.empty()?0:caretIndex(session.layout,x,y);session.caret=index;if(!extend)session.anchor=index;
        return clickCount<2&&!text.isEmpty();
    };
    auto publish=[&](bool selecting){auto* owner=current();if(canvas())canvas()->setFocus();publishTextEdit();textSelecting_=selecting;if(selecting)pointerOwner_=owner;};
    if(textSession_&&textSession_->owner==current()&&!forceNew){
        const auto placed=textFrame(textSession_->owner,textSession_->layerId,textSession_->originX,textSession_->originY,textSession_->layout.width,textSession_->layout.height);
        if(containsText(placed,point)&&!textSession_->layout.carets.empty()){publish(place(*textSession_));return;}
    }
    textSelecting_=false;
    if(textSession_&&!finishText())return;
    auto* project=current();if(!project||!project->document)return;
    TextSession session;session.owner=project;session.originX=point.x;session.originY=point.y;session.style=textDefaults_;
    if(!forceNew){
        for(auto layer=project->document->layers.rbegin();layer!=project->document->layers.rend();++layer){
            if(layer->group||!layer->text||!shown(*project->document,layer->id)||!containsText(layer->transform,point))continue;
            session.layerId=layer->id;session.style=*layer->text;session.originX=layer->transform.x;session.originY=layer->transform.y;
            project->active=layer->id;project->selected={layer->id};project->maskSelected=false;break;
        }
    }
    if(session.layerId.empty()){session.style.value.clear();session.style.colorRuns.clear();session.style.fontRuns.clear();session.style.red=foreground_.redF();session.style.green=foreground_.greenF();session.style.blue=foreground_.blueF();session.style.alpha=foreground_.alphaF();}
    try{session.layout=text::layoutText(session.style);}catch(const std::exception& error){statusBar()->showMessage(error.what());return;}
    const bool selecting=session.layerId.empty()?false:place(session);
    if(session.layerId.empty())session.caret=session.anchor=0;
    textSession_=std::move(session);tool_=Tool::Text;publish(selecting);
}
bool MainWindow::editTextAt(Point point,int clickCount){
    if(!canEditLayers()||!current()||!current()->document)return false;
    if(transformSession_){if(transformSession_->persistent||transformSession_->corners)return false;cancelTransformSession();}
    const Layer* hit=nullptr;
    for(auto layer=current()->document->layers.rbegin();layer!=current()->document->layers.rend();++layer){
        if(layer->group||!layer->text||!shown(*current()->document,layer->id)||!containsText(layer->transform,point))continue;
        hit=&*layer;break;
    }
    if(!hit)return false;
    if(textSession_&&!finishText())return false;
    const auto id=hit->id;current()->active=id;current()->selected={id};current()->maskSelected=false;
    beginText(point,false,false,clickCount);
    return textSession_&&textSession_->layerId==id;
}
void MainWindow::beginTextPointer(Point point,Qt::KeyboardModifiers modifiers,int clickCount){
    if(!canEditLayers()||!current()||!current()->document)return;
    textHandle_=hitTextHandle(point);
    if(textHandle_>=0){textResizeStart_=textFrame(textSession_->owner,textSession_->layerId,textSession_->originX,textSession_->originY,textSession_->layout.width,textSession_->layout.height);textResizeChanged_=false;textArmPoint_=point;pointerOwner_=current();return;}
    const bool extend=modifiers.testFlag(Qt::ShiftModifier);
    const bool inside=textSession_&&textSession_->owner==current()&&containsText(textFrame(textSession_->owner,textSession_->layerId,textSession_->originX,textSession_->originY,textSession_->layout.width,textSession_->layout.height),point);
    if(clickCount>=2||extend||inside||hitsVisibleText(*current()->document,point)){beginText(point,false,extend,clickCount);return;}
    textArming_=true;textArmPoint_=point;pointerOwner_=current();
}
void MainWindow::updateTextPointer(Point point,bool finish){
    if(textHandle_>=0){if(!finish)resizeTextBox(point);else{textHandle_=-1;textResizeChanged_=false;}return;}
    if(textArming_){
        if(!finish)return;
        textArming_=false;
        if(std::hypot(point.x-textArmPoint_.x,point.y-textArmPoint_.y)>=4)beginParagraph(textArmPoint_,point);
        else beginText(textArmPoint_,false,false,1);
        return;
    }
    if(finish)textSelecting_=false;
    else if(textSelecting_)updateTextCaret(point);
}
int MainWindow::hitTextHandle(Point point){
    if(!textSession_||textSession_->owner!=current()||textSession_->layout.width<=0)return -1;
    const auto placed=textFrame(textSession_->owner,textSession_->layerId,textSession_->originX,textSession_->originY,textSession_->layout.width,textSession_->layout.height);
    const Point corners[4]={placed.fromUnit({0,0}),placed.fromUnit({1,0}),placed.fromUnit({1,1}),placed.fromUnit({0,1})};
    const auto mid=[](Point a,Point b){return Point{(a.x+b.x)*.5,(a.y+b.y)*.5};};
    const Point handles[8]={corners[0],mid(corners[0],corners[1]),corners[1],mid(corners[1],corners[2]),corners[2],mid(corners[2],corners[3]),corners[3],mid(corners[3],corners[0])};
    int hit=-1;double best=64;
    for(int index=0;index<8;++index){const double distance=std::hypot(point.x-handles[index].x,point.y-handles[index].y);if(distance<best){best=distance;hit=index;}}
    return best<=8?hit:-1;
}
void MainWindow::resizeTextBox(Point point){
    if(!textSession_||textHandle_<0)return;
    double scaleX=1,scaleY=1;
    if(!textSession_->layerId.empty()&&textSession_->owner&&textSession_->owner->document){
        auto found=std::find_if(textSession_->owner->document->layers.begin(),textSession_->owner->document->layers.end(),[&](const Layer& layer){return layer.id==textSession_->layerId;});
        if(found!=textSession_->owner->document->layers.end()&&found->raster&&found->raster->width&&found->raster->height){scaleX=found->transform.width/found->raster->width;scaleY=found->transform.height/found->raster->height;}
    }
    const double fullWidth=std::max(16.,textResizeStart_.width/std::max(1e-6,scaleX)),fullHeight=std::max(16.,textResizeStart_.height/std::max(1e-6,scaleY));
    if(!textResizeChanged_){
        textSession_->undo.push_back({textSession_->style,textSession_->caret,textSession_->anchor});textSession_->redo.clear();
        if(!text::hasTextBox(textSession_->style)){textSession_->style.boxWidth=fullWidth;textSession_->style.boxHeight=fullHeight;}
        textResizeChanged_=true;
    }
    const auto local=textResizeStart_.toUnit(point);
    const int handle=textHandle_;
    const bool west=handle==0||handle==6||handle==7,east=handle==2||handle==3||handle==4,north=handle==0||handle==1||handle==2,south=handle==4||handle==5||handle==6;
    const double minU=std::min(.9,16./fullWidth),minV=std::min(.9,16./fullHeight);
    double left=0,top=0,right=1,bottom=1;
    if(west)left=std::clamp(local.x,-4.,1.-minU);
    if(east)right=std::clamp(local.x,minU,5.);
    if(north)top=std::clamp(local.y,-4.,1.-minV);
    if(south)bottom=std::clamp(local.y,minV,5.);
    if(right-left<minU){if(west)left=right-minU;else right=left+minU;}
    if(bottom-top<minV){if(north)top=bottom-minV;else bottom=top+minV;}
    const auto origin=textResizeStart_.fromUnit({left,top});
    textSession_->originX=origin.x;textSession_->originY=origin.y;
    textSession_->style.boxWidth=std::clamp((right-left)*fullWidth,16.,30000.);
    textSession_->style.boxHeight=std::clamp((bottom-top)*fullHeight,16.,30000.);
    publishTextEdit();
}
void MainWindow::beginParagraph(Point start,Point end){
    if(!canEditLayers()||!current()||!current()->document)return;
    if(textSession_&&!finishText())return;
    auto* project=current();if(!project||!project->document)return;
    TextSession session;session.owner=project;session.originX=std::min(start.x,end.x);session.originY=std::min(start.y,end.y);session.style=textDefaults_;
    session.style.value.clear();session.style.colorRuns.clear();session.style.fontRuns.clear();
    session.style.red=foreground_.redF();session.style.green=foreground_.greenF();session.style.blue=foreground_.blueF();session.style.alpha=foreground_.alphaF();
    session.style.boxWidth=std::clamp(std::abs(end.x-start.x),16.,30000.);session.style.boxHeight=std::clamp(std::abs(end.y-start.y),16.,30000.);
    try{session.layout=text::layoutText(session.style);}catch(const std::exception& error){statusBar()->showMessage(error.what());return;}
    session.caret=session.anchor=0;textSession_=std::move(session);tool_=Tool::Text;
    if(canvas())canvas()->setFocus();publishTextEdit();
}
void MainWindow::updateTextCaret(Point point){
    if(!textSelecting_||!textSession_||textSession_->owner!=current()||textSession_->layout.carets.empty())return;
    const auto unit=textFrame(textSession_->owner,textSession_->layerId,textSession_->originX,textSession_->originY,textSession_->layout.width,textSession_->layout.height).toUnit(point);
    const int index=caretIndex(textSession_->layout,float(unit.x*std::max(textSession_->layout.width,1)),float(unit.y*std::max(textSession_->layout.height,1)));
    if(index==textSession_->caret)return;
    textSession_->caret=index;textSession_->preedit.clear();
    auto* owner=current();publishTextEdit();textSelecting_=true;pointerOwner_=owner;
}
bool MainWindow::finishText(){
    textSelecting_=false;
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
            else{
                const bool sameText=found->text&&*found->text==session.style;
                const bool sameOrigin=std::abs(found->transform.x-session.originX)<1e-4&&std::abs(found->transform.y-session.originY)<1e-4;
                if(sameText&&sameOrigin){clear();if(project==current())refresh(false,false);return true;}
                if(!text::textRunsValid(session.style))throw std::runtime_error("Invalid text");
                auto drawn=text::rasterize(session.style);
                const double scaleX=found->raster&&found->raster->width?found->transform.width/found->raster->width:1;
                const double scaleY=found->raster&&found->raster->height?found->transform.height/found->raster->height:1;
                const auto id=session.layerId;
                const double width=std::max(1.,drawn.width*scaleX),height=std::max(1.,drawn.height*scaleY);
                commit("Edit Text",[&](Document& document){auto layer=std::find_if(document.layers.begin(),document.layers.end(),[&](const Layer& item){return item.id==id;});if(layer==document.layers.end())return;layer->text=session.style;layer->raster=drawn.raster;layer->transform.x=session.originX;layer->transform.y=session.originY;layer->transform.width=width;layer->transform.height=height;});
            }
        }
        textDefaults_.fontFamily=session.style.fontFamily;textDefaults_.fontSize=session.style.fontSize;
        clear();if(project==current())refresh();return true;
    }catch(const std::exception& error){if(!textSession_)textSession_=std::move(session);statusBar()->showMessage(error.what());if(textSession_)publishTextEdit();return false;}
}
void MainWindow::cancelText(){
    textSelecting_=false;
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
std::string selectedStyleName(MainWindow* window,const TextContent& style,int index){
    if(auto* styles=window->findChild<QComboBox*>("textStyle");styles&&styles->currentIndex()>=0){const auto name=styles->currentText().toStdString();if(!name.empty())return name;}
    const auto current=text::styleAt(style,std::max(0,index));
    return current.empty()?"Regular":current;
}
void MainWindow::previewTextFont(const std::string& family){
    if(!textSession_||family.empty())return;
    if(!textSession_->fontPreviewOriginal)textSession_->fontPreviewOriginal=textSession_->style;
    auto next=*textSession_->fontPreviewOriginal;
    const int start=std::min(textSession_->caret,textSession_->anchor),length=std::abs(textSession_->caret-textSession_->anchor);
    text::setTextFont(next,family,selectedStyleName(this,next,length?start:start-1),start,length);
    if(!text::textRunsValid(next))return;
    textSession_->style=std::move(next);publishTextEdit();
}
void MainWindow::previewTextStyle(const std::string& style){
    if(!textSession_||style.empty())return;
    if(!textSession_->fontPreviewOriginal)textSession_->fontPreviewOriginal=textSession_->style;
    auto next=*textSession_->fontPreviewOriginal;
    const int start=std::min(textSession_->caret,textSession_->anchor),length=std::abs(textSession_->caret-textSession_->anchor);
    const auto family=length>0&&!text::uniformFont(next,start,length).empty()?text::uniformFont(next,start,length):text::fontAt(next,length?start:std::max(0,start-1));
    text::setTextFont(next,family.empty()?next.fontFamily:family,style,start,length);
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
    if(family.empty()||family=="Multiple")return;
    if(!textSession_){textDefaults_.fontFamily=family;return;}
    if(textSession_->fontPreviewOriginal)return;
    auto next=textSession_->style;
    const int start=std::min(textSession_->caret,textSession_->anchor),length=std::abs(textSession_->caret-textSession_->anchor);
    text::setTextFont(next,family,selectedStyleName(this,next,length?start:start-1),start,length);
    if(next.fontFamily==textSession_->style.fontFamily&&next.fontStyle==textSession_->style.fontStyle&&next.fontRuns==textSession_->style.fontRuns)return;
    if(!text::textRunsValid(next))return;
    textSession_->undo.push_back({textSession_->style,textSession_->caret,textSession_->anchor});textSession_->redo.clear();
    textSession_->style=std::move(next);publishTextEdit();
}
void MainWindow::applyTextStyle(const std::string& style){
    if(!textSession_||style.empty()||style=="Multiple"||textSession_->fontPreviewOriginal)return;
    auto next=textSession_->style;
    const int start=std::min(textSession_->caret,textSession_->anchor),length=std::abs(textSession_->caret-textSession_->anchor);
    const auto family=length>0&&!text::uniformFont(next,start,length).empty()?text::uniformFont(next,start,length):text::fontAt(next,length?start:std::max(0,start-1));
    text::setTextFont(next,family.empty()?next.fontFamily:family,style,start,length);
    if(next.fontStyle==textSession_->style.fontStyle&&next.fontRuns==textSession_->style.fontRuns)return;
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
void MainWindow::previewTextColor(double red,double green,double blue){
    if(!textSession_)return;
    text::setTextColor(textSession_->style,red,green,blue,std::min(textSession_->caret,textSession_->anchor),std::abs(textSession_->caret-textSession_->anchor));
    publishTextEdit();
}
void MainWindow::applyTextAlignment(TextAlignment alignment){if(!textSession_){textDefaults_.alignment=alignment;return;}endTextFontPreview();if(textSession_->style.alignment==alignment)return;textSession_->undo.push_back({textSession_->style,textSession_->caret,textSession_->anchor});textSession_->redo.clear();textSession_->style.alignment=alignment;publishTextEdit();}
void MainWindow::applyTextTracking(double tracking){if(!textSession_)return;endTextFontPreview();tracking=std::clamp(tracking,-100.,1000.);if(textSession_->style.tracking==tracking)return;textSession_->undo.push_back({textSession_->style,textSession_->caret,textSession_->anchor});textSession_->redo.clear();textSession_->style.tracking=tracking;publishTextEdit();}
void MainWindow::applyTextLeading(double leading){if(!textSession_)return;endTextFontPreview();leading=std::clamp(leading,0.,5000.);if(textSession_->style.leading==leading)return;textSession_->undo.push_back({textSession_->style,textSession_->caret,textSession_->anchor});textSession_->redo.clear();textSession_->style.leading=leading;publishTextEdit();}
void MainWindow::focusTextCanvas(){if(canvas())canvas()->setFocus(Qt::OtherFocusReason);}
void MainWindow::showTextPointer(){while(QApplication::overrideCursor()&&QApplication::overrideCursor()->shape()==Qt::BlankCursor)QApplication::restoreOverrideCursor();if(auto* view=canvas())view->setCursor(Qt::IBeamCursor);}
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
    showTextPointer();
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
    if(alt&&!control&&(key==Qt::Key_Left||key==Qt::Key_Right||key==Qt::Key_Up||key==Qt::Key_Down)){
        const double step=shift?10.:1.;
        if(key==Qt::Key_Left||key==Qt::Key_Right)applyTextTracking(textSession_->style.tracking+(key==Qt::Key_Right?step:-step));
        else{const double current=textSession_->style.leading>0?textSession_->style.leading:text::lineHeight(textSession_->style);applyTextLeading(key==Qt::Key_Up?std::max(1.,current-step):std::min(5000.,current+step));}
        showTextPointer();return true;
    }
    if(control&&!alt&&(key==Qt::Key_Left||key==Qt::Key_Right)){move(key==Qt::Key_Left?wordBackward(text,textSession_->caret):wordForward(text,textSession_->caret));showTextPointer();return true;}
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

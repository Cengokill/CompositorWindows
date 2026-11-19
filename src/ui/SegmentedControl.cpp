#include "SegmentedControl.h"
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QToolButton>

namespace compositor::ui {
SegmentedControl::SegmentedControl(const QStringList& items, QWidget* parent):QWidget(parent),group_(new QButtonGroup(this)){
    // The look comes from VisualStyle.cpp through these properties; a plain QWidget needs a styled background to paint it.
    setProperty("segmentedControl",true);setAttribute(Qt::WA_StyledBackground);
    auto* row=new QHBoxLayout(this);row->setContentsMargins(2,2,2,2);row->setSpacing(2);
    group_->setExclusive(true);
    for(int i=0;i<items.size();++i){
        auto* button=new QToolButton(this);button->setText(items[i]);button->setAccessibleName(items[i]);button->setCheckable(true);button->setProperty("segment",true);
        group_->addButton(button,i);row->addWidget(button);
    }
    if(auto* first=group_->button(0))first->setChecked(true);
    connect(group_,&QButtonGroup::idClicked,this,[this](int index){if(index==shown_)return;shown_=index;if(changed)changed(index);});
}
int SegmentedControl::currentIndex() const{return group_->checkedId();}
void SegmentedControl::setCurrentIndex(int index){if(auto* button=group_->button(index)){button->setChecked(true);shown_=index;}}
void SegmentedControl::choose(int index){if(!group_->button(index)||index==shown_)return;setCurrentIndex(index);if(changed)changed(index);}
QToolButton* SegmentedControl::segment(int index) const{return qobject_cast<QToolButton*>(group_->button(index));}
SegmentedControl* findSegmentedControl(const QObject* root,const char* objectName){return dynamic_cast<SegmentedControl*>(root->findChild<QWidget*>(objectName));}
}

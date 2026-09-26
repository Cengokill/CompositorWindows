#pragma once
#include <QEvent>
#include <QHBoxLayout>
#include <QLayout>
#include <QResizeEvent>
#include <QWidget>
#include <algorithm>
#include <initializer_list>
#include <vector>

namespace compositor::ui {
// Keep each labelled control together while permitting options to wrap inside
// the toolbar's available width. Hidden family options consume no row space.
class ToolOptionsFlow final : public QLayout {
public:
    explicit ToolOptionsFlow(QWidget* parent):QLayout(parent){setContentsMargins(4,2,4,2);setSpacing(6);}
    ~ToolOptionsFlow() override {while(auto* item=takeAt(0))delete item;}
    void addItem(QLayoutItem* item) override {items_.push_back(item);invalidate();}
    int count() const override {return int(items_.size());}
    QLayoutItem* itemAt(int index) const override {return index>=0&&size_t(index)<items_.size()?items_[size_t(index)]:nullptr;}
    QLayoutItem* takeAt(int index) override {if(index<0||size_t(index)>=items_.size())return nullptr;auto* item=items_[size_t(index)];items_.erase(items_.begin()+index);invalidate();return item;}
    Qt::Orientations expandingDirections() const override {return Qt::Horizontal;}
    bool hasHeightForWidth() const override {return true;}
    int heightForWidth(int width) const override {return arrange(QRect(0,0,width,0),false);}
    QSize minimumSize() const override {
        QSize size;for(const auto* item:items_)if(!item->isEmpty())size=size.expandedTo(item->minimumSize());
        const auto margins=contentsMargins();return size+QSize(margins.left()+margins.right(),margins.top()+margins.bottom());
    }
    QSize sizeHint() const override {const int width=std::max(640,minimumSize().width());return {width,heightForWidth(width)};}
    void setGeometry(const QRect& rect) override {QLayout::setGeometry(rect);arrange(rect,true);}
private:
    std::vector<QLayoutItem*> items_;
    int arrange(const QRect& rect,bool place) const {
        const auto margins=contentsMargins();const auto area=rect.adjusted(margins.left(),margins.top(),-margins.right(),-margins.bottom());
        int x=area.left(),y=area.top(),rowHeight=0;
        for(auto* item:items_){if(item->isEmpty())continue;const auto size=item->sizeHint().expandedTo(item->minimumSize());
            if(x>area.left()&&x+size.width()>area.right()+1){x=area.left();y+=rowHeight+spacing();rowHeight=0;}
            if(place)item->setGeometry(QRect(QPoint(x,y),size));x+=size.width()+spacing();rowHeight=std::max(rowHeight,size.height());
        }
        return y+rowHeight-rect.top()+margins.bottom();
    }
};

class WrappingToolOptions final : public QWidget {
public:
    explicit WrappingToolOptions(QWidget* parent=nullptr):QWidget(parent){flow_=new ToolOptionsFlow(this);setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);}
    void addControl(QWidget* widget){flow_->addWidget(widget);}
    void addGroup(std::initializer_list<QWidget*> widgets){
        auto* group=new QWidget(this);auto* row=new QHBoxLayout(group);row->setContentsMargins(0,0,0,0);row->setSpacing(4);
        for(auto* widget:widgets)row->addWidget(widget);flow_->addWidget(group);
    }
protected:
    void resizeEvent(QResizeEvent* event) override {QWidget::resizeEvent(event);syncHeight();}
    bool event(QEvent* event) override {const bool result=QWidget::event(event);if(event->type()==QEvent::LayoutRequest||event->type()==QEvent::Show)syncHeight();return result;}
private:
    ToolOptionsFlow* flow_{};
    void syncHeight(){if(!flow_)return;const int needed=flow_->heightForWidth(width());if(minimumHeight()!=needed||maximumHeight()!=needed){setFixedHeight(needed);updateGeometry();}}
};
}

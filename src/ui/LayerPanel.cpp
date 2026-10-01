#include "LayerPanel.h"
#include "EditorIcons.h"
#include <QStyledItemDelegate>
#include "LayerAccessibility.h"
#include "ProjectLayerDrag.h"
#include "graphics/RasterSampling.h"
#include <QApplication>
#include <QComboBox>
#include <QDrag>
#include <QDragEnterEvent>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QCursor>
#include <QKeyEvent>
#include <QStyleOptionViewItem>
#include <QScrollBar>
#include <cmath>
#include <QSignalBlocker>
#include <QUuid>
#include <algorithm>

namespace compositor::ui {
namespace {
constexpr auto layerMime="application/x-compositor-layer-ids";
constexpr auto maskMime="application/x-compositor-mask-id";
constexpr auto ownerMime="application/x-compositor-panel-owner";
const Layer* findLayer(const Document& d,const std::string& id){for(const auto& l:d.layers)if(l.id==id)return &l;return nullptr;}
std::string idOf(const QTreeWidgetItem* item){return item?item->data(0,Qt::UserRole).toString().toStdString():std::string{};}
void nameLayerControls(QTreeWidgetItem& item,const Layer& layer){
    const auto name=QString::fromStdString(layer.name);
    // NativeLayerList.swift:569-586 gives each thumbnail and link its own name.
    item.setData(1,Qt::AccessibleTextRole,"Select image: "+name);
    item.setData(1,Qt::AccessibleDescriptionRole,item.toolTip(1));
    if(layer.mask){
        item.setData(2,Qt::AccessibleTextRole,(layer.mask->linked?"Unlink mask: ":"Link mask: ")+name);
        item.setData(2,Qt::AccessibleDescriptionRole,item.toolTip(2));
        item.setData(3,Qt::AccessibleTextRole,"Select mask: "+name);
        item.setData(3,Qt::AccessibleDescriptionRole,item.toolTip(3));
    }
    // Retain the row name; LayerAccessibility exposes its checkbox separately.
    item.setData(0,Qt::AccessibleDescriptionRole,(layer.visible?"Hide ":"Show ")+name);
}
std::vector<std::string> idsOf(const QMimeData* mime){std::vector<std::string> ids;for(const auto& value:QJsonDocument::fromJson(mime->data(layerMime)).array())ids.push_back(value.toString().toStdString());return ids;}
QTreeWidgetItem* itemWithId(QTreeWidget* tree,const std::string& id){QTreeWidgetItemIterator it(tree);while(*it){if(idOf(*it)==id)return *it;++it;}return nullptr;}
struct Destination{std::string id;DropZone zone;};
Destination destination(QTreeWidget* tree,const Document& d,const QPoint& point){
    const auto* item=tree->itemAt(point);if(!item)return {{},DropZone::Bottom};
    const auto id=idOf(item);const auto* layer=findLayer(d,id);const auto rect=tree->visualItemRect(item);
    if(layer&&layer->group&&point.y()>=rect.top()+rect.height()/4&&point.y()<rect.bottom()-rect.height()/4)return {id,DropZone::IntoGroup};
    // Every ordinary row is an insertion-above target, matching NativeLayerList.
    return {id,DropZone::Above};
}

}

QSize canvasThumbnailSize(QSizeF canvas,double box){
    if(!std::isfinite(box)||box<1||box>4096)throw std::invalid_argument("Invalid thumbnail box");
    if(canvas.width()<=0||canvas.height()<=0||!std::isfinite(canvas.width())||!std::isfinite(canvas.height()))return {int(std::lround(box)),int(std::lround(box))};
    const auto scale=box/std::max(canvas.width(),canvas.height());return {std::max(1,int(std::lround(canvas.width()*scale))),std::max(1,int(std::lround(canvas.height()*scale)))};
}
QImage canvasLayerThumbnail(const Raster* raster,const Transform& transform,QSizeF canvas,double box){
    const auto size=canvasThumbnailSize(canvas,box)*2;QImage image(size,QImage::Format_ARGB32_Premultiplied);const double scale=canvas.width()>0?size.width()/canvas.width():1.;
    for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x){const int background=((x/12+y/12)%2==0)?82:56;Pixel sample{};if(raster&&transform.valid())sample=graphics::sampleRaster(*raster,transform.toUnit({(x+.5)/scale,(y+.5)/scale}),transform.sampling);const auto channel=[&](uint8_t value){return value+(background*(255-sample.a)+127)/255;};image.setPixel(x,y,qRgb(channel(sample.r),channel(sample.g),channel(sample.b)));}
    image.setDevicePixelRatio(2);return image;
}
double canvasMaskEdgeTone(const GrayRaster& raster){
    if(raster.width<=0||raster.height<=0)return 1.;uint64_t sum=0,count=0;
    for(int y=0;y<raster.height;++y){const bool row=y==0||y==raster.height-1;if(row){for(int x=0;x<raster.width;++x){sum+=raster.pixel(x,y);++count;}}else{sum+=raster.pixel(0,y);++count;if(raster.width>1){sum+=raster.pixel(raster.width-1,y);++count;}}}
    return count?double(sum)/double(count)/255.:1.;
}
QImage canvasMaskThumbnail(const GrayRaster&raster,const Transform&transform,QSizeF canvas,double box){
    const auto size=canvasThumbnailSize(canvas,box)*2;QImage image(size,QImage::Format_ARGB32_Premultiplied);const auto exterior=uint8_t(std::clamp(std::lround(canvasMaskEdgeTone(raster)*255),0L,255L));const double scale=canvas.width()>0?size.width()/canvas.width():1.;
    for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x){const auto unit=transform.valid()?transform.toUnit({(x+.5)/scale,(y+.5)/scale}):Point{-1,-1};const int value=int(std::clamp(std::lround(graphics::sampleGray(raster,unit,transform.sampling,exterior)*255),0L,255L));image.setPixel(x,y,qRgb(value,value,value));}
    image.setDevicePixelRatio(2);return image;
}
struct LayerPanelController::ThumbnailCache{
    struct Entry{std::shared_ptr<const Raster> raster;std::shared_ptr<const GrayRaster> mask;Transform transform;QSizeF canvas;bool group{},enabled{};std::string adjustment;QIcon icon;};
    std::map<std::pair<std::string,bool>,Entry> entries;
};
QIcon LayerPanelController::thumbnail(const Layer&layer,bool mask,QSizeF canvas){
    auto&cache=thumbnailCache_->entries;const auto key=std::pair{layer.id,mask};const auto raster=mask?nullptr:layer.raster;const auto gray=mask&&layer.mask?layer.mask->raster:nullptr;const auto transform=mask&&layer.mask?layer.mask->placement.value_or(layer.transform):layer.transform;const bool enabled=!mask||!layer.mask||layer.mask->enabled;
    auto found=cache.find(key);if(found!=cache.end()){const auto&entry=found->second;if(entry.raster==raster&&entry.mask==gray&&entry.transform==transform&&entry.canvas==canvas&&entry.group==layer.group&&entry.adjustment==layer.adjustmentJson&&entry.enabled==enabled)return entry.icon;}
    QImage image;if(mask&&gray){image=canvasMaskThumbnail(*gray,transform,canvas);if(!enabled){QPainter painter(&image);painter.setPen(QPen(QColor(205,50,45),2));const auto w=image.width()/2.,h=image.height()/2.;painter.drawLine(QPointF(2,2),QPointF(w-2,h-2));painter.drawLine(QPointF(w-2,2),QPointF(2,h-2));}}
    else if(layer.group||!layer.adjustmentJson.empty()){image=QImage(72,72,QImage::Format_ARGB32_Premultiplied);image.setDevicePixelRatio(2);image.fill(Qt::transparent);QPainter painter(&image);editorIcon(layer.group?EditorIcon::Folder:EditorIcon::Gradient).paint(&painter,QRect(6,6,24,24));}
    else image=canvasLayerThumbnail(raster.get(),transform,canvas);
    QIcon icon(QPixmap::fromImage(image));cache[key]={raster,gray,transform,canvas,layer.group,enabled,layer.adjustmentJson,icon};++thumbnailRenderCount_;return icon;
}

layers::EditResult dropLayers(const Document& d,layers::SelectionState selection,const std::vector<std::string>& requested,const std::string& target,DropZone zone,bool copy){
    validateDocument(d);selection=layers::normalizeSelection(d,std::move(selection));
    const std::unordered_set<std::string> wanted(requested.begin(),requested.end());std::unordered_set<std::string> carried;
    for(const auto& id:requested){if(!findLayer(d,id))throw std::runtime_error("Dragged layer no longer exists");auto children=layers::descendants(d,id);carried.insert(children.begin(),children.end());}
    std::vector<std::string> ids;for(const auto& entry:layers::entries(d,true))if(wanted.contains(entry.id)&&!carried.contains(entry.id))ids.push_back(entry.id);
    if(ids.empty())return {d,selection,false,"Move Layers"};
    layers::Placement placement;
    if(zone==DropZone::IntoGroup){const auto*l=findLayer(d,target);if(!l||!l->group)throw std::runtime_error("Drop target is not a group");placement.parent=target;}
    else if(zone==DropZone::Bottom)placement.atBottom=true;
    else{const auto*l=findLayer(d,target);if(!l)throw std::runtime_error("Drop target no longer exists");placement.parent=l->parentId;placement.above=target;}
    for(const auto& id:ids){if(!layers::canPlace(d,id,placement.parent))throw std::runtime_error("Layer cannot be dropped here");if(copy&&findLayer(d,id)->group)throw std::runtime_error("Folder duplication is not supported by this source operation");}
    auto order=ids;if(zone==DropZone::IntoGroup)std::reverse(order.begin(),order.end());Document next=d;layers::SelectionState after=selection;
    for(const auto& id:order){if(!copy&&id==placement.above)continue;auto changed=copy?layers::duplicateLayer(next,after,id,placement):layers::place(next,after,id,placement);next=std::move(changed.document);after=std::move(changed.selection);}
    if(!copy)after={ids,ids.front()};
    const bool changed=next!=d;return {std::move(next),std::move(after),changed,copy?(ids.size()>1?"Duplicate Layers":"Duplicate Layer"):(ids.size()>1?"Move Layers":"Move Layer")};
}

namespace {
class LayerRowDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter,const QStyleOptionViewItem& option,const QModelIndex& index) const override {
        QStyleOptionViewItem opt(option);initStyleOption(&opt,index);
        const bool activeTarget=index.data(Qt::BackgroundRole).value<QBrush>().style()!=Qt::NoBrush;
        opt.backgroundBrush=Qt::NoBrush;opt.state&=~QStyle::State_HasFocus;
        if(index.column()==0) {
            const auto text=opt.text;opt.text.clear();opt.features&=~QStyleOptionViewItem::HasCheckIndicator;opt.widget->style()->drawControl(QStyle::CE_ItemViewItem,&opt,painter,opt.widget);
            // Keep the model's name/editing/accessibility roles intact; only paint changes.
            auto r=opt.rect.adjusted(5,5,-5,-4);
            painter->save();painter->setPen(opt.palette.color(QPalette::Text));
            painter->drawText(r.adjusted(0,0,0,-16),Qt::AlignVCenter,opt.fontMetrics.elidedText(text,Qt::ElideRight,r.width()));
            auto font=opt.font;font.setPixelSize(10);painter->setFont(font);painter->setPen(QColor("#969ba6"));
            painter->drawText(r.adjusted(0,18,0,0),Qt::AlignVCenter,index.data(Qt::UserRole+2).toString());painter->restore();return;
        }
        if(index.column()==4) {
            opt.text.clear();opt.widget->style()->drawControl(QStyle::CE_ItemViewItem,&opt,painter,opt.widget);
            QStyleOption check;check.initFrom(opt.widget);check.rect=QRect(opt.rect.center()-QPoint(8,8),QSize(16,16));
            check.state&=~QStyle::State_On;if(index.siblingAtColumn(0).data(Qt::CheckStateRole).toInt()==Qt::Checked)check.state|=QStyle::State_On;
            opt.widget->style()->drawPrimitive(QStyle::PE_IndicatorItemViewItemCheck,&check,painter,opt.widget);return;
        }
        if(index.column()==2) {
            const auto icon=opt.icon;opt.icon={};opt.text.clear();opt.widget->style()->drawControl(QStyle::CE_ItemViewItem,&opt,painter,opt.widget);
            icon.paint(painter,QRect(opt.rect.center()-QPoint(7,7),QSize(14,14)));return;
        }
        if(index.column()==1||index.column()==3) {
            const auto icon=opt.icon;opt.icon={};opt.text.clear();opt.widget->style()->drawControl(QStyle::CE_ItemViewItem,&opt,painter,opt.widget);
            if(!icon.isNull()) {
                const QRect box(opt.rect.center()-QPoint(15,15),QSize(30,30));icon.paint(painter,box);
                painter->save();painter->setRenderHint(QPainter::Antialiasing);painter->setBrush(Qt::NoBrush);
                painter->setPen(QPen(activeTarget?QColor("#72aafa"):QColor("#50545e"),activeTarget?1.5:1));painter->drawRoundedRect(QRectF(box).adjusted(-1,-1,1,1),3,3);painter->restore();
            }return;
        }
        opt.widget->style()->drawControl(QStyle::CE_ItemViewItem,&opt,painter,opt.widget);
    }
};
}
LayerPanelController::LayerPanelController(QTreeWidget* tree,Host host):QObject(tree),tree_(tree),host_(std::move(host)),token_(QUuid::createUuid().toByteArray()),thumbnailCache_(std::make_unique<ThumbnailCache>()){
    setObjectName("layerPanelController");tree_->setObjectName("layersTree");tree_->setColumnCount(5);tree_->setHeaderLabels({"Layer","Image","Link","Mask"});tree_->setHeaderHidden(true);tree_->setItemDelegate(new LayerRowDelegate(tree_));tree_->setIndentation(12);tree_->setIconSize({36,36});tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);tree_->setEditTriggers(QAbstractItemView::EditKeyPressed);tree_->setSelectionBehavior(QAbstractItemView::SelectRows);tree_->setDragDropMode(QAbstractItemView::NoDragDrop);tree_->setAcceptDrops(true);tree_->viewport()->setAcceptDrops(true);tree_->viewport()->installEventFilter(this);tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    tree_->header()->setStretchLastSection(false);tree_->header()->setMinimumSectionSize(12);
    tree_->header()->setSectionResizeMode(4,QHeaderView::Fixed);tree_->setColumnWidth(4,24);
    tree_->header()->moveSection(tree_->header()->visualIndex(4),0);
    tree_->header()->moveSection(tree_->header()->visualIndex(0),4);
    tree_->header()->setSectionResizeMode(0,QHeaderView::Stretch);for(int col=1;col<4;++col){tree_->header()->setSectionResizeMode(col,QHeaderView::Fixed);tree_->setColumnWidth(col,col==2?18:38);}tree_->setMinimumWidth(270);
    connect(tree_,&QTreeWidget::itemSelectionChanged,this,[this]{selectionChanged();});
    connect(tree_,&QTreeWidget::itemClicked,this,[this](QTreeWidgetItem* item,int col){if(!rebuilding_&&col==0&&item&&(!host_.canEdit||host_.canEdit())){auto s=host_.state();if(s.maskSelected)host_.select(s.selection,false);}});
    connect(tree_,&QTreeWidget::itemCollapsed,this,[this](QTreeWidgetItem* item){if(!rebuilding_){if(host_.canEdit&&!host_.canEdit()){updateSelection();return;}invoke([&]{host_.collapse(idOf(item),true);});}});
    connect(tree_,&QTreeWidget::itemExpanded,this,[this](QTreeWidgetItem* item){if(!rebuilding_){if(host_.canEdit&&!host_.canEdit()){updateSelection();return;}invoke([&]{host_.collapse(idOf(item),false);});}});
    connect(tree_,&QTreeWidget::customContextMenuRequested,this,[this](QPoint p){if(host_.canEdit&&!host_.canEdit())return;invoke([&]{contextMenu(p);});});
    tree_->setMouseTracking(true);tree_->viewport()->setMouseTracking(true);tree_->viewport()->setCursor(Qt::ArrowCursor);qApp->installEventFilter(this);
    installLayerAccessibility(tree_);
}
LayerPanelController::~LayerPanelController(){if(qApp)qApp->removeEventFilter(this);host_={};}
QRect LayerPanelController::checkRect(const QTreeWidgetItem*item)const{
    if(!item)return {};
    const auto row=tree_->visualRect(tree_->indexFromItem(item,4));
    return QRect(row.center()-QPoint(8,8),QSize(16,16));
}

std::vector<std::string> LayerPanelController::exposedVisibilityControls() const {
    std::vector<std::string> ids;
    for(QTreeWidgetItemIterator it(tree_);*it;++it){
        bool exposed=!(*it)->isHidden();
        for(auto* parent=(*it)->parent();parent;parent=parent->parent())exposed=exposed&&parent->isExpanded()&&!parent->isHidden();
        if(exposed)ids.push_back(idOf(*it));
    }
    return ids;
}
std::optional<LayerVisibilityControl> LayerPanelController::visibilityControl(const std::string& id) const {
    if(!host_.state)return {};
    const auto state=host_.state();const auto* layer=state.document?findLayer(*state.document,id):nullptr;
    const auto* item=itemWithId(tree_,id);if(!layer||!item)return {};
    bool exposed=tree_->isVisible()&&!item->isHidden();
    for(auto* parent=item->parent();parent;parent=parent->parent())exposed=exposed&&parent->isExpanded()&&!parent->isHidden();
    auto rect=exposed?checkRect(item).intersected(tree_->viewport()->rect()):QRect{};
    if(!rect.isEmpty())rect.translate(tree_->viewport()->mapToGlobal(QPoint(0,0)));
    return LayerVisibilityControl{(layer->visible?QStringLiteral("Hide "):QStringLiteral("Show "))+QString::fromStdString(layer->name),rect,layer->visible,tree_->isEnabled()&&(!host_.canEdit||host_.canEdit()),exposed};
}
void LayerPanelController::toggleVisibilityControl(const std::string& id) {
    const auto control=visibilityControl(id);if(!control||!control->enabled||!control->exposed)return;
    invoke([&]{if(!host_.beginVisibilitySwipe||!host_.endVisibilitySwipe)return;
        if(host_.canEdit&&!host_.canEdit())return;
        if(host_.beginVisibilitySwipe(id))host_.endVisibilitySwipe();});
    updateLayerAccessibility(tree_);
}
std::vector<std::pair<std::string,LayerControlKind>> LayerPanelController::exposedActionControls() const {
    std::vector<std::pair<std::string,LayerControlKind>> controls;
    for(const auto& id:exposedVisibilityControls())
        for(const auto kind:{LayerControlKind::Image,LayerControlKind::Mask,LayerControlKind::Link})
            if(actionControl(id,kind))controls.emplace_back(id,kind);
    return controls;
}
std::optional<LayerActionControl> LayerPanelController::actionControl(const std::string& id,LayerControlKind kind) const {
    if(!host_.state)return {};
    const auto state=host_.state();const auto* layer=state.document?findLayer(*state.document,id):nullptr;
    const auto* item=itemWithId(tree_,id);if(!layer||!item)return {};
    // NativeLayerList.swift:554-580: every layer has an image target, masks
    // have a target of their own, and only framed layers expose the link.
    if(kind!=LayerControlKind::Image&&!layer->mask)return {};
    if(kind==LayerControlKind::Link&&(layer->group||!layer->adjustmentJson.empty()))return {};
    const int column=kind==LayerControlKind::Image?1:kind==LayerControlKind::Mask?3:2;
    bool exposed=tree_->isVisible()&&!item->isHidden();
    for(auto* parent=item->parent();parent;parent=parent->parent())exposed=exposed&&parent->isExpanded()&&!parent->isHidden();
    const auto row=tree_->visualItemRect(item);
    QRect rect=exposed?QRect(tree_->columnViewportPosition(column),row.top(),tree_->columnWidth(column),row.height()).intersected(tree_->viewport()->rect()):QRect{};
    if(!rect.isEmpty())rect.translate(tree_->viewport()->mapToGlobal(QPoint(0,0)));
    const auto prefix=kind==LayerControlKind::Image?QStringLiteral("Select image: "):
        kind==LayerControlKind::Mask?QStringLiteral("Select mask: "):
        layer->mask->linked?QStringLiteral("Unlink mask: "):QStringLiteral("Link mask: ");
    return LayerActionControl{prefix+QString::fromStdString(layer->name),item->toolTip(column),rect,
        tree_->isEnabled()&&(host_.targetEnabled?host_.targetEnabled():!host_.canEdit||host_.canEdit()),exposed};
}
void LayerPanelController::invokeActionControl(std::string id,LayerControlKind kind) {
    const auto initial=actionControl(id,kind);if(!initial||!initial->enabled||!initial->exposed)return;
    if(kind!=LayerControlKind::Link&&host_.selectTarget){
        try{host_.selectTarget(id,kind==LayerControlKind::Mask);}
        catch(const std::exception& error){if(host_.error)host_.error(QString::fromUtf8(error.what()));}
        updateLayerAccessibility(tree_);return;
    }
    // Source link buttons remain present/enabled alongside thumbnails, but
    // toggleMaskLink refuses structural edits while a draft/editor is open.
    if(host_.canEdit&&!host_.canEdit())return;
    invoke([&]{
        // Preparation can settle an edit and rebuild the row. Resolve the ID
        // and guards again before consulting the current immutable document.
        const auto control=actionControl(id,kind);if(!control||!control->enabled||!control->exposed)return;
        const auto state=host_.state();if(!state.document)return;
        if(kind==LayerControlKind::Link){if(host_.commit)host_.commit(editMask(*state.document,state.selection,id,MaskCommand::ToggleLink),state.maskSelected);}
        else if(host_.select)host_.select({{id},id},kind==LayerControlKind::Mask);
    });
    updateLayerAccessibility(tree_);
}
void LayerPanelController::finishVisibilitySwipe(){
    if(!visibilitySwipe_)return;visibilitySwipe_.reset();if(QWidget::mouseGrabber()==tree_->viewport())tree_->viewport()->releaseMouse();if(host_.endVisibilitySwipe)host_.endVisibilitySwipe();
}
void LayerPanelController::refreshCursor(Qt::KeyboardModifiers modifiers,const QPoint&point){
    auto*viewport=tree_->viewport();const bool inside=viewport->rect().contains(point);if(!inside){if(modifierCursor_){viewport->setCursor(Qt::ArrowCursor);viewport->setProperty("layerCursorRole","arrow");modifierCursor_=false;}return;}
    const auto state=host_.state();auto*item=tree_->itemAt(point);const auto*layer=item&&state.document?findLayer(*state.document,idOf(item)):nullptr;const bool editable=tree_->isEnabled()&&(!host_.canEdit||host_.canEdit());const int column=tree_->columnAt(point.x());
    QString role="arrow";Qt::CursorShape shape=Qt::ArrowCursor;
    const bool mask=layer&&column==3&&layer->mask.has_value();const bool image=layer&&column==1&&bool(layer->raster);
    if(editable&&modifiers.testFlag(Qt::ControlModifier)&&(mask||image)){role="load-selection";shape=Qt::CrossCursor;}
    else if(editable&&layer&&modifiers.testFlag(Qt::AltModifier)&&!modifiers.testFlag(Qt::ControlModifier)){
        const auto rect=tree_->visualItemRect(item);const bool clipping=!mask&&point.y()>=rect.bottom()+1-rect.height()/4;
        if(clipping&&layers::canToggleClipping(*state.document,layer->id)){role=layer->maskSourceId.empty()?"create-clipping":"release-clipping";shape=Qt::CrossCursor;}
        else if(!clipping&&(mask||!layer->group)){role="duplicate";shape=Qt::DragCopyCursor;}
    }
    viewport->setProperty("layerCursorRole",role);viewport->setCursor(shape);modifierCursor_=role!="arrow";
}
LayerPanelController* LayerPanelController::find(QTreeWidget* tree){for(auto* child:tree->children())if(child->objectName()=="layerPanelController")return dynamic_cast<LayerPanelController*>(child);return nullptr;}
void LayerPanelController::invoke(const std::function<void()>& fn){try{if(host_.prepare)host_.prepare();fn();}catch(const std::exception&e){if(host_.error)host_.error(QString::fromUtf8(e.what()));}}
void LayerPanelController::selectionChanged(){if(rebuilding_)return;if(host_.canEdit&&!host_.canEdit()){updateSelection();return;}invoke([&]{layers::SelectionState selected;for(QTreeWidgetItemIterator it(tree_);*it;++it)if((*it)->isSelected())selected.ids.push_back(idOf(*it));selected.primary=idOf(tree_->currentItem());if(std::find(selected.ids.begin(),selected.ids.end(),selected.primary)==selected.ids.end())selected.primary=selected.ids.empty()?std::string{}:selected.ids.front();host_.select(std::move(selected),false);});}
void LayerPanelController::updateSelection(){const auto state=host_.state();const bool editable=!host_.canEdit||host_.canEdit();QSignalBlocker block(tree_);for(QTreeWidgetItemIterator it(tree_);*it;++it){auto* item=*it;auto flags=item->flags();flags.setFlag(Qt::ItemIsEditable,editable);flags.setFlag(Qt::ItemIsUserCheckable,editable);flags.setFlag(Qt::ItemIsSelectable,editable);item->setFlags(flags);const auto id=idOf(item);item->setSelected(std::find(state.selection.ids.begin(),state.selection.ids.end(),id)!=state.selection.ids.end());item->setExpanded(!state.collapsed.contains(id));if(id==state.selection.primary)tree_->setCurrentItem(item,0,QItemSelectionModel::NoUpdate);item->setBackground(1,id==state.selection.primary&&!state.maskSelected?QBrush(QColor(85,125,165)):QBrush());item->setBackground(3,id==state.selection.primary&&state.maskSelected?QBrush(QColor(85,125,165)):QBrush());}if(auto* combo=tree_->parentWidget()->findChild<QComboBox*>("layerEditTarget")){QSignalBlocker comboBlock(combo);const auto*l=state.document?findLayer(*state.document,state.selection.primary):nullptr;combo->setEnabled(l&&l->mask&&state.selection.ids.size()==1&&(!host_.targetEnabled||host_.targetEnabled()));combo->setCurrentIndex(state.maskSelected?1:0);}}
void LayerPanelController::rebuild(){rebuilding_=true;QSignalBlocker block(tree_);tree_->clear();const auto state=host_.state();if(state.document){std::unordered_map<std::string,QTreeWidgetItem*> items;for(const auto& layer:state.document->layers){auto* item=new QTreeWidgetItem({QString::fromStdString(layer.name)});item->setData(0,Qt::UserRole,QString::fromStdString(layer.id));item->setData(0,Qt::UserRole+2,layer.group?QString("Folder"):!layer.adjustmentJson.empty()?QString("Adjustment"):QString("%1 × %2 px").arg(layer.raster?layer.raster->width:int(layer.transform.width)).arg(layer.raster?layer.raster->height:int(layer.transform.height)));item->setFlags(item->flags()|Qt::ItemIsUserCheckable|Qt::ItemIsEditable);item->setCheckState(0,layer.visible?Qt::Checked:Qt::Unchecked);item->setIcon(1,thumbnail(layer,false,{double(state.document->width),double(state.document->height)}));item->setToolTip(1,"Select image; Ctrl-click selects alpha, Ctrl+Shift adds, Ctrl+Alt subtracts");if(layer.mask){item->setIcon(3,thumbnail(layer,true,{double(state.document->width),double(state.document->height)}));if(layer.mask->linked)item->setIcon(2,editorIcon(EditorIcon::Link));item->setToolTip(2,layer.mask->linked?"Unlink mask":"Link mask");item->setToolTip(3,"Select mask; Shift-click enables/disables; Ctrl-click selects black areas; Alt-drag copies mask");}if(!layer.maskSourceId.empty())item->setToolTip(0,"Clipped to "+QString::fromStdString(layer.maskSourceId));item->setSizeHint(0,{0,44});items.emplace(layer.id,item);}
        for(const auto& entry:layers::entries(*state.document,true)){const auto*l=findLayer(*state.document,entry.id);auto* item=items.at(entry.id);nameLayerControls(*item,*l);if(l->parentId.empty())tree_->addTopLevelItem(item);else items.at(l->parentId)->addChild(item);item->setExpanded(!state.collapsed.contains(entry.id));if(!entry.visible)item->setForeground(0,QBrush(QColor(125,125,125)));}}
    std::erase_if(thumbnailCache_->entries,[&](const auto&entry){const auto*l=state.document?findLayer(*state.document,entry.first.first):nullptr;return !l||(entry.first.second&&!l->mask);});
    updateSelection();rebuilding_=false;updateLayerAccessibility(tree_);
}
QMimeData* LayerPanelController::dragMime(bool maskCopy)const{const auto state=host_.state();std::vector<std::string> ordered;if(state.document){const std::unordered_set<std::string> selected(state.selection.ids.begin(),state.selection.ids.end());for(const auto& entry:layers::entries(*state.document,true,state.collapsed))if(selected.contains(entry.id))ordered.push_back(entry.id);}QMimeData* mime=!maskCopy&&host_.dragOwner?new ProjectLayerMimeData(host_.dragOwner(),ordered):new QMimeData;mime->setData(ownerMime,token_);if(maskCopy)mime->setData(maskMime,QByteArray::fromStdString(pressId_.empty()?state.selection.primary:pressId_));else{QJsonArray ids;for(const auto&id:state.selection.ids)ids.append(QString::fromStdString(id));mime->setData(layerMime,QJsonDocument(ids).toJson(QJsonDocument::Compact));}return mime;}
bool LayerPanelController::accepts(const QMimeData* mime,const QPoint& point,Qt::KeyboardModifiers modifiers)const{if(host_.canEdit&&!host_.canEdit())return false;try{const auto state=host_.state();if(!state.document||mime->data(ownerMime)!=token_)return false;if(mime->hasFormat(projectLayerMime)&&host_.dragOwner){const auto payload=projectLayerPayload(mime);if(!payload||projectLayerIdentity(host_.dragOwner())!=payload->session)return false;}const auto dest=destination(tree_,*state.document,point);if(mime->hasFormat(maskMime)){const auto* source=findLayer(*state.document,mime->data(maskMime).toStdString());const auto* target=findLayer(*state.document,dest.id);return source&&source->mask&&target&&!target->group&&source!=target;}if(!mime->hasFormat(layerMime))return false;const auto ids=idsOf(mime);if(ids.empty())return false;std::string parent;if(dest.zone==DropZone::IntoGroup)parent=dest.id;else if(dest.zone==DropZone::Above)parent=findLayer(*state.document,dest.id)->parentId;for(const auto&id:ids){const auto*l=findLayer(*state.document,id);if(!l||!layers::canPlace(*state.document,id,parent)||(modifiers.testFlag(Qt::AltModifier)&&l->group))return false;}return true;}catch(...){return false;}}
bool LayerPanelController::performDrop(const QMimeData* mime,const QPoint& point,Qt::KeyboardModifiers modifiers){if(!accepts(mime,point,modifiers))return false;bool changed=false;invoke([&]{const auto state=host_.state();const auto dest=destination(tree_,*state.document,point);auto result=mime->hasFormat(maskMime)?copyMask(*state.document,state.selection,mime->data(maskMime).toStdString(),dest.id):dropLayers(*state.document,state.selection,idsOf(mime),dest.id,dest.zone,modifiers.testFlag(Qt::AltModifier));changed=result.changed;if(changed){host_.commit(std::move(result),mime->hasFormat(maskMime));if(dest.zone==DropZone::IntoGroup)host_.collapse(dest.id,false);}});return changed;}

bool LayerPanelController::eventFilter(QObject* watched,QEvent* event){
    if(watched!=tree_->viewport()){
        if(event->type()==QEvent::WindowDeactivate&&watched==tree_->window())finishVisibilitySwipe();
        if(event->type()==QEvent::KeyPress||event->type()==QEvent::KeyRelease){const auto point=tree_->viewport()->mapFromGlobal(QCursor::pos());if(tree_->viewport()->rect().contains(point))refreshCursor(static_cast<QKeyEvent*>(event)->modifiers(),point);}
        return QObject::eventFilter(watched,event);
    }
    if(event->type()==QEvent::UngrabMouse){finishVisibilitySwipe();return false;}
    if(event->type()==QEvent::Enter){refreshCursor(QApplication::keyboardModifiers(),tree_->viewport()->mapFromGlobal(QCursor::pos()));}
    if(event->type()==QEvent::Leave){refreshCursor({},QPoint(-1,-1));}
    if(event->type()==QEvent::MouseMove){auto*mouse=static_cast<QMouseEvent*>(event);refreshCursor(mouse->modifiers(),mouse->position().toPoint());if(visibilitySwipe_){if(auto*item=tree_->itemAt(mouse->position().toPoint())){const auto id=idOf(item);try{if(host_.setVisibilityInSwipe)host_.setVisibilityInSwipe(id,*visibilitySwipe_);const auto state=host_.state();QSignalBlocker block(tree_);for(QTreeWidgetItemIterator it(tree_);*it;++it)if(const auto*l=state.document?findLayer(*state.document,idOf(*it)):nullptr)(*it)->setCheckState(0,l->visible?Qt::Checked:Qt::Unchecked);}catch(const std::exception&error){finishVisibilitySwipe();if(host_.error)host_.error(QString::fromUtf8(error.what()));}}if(mouse->position().y()<0)tree_->verticalScrollBar()->triggerAction(QAbstractSlider::SliderSingleStepSub);else if(mouse->position().y()>=tree_->viewport()->height())tree_->verticalScrollBar()->triggerAction(QAbstractSlider::SliderSingleStepAdd);return true;}}
    if(event->type()==QEvent::MouseButtonRelease&&visibilitySwipe_){finishVisibilitySwipe();return true;}
    // The tree stays enabled so ordinary target buttons can retain source
    // drafts. Block every other mouse edit while structural edits are gated.
    if((event->type()==QEvent::MouseButtonPress||event->type()==QEvent::MouseButtonDblClick)&&host_.canEdit&&!host_.canEdit()){
        auto* mouse=static_cast<QMouseEvent*>(event);auto* item=tree_->itemAt(mouse->position().toPoint());
        pressed_=false;deferSingle_=false;
        if(mouse->button()==Qt::LeftButton&&mouse->modifiers()==Qt::NoModifier&&item){
            const auto id=idOf(item);const int column=tree_->columnAt(mouse->position().toPoint().x());
            if(column==1||column==3)invokeActionControl(id,column==1?LayerControlKind::Image:LayerControlKind::Mask);
        }
        return true;
    }
    if(event->type()==QEvent::MouseButtonPress&&host_.beginVisibilitySwipe){auto*mouse=static_cast<QMouseEvent*>(event);auto*item=tree_->itemAt(mouse->position().toPoint());if(mouse->button()==Qt::LeftButton&&item&&checkRect(item).contains(mouse->position().toPoint())){if(host_.canEdit&&!host_.canEdit())return true;const auto id=idOf(item);try{visibilitySwipe_=host_.beginVisibilitySwipe(id);pressed_=false;if(visibilitySwipe_){QSignalBlocker block(tree_);if(auto*current=itemWithId(tree_,id))current->setCheckState(0,*visibilitySwipe_?Qt::Checked:Qt::Unchecked);tree_->viewport()->grabMouse();}}catch(const std::exception&error){finishVisibilitySwipe();if(host_.error)host_.error(QString::fromUtf8(error.what()));}return true;}}
    if(event->type()==QEvent::DragEnter||event->type()==QEvent::DragMove){auto* e=static_cast<QDragMoveEvent*>(event);if(accepts(e->mimeData(),e->position().toPoint(),e->modifiers())){e->setDropAction(e->mimeData()->hasFormat(maskMime)||e->modifiers().testFlag(Qt::AltModifier)?Qt::CopyAction:Qt::MoveAction);e->accept();}else e->ignore();return true;}
    if(event->type()==QEvent::Drop){auto* e=static_cast<QDropEvent*>(event);if(performDrop(e->mimeData(),e->position().toPoint(),e->modifiers())){e->setDropAction(e->mimeData()->hasFormat(maskMime)||e->modifiers().testFlag(Qt::AltModifier)?Qt::CopyAction:Qt::MoveAction);e->accept();}else e->ignore();return true;}
    if(event->type()==QEvent::DragLeave){event->accept();return true;}
    if(event->type()==QEvent::MouseButtonPress){auto* e=static_cast<QMouseEvent*>(event);if(e->button()!=Qt::LeftButton)return false;auto* item=tree_->itemAt(e->position().toPoint());if(!item)return false;press_=e->position().toPoint();pressId_=idOf(item);pressColumn_=tree_->columnAt(press_.x());pressModifiers_=e->modifiers();pressed_=true;dragging_=false;deferSingle_=false;const auto state=host_.state();const auto*l=state.document?findLayer(*state.document,pressId_):nullptr;if(!l)return false;const auto id=pressId_;const bool mask=pressColumn_==3&&l->mask.has_value();
        if((pressColumn_==1||mask)&&e->modifiers().testFlag(Qt::ControlModifier)){pressed_=false;invoke([&]{host_.loadSelection(id,mask,e->modifiers());});return true;}
        if(pressColumn_==2&&l->mask){pressed_=false;invoke([&]{host_.commit(editMask(*state.document,state.selection,id,MaskCommand::ToggleLink),state.maskSelected);});return true;}
        if(mask&&e->modifiers().testFlag(Qt::ShiftModifier)&&!e->modifiers().testFlag(Qt::AltModifier)){pressed_=false;invoke([&]{host_.select({{id},id},true);host_.maskCommand(int(MaskCommand::ToggleEnabled));});return true;}
        if(e->modifiers().testFlag(Qt::AltModifier)&&!e->modifiers().testFlag(Qt::ControlModifier)&&!mask&&press_.y()>=tree_->visualItemRect(item).bottom()-tree_->visualItemRect(item).height()/4){pressed_=false;invoke([&]{host_.commit(layers::toggleClipping(*state.document,state.selection,id),state.maskSelected);});return true;}
        if(mask&&e->modifiers().testFlag(Qt::AltModifier))return true;
        if((pressColumn_==1||mask)&&!e->modifiers().testFlag(Qt::ShiftModifier)){if(host_.selectTarget)invokeActionControl(id,mask?LayerControlKind::Mask:LayerControlKind::Image);else invoke([&]{host_.select({{id},id},mask);});return true;}
        if(state.selection.ids.size()>1&&item->isSelected()&&!(e->modifiers()&(Qt::ControlModifier|Qt::ShiftModifier))){deferSingle_=true;return true;}
    }
    if(event->type()==QEvent::MouseMove&&pressed_){auto* e=static_cast<QMouseEvent*>(event);if((e->buttons()&Qt::LeftButton)&&(e->position().toPoint()-press_).manhattanLength()>=QApplication::startDragDistance()){pressed_=false;dragging_=true;QDrag drag(tree_);drag.setMimeData(dragMime(pressColumn_==3&&pressModifiers_.testFlag(Qt::AltModifier)));drag.exec(Qt::MoveAction|Qt::CopyAction,pressModifiers_.testFlag(Qt::AltModifier)?Qt::CopyAction:Qt::MoveAction);dragging_=false;return true;}}
    if(event->type()==QEvent::MouseButtonRelease&&pressed_){pressed_=false;const auto id=pressId_;if(deferSingle_||(pressColumn_==3&&pressModifiers_.testFlag(Qt::AltModifier))){invoke([&]{host_.select({{id},id},pressColumn_==3);});return true;}}
    return QObject::eventFilter(watched,event);
}

void LayerPanelController::contextMenu(const QPoint& point){auto* item=tree_->itemAt(point);if(!item)return;auto state=host_.state();const auto id=idOf(item);if(std::find(state.selection.ids.begin(),state.selection.ids.end(),id)==state.selection.ids.end()){host_.select({{id},id},false);state=host_.state();}const auto*l=state.document?findLayer(*state.document,id):nullptr;if(!l)return;QMenu menu(tree_);menu.setObjectName("layerContextMenu");auto add=[&](QString label,int cmd,bool enabled=true){auto*a=menu.addAction(label);a->setEnabled(enabled);connect(a,&QAction::triggered,&menu,[this,cmd,id]{invoke([&]{if(cmd!=1&&cmd!=6&&cmd!=8)host_.select({{id},id},false);host_.layerCommand(cmd);});});};add("Rename…",10);add("Hide / Show Layer",11);add("Group Selected",1);add("Ungroup",13,l->group);add("Move Out of Group",2,!l->parentId.empty());add("Duplicate Layer",3,!l->group&&state.selection.ids.size()==1);add("Raise Layer",4,layers::canMoveSibling(*state.document,id,1));add("Lower Layer",5,layers::canMoveSibling(*state.document,id,-1));add("Create / Release Clipping Mask",7,layers::canToggleClipping(*state.document,id));add("Release Clipping Mask",12,!l->maskSourceId.empty());add("Merge Layers",8,layers::mergePlan(*state.document,state.selection).has_value());add("Copy Layer to Project…",9);menu.addSeparator();auto* masks=menu.addMenu("Mask");auto maskAction=[&](QString label,MaskCommand command,bool enabled){auto*a=masks->addAction(label);a->setEnabled(enabled);connect(a,&QAction::triggered,&menu,[this,command]{invoke([&]{host_.maskCommand(int(command));});});};const bool single=state.selection.ids.size()==1;maskAction("Add White Mask (Hide Selection)",MaskCommand::AddReveal,single&&!l->mask);maskAction("Add Black Mask (Reveal Selection)",MaskCommand::AddHide,single&&!l->mask);maskAction("Edit Image",MaskCommand::EditImage,single);maskAction("Edit Mask",MaskCommand::EditMask,single&&l->mask.has_value());maskAction(l->mask&&l->mask->enabled?"Disable Mask":"Enable Mask",MaskCommand::ToggleEnabled,single&&l->mask.has_value());maskAction(l->mask&&l->mask->linked?"Unlink Mask":"Link Mask",MaskCommand::ToggleLink,single&&l->mask.has_value());maskAction("Select Image Alpha",MaskCommand::LoadAlpha,single&&bool(l->raster));maskAction("Select Mask Black Areas",MaskCommand::LoadBlack,single&&l->mask.has_value());maskAction("Delete Mask",MaskCommand::Delete,single&&l->mask.has_value());menu.addSeparator();add("Delete Selected Layers",6);menu.exec(tree_->viewport()->mapToGlobal(point));}
}

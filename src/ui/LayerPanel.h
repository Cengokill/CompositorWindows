#pragma once
#include "layers/LayerOperations.h"
#include "editing/Selection.h"
#include <QObject>
#include <QPoint>
#include <QTreeWidget>
#include <QMimeData>
#include <QImage>
#include <QSizeF>
#include <functional>

namespace compositor::ui {
enum class DropZone { Above, IntoGroup, Bottom };
enum class MaskCommand { AddReveal, AddHide, RevealAll, HideAll, ToggleEnabled, Delete, ToggleLink, EditImage, EditMask, LoadAlpha, LoadBlack };
struct PanelState {
    const Document* document{};
    layers::SelectionState selection;
    bool maskSelected{};
    std::unordered_set<std::string> collapsed;
};
// All changes are calculated on immutable snapshots before the host starts history.
layers::EditResult dropLayers(const Document&,layers::SelectionState,const std::vector<std::string>&,
    const std::string& target,DropZone,bool copy);
layers::EditResult editMask(const Document&,layers::SelectionState,const std::string&,MaskCommand);
layers::EditResult copyMask(const Document&,layers::SelectionState,const std::string& source,const std::string& target);
std::optional<Selection> loadedSelection(const Document&,const std::string&,bool mask,editing::SelectionMode);
QSize canvasThumbnailSize(QSizeF canvas,double box);
QImage canvasLayerThumbnail(const Raster*,const Transform&,QSizeF canvas,double box=36);
double canvasMaskEdgeTone(const GrayRaster&);
QImage canvasMaskThumbnail(const GrayRaster&,const Transform&,QSizeF canvas,double box=30);

class LayerPanelController final:public QObject {
public:
    struct Host {
        std::function<void()> prepare;
        std::function<PanelState()> state;
        std::function<void(layers::SelectionState,bool)> select;
        std::function<void(layers::EditResult,bool)> commit;
        std::function<void(const std::string&,bool)> collapse;
        std::function<void(int)> layerCommand,maskCommand;
        std::function<void(const std::string&,bool,Qt::KeyboardModifiers)> loadSelection;
        std::function<void(const QString&)> error;
        std::function<bool()> canEdit;
        std::function<std::optional<bool>(const std::string&)> beginVisibilitySwipe;
        std::function<void(const std::string&,bool)> setVisibilityInSwipe;
        std::function<void()> endVisibilitySwipe;
    };
    LayerPanelController(QTreeWidget*,Host);
    ~LayerPanelController() override;
    void rebuild();
    void updateSelection();
    QMimeData* dragMime(bool maskCopy=false) const;
    bool accepts(const QMimeData*,const QPoint&,Qt::KeyboardModifiers) const;
    bool performDrop(const QMimeData*,const QPoint&,Qt::KeyboardModifiers);
    static LayerPanelController* find(QTreeWidget*);
    void finishVisibilitySwipe();
    void refreshCursor(Qt::KeyboardModifiers,const QPoint& viewportPoint);
    uint64_t thumbnailRenderCount()const{return thumbnailRenderCount_;}
protected:
    bool eventFilter(QObject*,QEvent*) override;
private:
    QTreeWidget* tree_;
    Host host_;
    QByteArray token_;
    QPoint press_;
    std::string pressId_;
    int pressColumn_{};
    Qt::KeyboardModifiers pressModifiers_{};
    bool rebuilding_{},pressed_{},dragging_{},deferSingle_{};
    std::optional<bool> visibilitySwipe_;
    struct ThumbnailCache;
    std::unique_ptr<ThumbnailCache> thumbnailCache_;
    uint64_t thumbnailRenderCount_{};
    bool modifierCursor_{};
    QIcon thumbnail(const Layer&,bool,QSizeF);
    QRect checkRect(const QTreeWidgetItem*)const;
    void contextMenu(const QPoint&);
    void selectionChanged();
    void invoke(const std::function<void()>&);
};
}

#pragma once
#include "core/Document.h"

namespace compositor::graphics {
// Source EditorCanvas.clonePreview/cloneTip. Cached images are temporary and
// bounded independently of the document's logical canvas dimensions.
class CloneCursorPreview {
public:
    std::shared_ptr<const Raster> render(const Document&,const std::string& activeLayerID,
        Point sourceCenter,double diameter,double hardness,double physicalZoom,bool sampleAllLayers,
        std::shared_ptr<const LayerRenderPreview> livePreview={});
    void reset();
private:
    std::optional<Document> document_;
    std::string active_;
    Point center_{};
    double diameter_{},hardness_{},zoom_{},tipDiameter_{},tipHardness_{};
    bool all_{};
    std::shared_ptr<const LayerRenderPreview> preview_;
    std::shared_ptr<const Raster> tip_,result_;
};
}

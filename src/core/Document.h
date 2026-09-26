#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace compositor {
namespace editing { class SelectionOutline; }
namespace graphics { struct SamplingSource; }
struct Pixel { uint8_t r{},g{},b{},a{}; bool operator==(const Pixel&) const = default; };
struct Point { double x{},y{}; bool operator==(const Point&) const = default; };
struct Transform {
    double x{},y{},width{1},height{1},rotation{};
    bool flipX{},flipY{};
    enum class Sampling { Nearest, Smooth, High } sampling{Sampling::High};
    bool valid() const;
    Point fromUnit(Point) const;
    Point toUnit(Point) const;
    bool operator==(const Transform&) const = default;
};
enum class Blend { Normal,Multiply,Screen,Overlay,Darken,Lighten,Difference,ColorDodge,ColorBurn,Hue,Saturation,Color,Luminosity };
inline constexpr std::array<const char*,13> blendNames{"Normal","Multiply","Screen","Overlay","Darken","Lighten","Difference","Color Dodge","Color Burn","Hue","Saturation","Color","Luminosity"};

// Immutable 256x256 tiles. Editing copies only touched tiles. Flattening is explicit.
class Raster {
public:
    static constexpr int tileSide=256;
    struct Tile { std::array<Pixel,tileSide*tileSide> pixels{}; };
    int width{},height{};
    std::vector<std::shared_ptr<const Tile>> tiles;
    // RasterSnapshot.alignment: transient halving phase retained through crops.
    // PNG project encoding materializes pixels and intentionally resets it.
    int samplingOriginX{},samplingOriginY{};
    static std::shared_ptr<const Raster> filled(int width,int height,Pixel value={});
    static std::shared_ptr<const Raster> fromRgba(int width,int height,const uint8_t* data,size_t stride);
    Pixel pixel(int x,int y) const;
    std::shared_ptr<const Raster> replacing(int x,int y,int width,int height,const Pixel* pixels,size_t rowPixels) const;
    std::vector<uint8_t> rgba() const;
    static uint64_t materializationCount();
    static void resetMaterializationCount();
    size_t retainedBytes() const { return tiles.size()*sizeof(Tile); }
};
struct GrayBounds {
    int x{},y{},width{},height{};
    bool empty()const{return width<=0||height<=0;}
};
// Immutable document-coordinate coverage. Large vector selections rasterize
// only requested tiles; dense image masks keep their existing byte storage.
class GrayRasterSource {
public:
    virtual ~GrayRasterSource()=default;
    virtual uint8_t pixel(int x,int y)const=0;
    virtual GrayBounds nonzeroBounds()const=0;
    virtual size_t retainedBytes()const=0;
    virtual std::shared_ptr<const editing::SelectionOutline> vectorOutline()const{return {};}
};
struct GrayRaster {
    int width{},height{};
    std::vector<uint8_t> pixels;
    int samplingOriginX{},samplingOriginY{};
    std::shared_ptr<const GrayRasterSource> source;
    uint8_t pixel(int x,int y,uint8_t exterior=0) const;
    bool validStorage()const;
    GrayBounds nonzeroBounds()const;
    bool hasCoverage()const{return !nonzeroBounds().empty();}
    size_t retainedBytes()const{return pixels.size()+(source?source->retainedBytes():0);}
    static std::shared_ptr<const GrayRaster> sampled(int width,int height,GrayBounds support,
        std::function<uint8_t(int,int)> pixel,size_t retainedBytes=0,
        std::shared_ptr<const editing::SelectionOutline> outline={});
};
struct Mask {
    std::shared_ptr<const GrayRaster> raster;
    bool enabled{true},linked{true};
    std::optional<Transform> placement;
    // Rendering-only crop metadata. Set on temporary viewport documents, never on saved layers.
    std::optional<uint8_t> previewExterior;
    bool operator==(const Mask&) const = default;
};
struct Layer {
    std::string id,name{"Layer"},parentId,maskSourceId;
    bool visible{true},group{};
    double opacity{1};
    Blend blend{Blend::Normal};
    Transform transform;
    std::shared_ptr<const Raster> raster;
    std::optional<Mask> mask;
    // Exact serialized live metadata is retained while its editing implementation lands.
    std::string adjustmentJson,shapeJson;
    bool operator==(const Layer&) const = default;
};
struct Selection {
    // optional absent means unrestricted. Present all-zero coverage means edit nothing.
    std::shared_ptr<const GrayRaster> coverage;
    std::shared_ptr<const editing::SelectionOutline> outline;
    bool operator==(const Selection&) const = default;
};
struct Document {
    std::string id;
    int width{800},height{600};
    double resolution{72};
    std::vector<Layer> layers; // Bottom to top.
    std::optional<Selection> selection;
    bool operator==(const Document&) const = default;
};
// Immutable render-only source override. It is never part of Document, history,
// or serialized data. Sampling uses the unit coordinates of layer metadata.
struct LayerRenderPreview {
    struct Damage {double left{},top{},right{},bottom{};};
    Layer layer;
    std::shared_ptr<const void> identity;
    std::shared_ptr<const void> lineage;
    std::function<Pixel(Point,Transform::Sampling)> image;
    std::function<double(Point,Transform::Sampling,uint8_t)> mask;
    std::shared_ptr<const graphics::SamplingSource> imageSource,maskSource;
    // Null previous compares against the canonical source. Nullopt means that
    // every output tile must be invalidated; an empty vector means no change.
    std::function<std::optional<std::vector<Damage>>(const LayerRenderPreview*)> damageComparedWith;
};
std::string newId();
void validateDocument(const Document&);
Pixel blendPixel(Pixel destination,Pixel source,Blend mode);
class IRasterBackend {
public:
    virtual ~IRasterBackend()=default;
    virtual std::shared_ptr<const Raster> render(const Document&,int x,int y,int width,int height) const=0;
};
class SoftwareRenderer final:public IRasterBackend {
    std::shared_ptr<const LayerRenderPreview> preview_;
public:
    explicit SoftwareRenderer(std::shared_ptr<const LayerRenderPreview> preview={}):preview_(std::move(preview)){}
    std::shared_ptr<const Raster> render(const Document&,int x,int y,int width,int height) const override;
    std::shared_ptr<const Raster> renderScaled(const Document&,double x,double y,int width,int height,double unitsPerPixel) const;
    std::shared_ptr<const Raster> renderScaledPatch(const Document&,double tileX,double tileY,int offsetX,int offsetY,int width,int height,double unitsPerPixel) const;
    // Cursor previews have at most1024 samples per axis; ceil-to-screen sizing
    // can require a positive step slightly below the viewport minimum1/32.
    std::shared_ptr<const Raster> renderCursorRegion(const Document&,double x,double y,int width,int height,double unitsPerPixel) const;
};
struct CompositeViewport {
    std::shared_ptr<const Raster> raster;
    double documentX{},documentY{},unitsPerPixel{1};
    int documentWidth{},documentHeight{};
};
// Recompose only document tiles affected by changed source tiles. Value snapshots
// and tile identities keep mouse-up and the next stroke independent of flattening.
class CompositeCache {
    std::optional<Document> previous_;
    std::shared_ptr<const Raster> output_;
    std::optional<Document> viewportPrevious_;
    struct ViewportTile {std::shared_ptr<const Raster::Tile> pixels;uint64_t use{};};
    std::map<std::pair<int,int>,ViewportTile> viewportTiles_;
    double viewportUnits_{},viewportPhaseX_{},viewportPhaseY_{};
    uint64_t viewportTick_{};
    std::shared_ptr<const LayerRenderPreview> viewportPreview_;
public:
    std::shared_ptr<const Raster> render(const Document&);
    CompositeViewport renderViewport(const Document&,double x,double y,double width,double height,double unitsPerPixel=1,size_t maxVisibleTiles=64,size_t maxRetainedTiles=256,std::shared_ptr<const LayerRenderPreview> preview={});
    size_t viewportRetainedTiles()const{return viewportTiles_.size();}
    size_t viewportMetadataEntries()const{return viewportTiles_.size();}
    void reset(){previous_.reset();output_.reset();viewportPrevious_.reset();viewportTiles_.clear();viewportUnits_=viewportPhaseX_=viewportPhaseY_=0;viewportTick_=0;viewportPreview_.reset();}
};
struct Snapshot { std::optional<Document> document; std::string activeLayer; uint64_t revision{}; };
class History {
    struct Entry { std::string name; Snapshot before,after; };
    std::vector<Entry> past_,future_;
    std::optional<Snapshot> pending_;
    std::string pendingName_;
    uint64_t revision_{1},savedRevision_{1},nextRevision_{2};
    int depth_{};
    struct TrimPlan { size_t past{},future{}; };
    static size_t retainedBytes(const std::optional<Document>&,std::span<const Entry>,std::span<const Entry>);
    TrimPlan planTrim(const std::optional<Document>&,std::span<const Entry>,std::span<const Entry>) const;
    void applyTrim(TrimPlan) noexcept;
public:
    size_t entryLimit{100},byteLimit{256*1024*1024};
    void begin(std::string name,const std::optional<Document>& doc,const std::string& active);
    void end(const std::optional<Document>& doc,const std::string& active);
    std::optional<Snapshot> cancel() noexcept;
    std::optional<Snapshot> undo();
    std::optional<Snapshot> redo();
    void markSaved(){ savedRevision_=revision_; }
    void reset();
    bool modified() const {return revision_!=savedRevision_;}
    bool canUndo() const {return depth_==0&&!past_.empty();}
    bool canRedo() const {return depth_==0&&!future_.empty();}
    std::string undoName() const {return past_.empty()?"":past_.back().name;}
    std::string redoName() const {return future_.empty()?"":future_.back().name;}
    size_t undoCount() const {return past_.size();}
    size_t retainedBytes(const std::optional<Document>&) const;
};
}

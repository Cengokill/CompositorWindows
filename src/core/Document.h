#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace compositor {
namespace editing { class SelectionOutline; }
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
    static std::shared_ptr<const Raster> filled(int width,int height,Pixel value={});
    static std::shared_ptr<const Raster> fromRgba(int width,int height,const uint8_t* data,size_t stride);
    Pixel pixel(int x,int y) const;
    std::shared_ptr<const Raster> replacing(int x,int y,int width,int height,const Pixel* pixels,size_t rowPixels) const;
    std::vector<uint8_t> rgba() const;
    static uint64_t materializationCount();
    static void resetMaterializationCount();
    size_t retainedBytes() const { return tiles.size()*sizeof(Tile); }
};
struct GrayRaster {
    int width{},height{};
    std::vector<uint8_t> pixels;
    uint8_t pixel(int x,int y,uint8_t exterior=0) const;
};
struct Mask {
    std::shared_ptr<const GrayRaster> raster;
    bool enabled{true},linked{true};
    std::optional<Transform> placement;
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
std::string newId();
void validateDocument(const Document&);
Pixel blendPixel(Pixel destination,Pixel source,Blend mode);
class IRasterBackend {
public:
    virtual ~IRasterBackend()=default;
    virtual std::shared_ptr<const Raster> render(const Document&,int x,int y,int width,int height) const=0;
};
class SoftwareRenderer final:public IRasterBackend {
public:
    std::shared_ptr<const Raster> render(const Document&,int x,int y,int width,int height) const override;
};
// Recompose only document tiles affected by changed source tiles. Value snapshots
// and tile identities keep mouse-up and the next stroke independent of flattening.
class CompositeCache {
    std::optional<Document> previous_;
    std::shared_ptr<const Raster> output_;
public:
    std::shared_ptr<const Raster> render(const Document&);
    void reset(){previous_.reset();output_.reset();}
};
struct Snapshot { std::optional<Document> document; std::string activeLayer; uint64_t revision{}; };
class History {
    struct Entry { std::string name; Snapshot before,after; };
    std::vector<Entry> past_,future_;
    std::optional<Snapshot> pending_;
    std::string pendingName_;
    uint64_t revision_{1},savedRevision_{1},nextRevision_{2};
    int depth_{};
    void trim(const std::optional<Document>&);
public:
    size_t entryLimit{100},byteLimit{256*1024*1024};
    void begin(std::string name,const std::optional<Document>& doc,const std::string& active);
    void end(const std::optional<Document>& doc,const std::string& active);
    std::optional<Snapshot> cancel();
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

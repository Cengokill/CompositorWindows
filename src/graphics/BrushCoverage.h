#pragma once
#include "ParallelBatch.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace compositor::graphics {
inline constexpr uint32_t brushTileSize=256;
struct BrushSegment { float x0,y0,x1,y1; };
// Field offsets follow four 16-byte HLSL registers. CPU object address alignment
// is immaterial: D3D11 copies these 64 bytes into a constant buffer.
struct BrushUniforms {
    std::array<float,4> mapping{1,0,0,1}; // a,b,c,d: local pixel to document
    std::array<float,4> geometry{0,0,10,0}; // document tile origin x,y,radius,hardness
    std::array<float,4> canvas{256,256,1,0.5f}; // document width,height,AA width,deposition spacing
    std::array<uint32_t,4> counts{}; // populated by render: width,height,settled,total
};
static_assert(sizeof(BrushUniforms)==64 && sizeof(BrushSegment)==16);
struct BrushTile {
    uint32_t width,height;
    // Soft tips store optical density; hard tips store coverage.
    // One tile belongs to one stroke with fixed geometry/settings.
    std::vector<float> permanent;
    std::vector<uint8_t> preview;
    explicit BrushTile(uint32_t width=brushTileSize,uint32_t height=brushTileSize);
};
// Caller submits only newly settled geometry. Tail is replaced every invocation;
// an empty settled/tail invocation removes the tail. These calls do not apply
// color or stroke opacity. The returned preview is an owned gray8 snapshot.
void validateBrush(const BrushTile&,const BrushUniforms&,std::span<const BrushSegment> settled,std::span<const BrushSegment> tail);
void renderBrushCpu(BrushTile&,BrushUniforms,std::span<const BrushSegment> settled,std::span<const BrushSegment> tail={});
struct BrushTileRender { BrushTile* tile; BrushUniforms uniforms; };
inline constexpr uint32_t brushCpuMaxWorkers=parallelBatchMaxWorkers;
// Synchronous, disjoint32-row jobs within independently owned tiles. Preflight validates the entire
// batch before writes; missing/duplicate tiles and batches over4096 reject.
// 0 selects min(logical CPUs,16), 1 executes sequentially. The private native
// pool bounds its workers across concurrent calls and retains no caller data
// after return. Unexpected worker exceptions propagate after all callbacks
// finish; callers must discard the batch's scratch tiles on such a failure.
// Scheduling uses at most32768 row descriptors and never copies tile states.
void renderBrushCpuBatch(std::span<const BrushTileRender>,std::span<const BrushSegment> settled,
                         std::span<const BrushSegment> tail={},uint32_t workerLimit=0);
class D3D11BrushCoverage {
public:
    explicit D3D11BrushCoverage(const std::filesystem::path& shader,bool preferWarp=true);
    ~D3D11BrushCoverage();
    D3D11BrushCoverage(D3D11BrushCoverage&&) noexcept;
    D3D11BrushCoverage& operator=(D3D11BrushCoverage&&) noexcept;
    D3D11BrushCoverage(const D3D11BrushCoverage&)=delete;
    D3D11BrushCoverage& operator=(const D3D11BrushCoverage&)=delete;
    void render(BrushTile&,BrushUniforms,std::span<const BrushSegment> settled,std::span<const BrushSegment> tail={});
    // Tiles share submitted geometry. Dispatches use a bounded reusable pool;
    // all CPU tile states commit together after successful readback.
    void renderBatch(std::span<const BrushTileRender>,std::span<const BrushSegment> settled,std::span<const BrushSegment> tail={});
    std::string adapterName() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace compositor::graphics

#pragma once
#include "image_types.h"
#include <filesystem>
#include <memory>
#include <optional>

namespace compositor::imaging {
// Relative depth, rows top-down: 1 nearest, 0 farthest.
struct DepthEstimate { std::uint32_t width{}, height{}; std::vector<float> values; };
// Depth Anything V2 Small, converted locally by tests/imaging/convert_depth_anything.py.
// Loading and execution are local, on ONNX Runtime's CPU provider.
class OnnxDepthProvider final {
    struct Impl;std::unique_ptr<Impl> impl_;
public:
    static constexpr const char* modelSha256="61d29718cf81b46939d1549c2c0d0f85d788d3d837c7c389c3a2e949da516196";
    // The model's longer input side (46 x 14), and the longer side of the result.
    static constexpr std::uint32_t inputSide=644,refinedSide=640;
    explicit OnnxDepthProvider(const std::filesystem::path& modelPath);
    ~OnnxDepthProvider();
    // The input keeps the image's proportions in multiples of 14. The prediction
    // is enlarged to refinedSide and pulled onto the photo's edges with a guided
    // filter. Nullopt when the model sees no variation in depth.
    std::optional<DepthEstimate> estimate(const RgbaImage&,const ImportOptions& = {});
    // The model input size for an image of width x height.
    static std::pair<std::uint32_t,std::uint32_t> inputSize(std::uint32_t width,std::uint32_t height);
};
}

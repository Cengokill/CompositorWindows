#pragma once
#include "imaging/image_types.h"
#include <cstdint>
#include <functional>
#include <vector>

namespace compositor::editing {
// Focus stacking for frames of one subject captured at different focus distances.
// Alignment is a whole-pixel translation only; rotation and focus breathing are not corrected.
struct FocusStackLimits {
    std::uint64_t maxWorkingBytes{1200000000};
    // Must be thread-safe; polled between frames and pixel rows.
    std::function<bool()> cancelled;
};
struct FocusShift {
    int dx{},dy{};
    bool operator==(const FocusShift&) const = default;
};
// Shift such that moving(x+dx,y+dy) best matches reference(x,y), searched within +/-maxShift.
FocusShift estimateFocusShift(const imaging::RgbaImage& reference,const imaging::RgbaImage& moving,int maxShift);
// Local Laplacian energy of the shifted frame, averaged over a (2*radius+1)^2 box. Row-major, width*height.
std::vector<float> focusSharpness(const imaging::RgbaImage&,FocusShift,int radius);
// Bytes held while stacking count frames of width x height, inputs included.
std::uint64_t focusStackWorkingBytes(std::uint32_t width,std::uint32_t height,std::size_t count);
struct FocusStackResult {
    imaging::RgbaImage image;
    std::vector<FocusShift> shifts;   // One per frame; the first frame is the reference.
    std::vector<std::uint8_t> source; // Selected frame per output pixel.
};
// Requires 2..16 valid frames of identical size. Each output pixel copies the aligned frame
// with the highest local sharpness; pixels a shifted frame does not cover use the other frames.
// Where the two sharpest frames score about equally, the pixel is instead the average of every covering frame.
FocusStackResult focusStack(const std::vector<imaging::RgbaImage>& frames,const FocusStackLimits& = {});
inline constexpr std::size_t maxFocusStackFrames=16;
}

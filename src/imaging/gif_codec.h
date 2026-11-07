#pragma once
#include "image_types.h"
#include <filesystem>

namespace compositor::imaging {
// Images are fully composited logical-screen frames in premultiplied sRGB RGBA.
// GIF stores delays in hundredths of a second and only binary transparency.
struct GifFrame {
    RgbaImage image;
    unsigned delayCentiseconds{};
};
struct GifAnimation {
    std::vector<GifFrame> frames;
};
GifAnimation decodeGif(const std::filesystem::path&, const ImportOptions& = {});
// Quantizes each frame to at most 256 colors, keeping an exact palette when the
// frame has that many unique opaque colors. Fully transparent pixels stay binary.
// Every frame is written in full, with disposal-to-background. An interrupted
// operation leaves an existing destination untouched.
void encodeGif(const std::filesystem::path&, const GifAnimation&, const std::function<bool()>& cancelled = {});
}

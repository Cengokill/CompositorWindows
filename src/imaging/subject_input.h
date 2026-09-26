#pragma once
#include "image_types.h"

namespace compositor::imaging {
struct SubjectInputStats {
    std::uint64_t sourceBytes{}, outputBytes{}, temporaryBytes{};
    std::uint64_t validatedPixels{}, preparedSourceRows{}, cancellationChecks{};
    std::uint32_t maximumCachedRows{};
    bool verticalFirst{};
};
// Fixed 1x3x1024x1024 float32 NCHW input for the pinned local foreground model.
// RGB8 input follows Pillow's antialiased bilinear integer recipe. Premultiplied
// RGBA is reconstructed to nearest straight RGB8 first; alpha zero is black.
// Accounting covers resident input and new buffer payloads, not allocator/ORT memory.
std::vector<float> subjectInputTensor(const RgbaImage&, const ImportOptions& = {},
                                     SubjectInputStats* = nullptr);
}

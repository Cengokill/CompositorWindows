#pragma once
#include "image_types.h"
#include <cstdint>
#include <filesystem>

namespace compositor::imaging {
// Bounds shared by the RAW development dialog and developRaw validation.
inline constexpr double kRawExposureLimitEv = 5.0;
inline constexpr std::uint32_t kRawKelvinMin = 2000;
inline constexpr std::uint32_t kRawKelvinMax = 12000;
inline constexpr double kRawTintLimit = 150.0;
inline constexpr double kRawNoiseLimit = 100.0;

bool isRawFileExtension(const std::filesystem::path& path);

// Develops a camera RAW file with LibRaw and applies options.raw at full resolution.
// previewMaxSide only downsamples the returned pixels (0 keeps full size); the settings are never applied to the downsampled image.
RawDevelopment developRaw(const std::filesystem::path& path, const ImportOptions& options = {}, std::uint32_t previewMaxSide = 0);
DecodedImage decodeRaw(const std::filesystem::path& path, const ImportOptions& options = {});
}

#pragma once
#include "imaging/image_types.h"
#include <filesystem>
#include <memory>
#include <span>

namespace compositor::platform {
enum class DisplayProfileKind { AssumedSrgb, Icc };
struct DisplayProfile {
    DisplayProfileKind kind{DisplayProfileKind::AssumedSrgb};
    std::filesystem::path path;
    std::wstring monitorDevice;
    std::vector<std::uint8_t> icc;
    std::string sha256;
    // A missing/invalid profile is an explicit sRGB fallback, never a claimed profile.
    std::string diagnostic;
};
DisplayProfile loadDisplayProfile(const std::filesystem::path& path);
DisplayProfile discoverDisplayProfile(void* nativeWindow);
struct DisplayConversionLimits {
    std::uint64_t maxOutputBytes{64ULL * 1024 * 1024};
    std::uint32_t tileSide{256};
};
struct DisplayConversion {
    imaging::RgbaImage pixels;
    bool converted{};
    bool fallback{};
    std::string diagnostic;
    std::string profileHash;
};
// Own on the presentation thread. Input is immutable canonical sRGB; only the
// returned copy belongs to the monitor's device space. Never save that copy.
class DisplayColorConverter {
public:
    explicit DisplayColorConverter(bool forceWarp=false);
    ~DisplayColorConverter();
    DisplayColorConverter(const DisplayColorConverter&)=delete;
    DisplayColorConverter& operator=(const DisplayColorConverter&)=delete;
    DisplayConversion convert(const imaging::RgbaImage&, const DisplayProfile&, const DisplayConversionLimits& = {});
    std::string adapterName() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}

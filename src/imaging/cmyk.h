#pragma once
#include "core/CmykProfile.h"
#include <filesystem>
#include <span>
#include <functional>
#include <array>
#include <stdexcept>

namespace compositor::imaging {
inline constexpr std::size_t maxCmykProfileBytes = 16 * 1024 * 1024;
class MissingCmykProfile : public std::runtime_error {
public: MissingCmykProfile():std::runtime_error("This CMYK file has no embedded profile. Choose its CMYK source profile."){}
};
CmykProfilePtr readCmykProfile(const std::filesystem::path&);
CmykProfilePtr makeCmykProfile(std::span<const std::uint8_t>,std::string name);
// Each instance belongs to one thread. Samples are straight, full-range 16-bit.
class CmykTransform {
public:
    explicit CmykTransform(CmykProfilePtr);
    ~CmykTransform();
    CmykTransform(const CmykTransform&)=delete;
    CmykTransform& operator=(const CmykTransform&)=delete;
    void toRgb(std::span<const std::uint16_t> cmyk,std::span<std::uint16_t> rgb);
    void toCmyk(std::span<const std::uint16_t> rgb,std::span<std::uint16_t> cmyk);
    // Premultiplied sRGB input is flattened over matte before conversion.
    void encode(std::span<const std::uint8_t> rgba,std::span<std::uint8_t> cmyk,
                std::array<std::uint8_t,3> matte);
    void decode(std::span<const std::uint8_t> cmyk,std::span<std::uint8_t> rgba);
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};
std::vector<float> cmykProofLookup(CmykProfilePtr,const std::function<bool()>& cancelled={});
}

namespace compositor::imaging {
// Reads only the Photoshop header/resources; no layer pixels are decoded.
CmykProfilePtr photoshopCmykProfile(const std::filesystem::path&,CmykProfilePtr fallback={});
}

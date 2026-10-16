#include "DeepImage.h"
#include "Document.h"
#include <algorithm>
#include <stdexcept>

namespace compositor {
std::shared_ptr<const DeepImage> deepFromPlanes(int width, int height, const std::uint16_t* red, const std::uint16_t* green, const std::uint16_t* blue, const std::uint16_t* alpha) {
    if (width < 1 || height < 1 || !red || !green || !blue) throw std::runtime_error("Invalid 16-bit image");
    auto image = std::make_shared<DeepImage>();
    image->width = width;
    image->height = height;
    image->samples.resize(std::size_t(width) * std::size_t(height) * 4);
    for (std::size_t i = 0; i < std::size_t(width) * std::size_t(height); ++i) {
        const std::uint16_t a = alpha ? alpha[i] : std::uint16_t{65535};
        auto* pixel = image->samples.data() + i * 4;
        pixel[0] = std::uint16_t(std::uint32_t(red[i]) * a / 65535);
        pixel[1] = std::uint16_t(std::uint32_t(green[i]) * a / 65535);
        pixel[2] = std::uint16_t(std::uint32_t(blue[i]) * a / 65535);
        pixel[3] = a;
    }
    return image;
}
std::shared_ptr<const Raster> rasterFromDeep(const DeepImage& image) {
    if (!image.valid()) throw std::runtime_error("Invalid 16-bit image");
    std::vector<std::uint8_t> rgba(std::size_t(image.width) * std::size_t(image.height) * 4);
    for (int y = 0; y < image.height; ++y) for (int x = 0; x < image.width; ++x) {
        const auto* sample = image.at(x, y);
        auto* pixel = rgba.data() + (std::size_t(y) * std::size_t(image.width) + std::size_t(x)) * 4;
        pixel[3] = ditherChannel(sample[3], x, y);
        for (int channel = 0; channel < 3; ++channel) pixel[channel] = std::min(pixel[3], ditherChannel(sample[channel], x, y));
    }
    return Raster::fromRgba(image.width, image.height, rgba.data(), std::size_t(image.width) * 4);
}
}

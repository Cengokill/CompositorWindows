#pragma once
#include "core/Document.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <optional>

namespace compositor::imaging {
struct PhotoshopText {
    bool editable{};
    TextContent text;
    double x{}, y{};
    std::string note;
};
struct PhotoshopArtboardRect { double left, top, right, bottom; std::optional<int> backgroundType; };
std::optional<PhotoshopArtboardRect> readPhotoshopArtboard(const uint8_t* data, size_t size);
// Horizontal type that fits the text model stays editable. Vertical type, shear,
// and uneven scale come back with a note and no text so the caller keeps the pixels.
PhotoshopText readPhotoshopText(const uint8_t* data, size_t size);
}

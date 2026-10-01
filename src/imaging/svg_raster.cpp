#include "svg_raster.h"
#include <d2d1_3.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <vector>
namespace compositor::imaging {
namespace {
using Microsoft::WRL::ComPtr;
void check(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("SVG rasterization failed"); }
int attribute(const std::string& xml, const char* name, int fallback) {
    auto key = std::string(name) + "=\""; auto at = xml.find(key); if (at == std::string::npos) return fallback;
    return std::clamp(std::atoi(xml.c_str() + at + key.size()), 1, 4096);
}
}
DecodedImage rasterizeSvg(const std::filesystem::path& path, const ImportOptions& options) {
    std::ifstream file(path, std::ios::binary); if (!file) throw std::runtime_error("Cannot open SVG");
    std::string xml((std::istreambuf_iterator<char>(file)), {}); if (xml.find("<svg") == std::string::npos) throw std::runtime_error("Not an SVG document");
    int width = attribute(xml, "width", 512), height = attribute(xml, "height", 512);
    checkedBytes(uint32_t(width), uint32_t(height), 4, options);
    ComPtr<ID3D11Device> device; check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, nullptr));
    ComPtr<IDXGIDevice> dxgi; check(device.As(&dxgi));
    ComPtr<ID2D1Factory1> factory; check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&factory)));
    ComPtr<ID2D1Device> d2dDevice; check(factory->CreateDevice(dxgi.Get(), &d2dDevice));
    ComPtr<ID2D1DeviceContext> context; check(d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context));
    ComPtr<ID2D1DeviceContext5> svgContext; if (FAILED(context.As(&svgContext))) throw std::runtime_error("This Windows version cannot rasterize SVG");
    ComPtr<IStream> stream; check(CreateStreamOnHGlobal(nullptr, TRUE, &stream)); ULONG written = 0; check(stream->Write(xml.data(), ULONG(xml.size()), &written)); LARGE_INTEGER zero{}; check(stream->Seek(zero, STREAM_SEEK_SET, nullptr));
    ComPtr<ID2D1SvgDocument> document; check(svgContext->CreateSvgDocument(stream.Get(), D2D1::SizeF(float(width), float(height)), &document));
    D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    ComPtr<ID2D1Bitmap1> target; check(context->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0, properties, &target));
    context->SetTarget(target.Get()); context->BeginDraw(); context->Clear(D2D1::ColorF(0, 0)); svgContext->DrawSvgDocument(document.Get()); check(context->EndDraw());
    properties.bitmapOptions = D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    ComPtr<ID2D1Bitmap1> readable; check(context->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0, properties, &readable)); check(readable->CopyFromBitmap(nullptr, target.Get(), nullptr));
    D2D1_MAPPED_RECT mapped{}; check(readable->Map(D2D1_MAP_OPTIONS_READ, &mapped));
    DecodedImage decoded; decoded.image.width = uint32_t(width); decoded.image.height = uint32_t(height); decoded.image.stride = size_t(width) * 4; decoded.image.pixels.resize(decoded.image.stride * height); decoded.metadata.decoder = "svg";
    for (int y = 0; y < height; ++y) { auto row = mapped.bits + y * mapped.pitch; auto dest = decoded.image.pixels.data() + size_t(y) * decoded.image.stride; for (int x = 0; x < width; ++x) { dest[x * 4] = row[x * 4 + 2]; dest[x * 4 + 1] = row[x * 4 + 1]; dest[x * 4 + 2] = row[x * 4]; dest[x * 4 + 3] = row[x * 4 + 3]; } }
    readable->Unmap(); validate(decoded.image); return decoded;
}
}

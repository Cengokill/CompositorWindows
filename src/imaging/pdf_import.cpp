#include "pdf_import.h"
#include <windows.h>
#include <winrt/Windows.Data.Pdf.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.UI.h>
#include <cmath>

namespace compositor::imaging {
namespace {
namespace pdf = winrt::Windows::Data::Pdf;
namespace bitmap = winrt::Windows::Graphics::Imaging;
namespace storage = winrt::Windows::Storage;

struct Apartment {
    Apartment() { winrt::init_apartment(winrt::apartment_type::multi_threaded); }
    ~Apartment() { winrt::uninit_apartment(); }
};

template<class Operation>
auto awaitPdfOperation(const Operation& operation, const ImportOptions& options) {
    auto finished = std::make_shared<winrt::handle>(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!*finished) winrt::throw_last_error();
    operation.Completed([finished](const auto&, auto) { SetEvent(finished->get()); });
    bool cancelling = false;
    for (;;) {
        const auto status = WaitForSingleObject(finished->get(), cancelling ? INFINITE : 25);
        if (status == WAIT_OBJECT_0) break;
        if (status != WAIT_TIMEOUT) winrt::throw_last_error();
        if (options.cancelled && options.cancelled()) {
            try { operation.Cancel(); }
            catch (const winrt::hresult_error&) {
                // Keep the inputs alive until completion even if the cancellation request fails.
            }
            cancelling = true;
        }
    }
    checkCancelled(options);
    return operation.GetResults();
}

uint32_t pageSide(float dips, double dpi, const ImportOptions& options) {
    // Windows supplies crop- and rotation-adjusted dimensions at 96 DIPs/inch.
    const auto pixels = std::round(double(dips) * dpi / 96.);
    if (!std::isfinite(pixels) || pixels < 1 || pixels > std::min(options.maxSide, 30000U))
        throw std::runtime_error("PDF page exceeds the supported dimensions at this resolution");
    return uint32_t(pixels);
}
}

std::vector<std::shared_ptr<const Raster>> rasterizePdf(
    const std::filesystem::path& path, double dpi, const ImportOptions& options) {
    if (!std::isfinite(dpi) || dpi < 1 || dpi > 9600)
        throw std::runtime_error("Invalid PDF rasterization resolution");
    try {
        checkCancelled(options);
        Apartment apartment;
        const auto file = awaitPdfOperation(storage::StorageFile::GetFileFromPathAsync(
            std::filesystem::absolute(path).wstring()), options);
        const auto document = awaitPdfOperation(pdf::PdfDocument::LoadFromFileAsync(file, L""), options);
        const auto count = document.PageCount();
        if (count == 0 || count > 10000)
            throw std::runtime_error("PDF page count exceeds the supported layer limits");

        auto remaining = options;
        remaining.remainingPixels = std::min<uint64_t>(remaining.remainingPixels, 100000000);
        std::vector<std::shared_ptr<const Raster>> pages;
        pages.reserve(count);
        for (uint32_t index = 0; index < count; ++index) {
            checkCancelled(options);
            const auto page = document.GetPage(index);
            const auto size = page.Size();
            const auto width = pageSide(size.Width, dpi, remaining);
            const auto height = pageSide(size.Height, dpi, remaining);
            const auto bytes = checkedBytes(width, height, 4, remaining);
            const auto tileBytes = uint64_t((width + Raster::tileSide - 1) / Raster::tileSide)
                * ((height + Raster::tileSide - 1) / Raster::tileSide) * sizeof(Raster::Tile);
            // Allow room for the render target, decoded pixels and tiled result.
            if (2 * uint64_t(bytes) + tileBytes > remaining.maxWorkingBytes)
                throw std::runtime_error("PDF pages exceed the import allocation budget");

            pdf::PdfPageRenderOptions render;
            render.DestinationWidth(width);
            render.DestinationHeight(height);
            render.BackgroundColor(winrt::Windows::UI::Color{0, 0, 0, 0});
            render.IsIgnoringHighContrast(true);
            render.BitmapEncoderId(bitmap::BitmapEncoder::PngEncoderId());
            storage::Streams::InMemoryRandomAccessStream stream;
            awaitPdfOperation(page.RenderToStreamAsync(stream, render), options);
            const auto encodedBytes = stream.Size();
            if (encodedBytes > remaining.maxWorkingBytes - 2 * uint64_t(bytes) - tileBytes)
                throw std::runtime_error("PDF page stream exceeds the import allocation budget");
            stream.Seek(0);
            const auto decoder = awaitPdfOperation(bitmap::BitmapDecoder::CreateAsync(stream), options);
            if (decoder.PixelWidth() != width || decoder.PixelHeight() != height)
                throw std::runtime_error("Windows returned unexpected PDF page dimensions");
            const auto pixels = awaitPdfOperation(decoder.GetPixelDataAsync(
                bitmap::BitmapPixelFormat::Rgba8, bitmap::BitmapAlphaMode::Premultiplied,
                bitmap::BitmapTransform{}, bitmap::ExifOrientationMode::IgnoreExifOrientation,
                bitmap::ColorManagementMode::ColorManageToSRgb), options).DetachPixelData();
            if (pixels.size() != bytes)
                throw std::runtime_error("Windows returned incomplete PDF page pixels");
            pages.push_back(Raster::fromRgba(int(width), int(height), pixels.data(),
                                             size_t(width) * 4, options.cancelled));
            remaining.remainingPixels -= uint64_t(width) * height;
            remaining.maxWorkingBytes -= tileBytes;
        }
        checkCancelled(options);
        return pages;
    } catch (const winrt::hresult_error& error) {
        if (error.code() == HRESULT_FROM_WIN32(ERROR_WRONG_PASSWORD)
            || error.code() == HRESULT_FROM_WIN32(ERROR_INVALID_PASSWORD))
            throw std::runtime_error("This PDF is password-protected. Save an unlocked copy and import it.");
        throw std::runtime_error("Could not read or render the PDF: " + winrt::to_string(error.message()));
    }
}
}

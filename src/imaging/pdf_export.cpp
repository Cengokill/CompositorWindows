#include "export_temporary_file.h"
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <string_view>

namespace compositor::imaging {
using Microsoft::WRL::ComPtr;
namespace {
using detail::Apartment;
using detail::cancelled;
using detail::factory;
using detail::Handle;
using detail::ok;
using detail::regularFile;
using detail::TemporaryFile;

// Mac Export As floors its scale at 1%.
constexpr double minimumScale = 0.01;
constexpr std::size_t copyChunk = 1024 * 1024;

std::pair<std::uint32_t, std::uint32_t> fitPreview(std::uint32_t width, std::uint32_t height, std::uint32_t maxWidth, std::uint32_t maxHeight) {
    if (!maxWidth || !maxHeight) return {0, 0};
    const double scale = std::min({1., double(maxWidth) / width, double(maxHeight) / height});
    return {std::max(1U, std::uint32_t(std::floor(width * scale))), std::max(1U, std::uint32_t(std::floor(height * scale)))};
}

// Locale-independent, so a PDF never gets a decimal comma.
std::string number(double value) {
    char text[40];
    const auto end = std::to_chars(text, text + sizeof text, value, std::chars_format::fixed, 4).ptr;
    return {text, end};
}

// Sequential writer for the PDF's temporary file, which TemporaryFile has already created.
class PdfFile {
public:
    explicit PdfFile(const std::filesystem::path& path) {
        file_.value = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file_.value == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open temporary PDF export");
        regularFile(file_.value);
    }
    void write(const void* data, std::size_t size) {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        while (size) {
            DWORD written = 0;
            if (!WriteFile(file_.value, bytes, DWORD(std::min<std::size_t>(size, 1U << 30)), &written, nullptr) || !written) throw std::runtime_error("Cannot write the PDF export");
            bytes += written; size -= written; offset_ += written;
        }
    }
    void write(std::string_view text) { write(text.data(), text.size()); }
    std::uint64_t offset() const { return offset_; }
    void flush() { if (!FlushFileBuffers(file_.value)) throw std::runtime_error("Cannot flush the PDF export"); }
private:
    Handle file_;
    std::uint64_t offset_{};
};

struct DataRange { std::uint64_t offset{}; std::uint32_t length{}; };

std::uint32_t bigEndian(const std::uint8_t* p) { return std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 | std::uint32_t(p[2]) << 8 | p[3]; }

// The IDAT payloads of an 8-bit RGB, non-interlaced PNG written by WIC. Concatenated they are one zlib stream whose rows
// carry PNG filter bytes, which a PDF FlateDecode image reads directly with /Predictor 15, so the pixels are embedded
// losslessly without being decompressed or held in memory.
std::vector<DataRange> pngImageData(const std::filesystem::path& png, std::uint32_t width, std::uint32_t height) {
    std::ifstream in(png, std::ios::binary);
    const auto malformed = [] { return std::runtime_error("The PNG encoder produced an unexpected layout"); };
    std::array<std::uint8_t, 8> signature{};
    if (!in.read(reinterpret_cast<char*>(signature.data()), 8) || signature != std::array<std::uint8_t, 8>{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A}) throw malformed();
    std::vector<DataRange> ranges;
    bool header = false;
    for (;;) {
        std::array<std::uint8_t, 8> chunk{};
        if (!in.read(reinterpret_cast<char*>(chunk.data()), 8)) throw malformed();
        const auto length = bigEndian(chunk.data());
        const std::string_view type(reinterpret_cast<const char*>(chunk.data()) + 4, 4);
        const auto start = std::uint64_t(in.tellg());
        if (type == "IHDR") {
            std::array<std::uint8_t, 13> ihdr{};
            if (length != ihdr.size() || !in.read(reinterpret_cast<char*>(ihdr.data()), ihdr.size())) throw malformed();
            // 8 bits, color type 2 (RGB), deflate, adaptive filtering, no interlace.
            if (bigEndian(ihdr.data()) != width || bigEndian(ihdr.data() + 4) != height || ihdr[8] != 8 || ihdr[9] != 2 || ihdr[10] || ihdr[11] || ihdr[12]) throw malformed();
            header = true;
        } else if (type == "IDAT") {
            if (!header) throw malformed();
            ranges.push_back({start, length});
        } else if (type == "IEND") break;
        in.seekg(std::streamoff(start + length + 4));   // payload and CRC
        if (!in) throw malformed();
    }
    if (ranges.empty()) throw malformed();
    return ranges;
}

// The Windows sRGB profile, embedded so the page is tagged with the same color space as the JPEG export.
std::vector<std::uint8_t> srgbProfile() {
    wchar_t system[MAX_PATH]{};
    if (!GetSystemDirectoryW(system, MAX_PATH)) throw std::runtime_error("Cannot locate Windows sRGB profile");
    std::ifstream in(std::filesystem::path(system) / L"spool/drivers/color/sRGB Color Space Profile.icm", std::ios::binary);
    std::vector<std::uint8_t> profile((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (profile.size() < 128 || profile.size() > 1024 * 1024) throw std::runtime_error("Cannot read Windows sRGB profile");
    return profile;
}

// One page holding one image XObject, the PNG's pixels. Object numbers: 1 catalog, 2 page tree, 3 page, 4 image,
// 5 page content, 6 ICC profile.
void writePdf(PdfFile& pdf, const std::filesystem::path& png, const std::vector<DataRange>& image, ExportSize size, double pageWidth, double pageHeight,
              const std::function<bool()>& check) {
    std::array<std::uint64_t, 7> offsets{};
    const auto object = [&](int id, std::string_view body) { offsets[id] = pdf.offset(); pdf.write(std::to_string(id) + " 0 obj\n"); pdf.write(body); pdf.write("endobj\n"); };
    const auto profile = srgbProfile();
    pdf.write(std::string_view("%PDF-1.4\n%\xE2\xE3\xCF\xD3\n"));
    object(1, "<< /Type /Catalog /Pages 2 0 R >>\n");
    object(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>\n");
    object(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + number(pageWidth) + " " + number(pageHeight) + "] "
              "/Resources << /XObject << /Im0 4 0 R >> >> /Contents 5 0 R >>\n");
    std::uint64_t imageBytes = 0;
    for (const auto& range : image) imageBytes += range.length;
    offsets[4] = pdf.offset();
    pdf.write("4 0 obj\n<< /Type /XObject /Subtype /Image /Width " + std::to_string(size.width) + " /Height " + std::to_string(size.height) +
              " /ColorSpace [/ICCBased 6 0 R] /BitsPerComponent 8 /Filter /FlateDecode /DecodeParms << /Predictor 15 /Colors 3 /BitsPerComponent 8 /Columns " +
              std::to_string(size.width) + " >> /Length " + std::to_string(imageBytes) + " >>\nstream\n");
    std::ifstream in(png, std::ios::binary);
    std::vector<char> buffer(copyChunk);
    for (const auto& range : image) {
        in.seekg(std::streamoff(range.offset));
        for (std::uint32_t done = 0; done < range.length;) {
            cancelled(check);
            const auto count = std::min<std::size_t>(buffer.size(), range.length - done);
            if (!in.read(buffer.data(), std::streamsize(count))) throw std::runtime_error("Cannot read the encoded PDF image");
            pdf.write(buffer.data(), count); done += std::uint32_t(count);
        }
    }
    pdf.write("\nendstream\nendobj\n");
    const std::string content = "q " + number(pageWidth) + " 0 0 " + number(pageHeight) + " 0 0 cm\n/Im0 Do\nQ\n";
    object(5, "<< /Length " + std::to_string(content.size()) + " >>\nstream\n" + content + "endstream\n");
    offsets[6] = pdf.offset();
    pdf.write("6 0 obj\n<< /N 3 /Alternate /DeviceRGB /Length " + std::to_string(profile.size()) + " >>\nstream\n");
    pdf.write(profile.data(), profile.size());
    pdf.write("\nendstream\nendobj\n");
    const auto xref = pdf.offset();
    pdf.write("xref\n0 7\n0000000000 65535 f \n");
    for (int i = 1; i < 7; ++i) { char entry[24]; std::snprintf(entry, sizeof entry, "%010llu 00000 n \n", static_cast<unsigned long long>(offsets[i])); pdf.write(std::string_view(entry, 20)); }
    pdf.write("trailer\n<< /Size 7 /Root 1 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n");
    pdf.flush();
}
}

ExportSize scaledExportSize(std::uint32_t width, std::uint32_t height, double scale) {
    validateExportExtent(width, height);
    if (!std::isfinite(scale) || scale <= 0) throw std::invalid_argument("Invalid PDF export scale");
    const double largest = std::min(double(maxExportSide) / std::max(width, height), std::sqrt(double(maxExportPixels) / (double(width) * height)));
    const double applied = std::min(largest, std::max(minimumScale, scale));
    auto side = [&](std::uint32_t value) { return std::min(maxExportSide, std::uint32_t(std::max<long long>(1, std::llround(value * applied)))); };
    ExportSize size{side(width), side(height)};
    // Rounding both sides up can overshoot the pixel limit by a sliver.
    while (std::uint64_t(size.width) * size.height > maxExportPixels) (size.width >= size.height ? size.width : size.height)--;
    return size;
}

StreamingExportResult encodePdfAtomic(const std::filesystem::path& destination, std::uint32_t width, std::uint32_t height,
    const RgbaRows& rows, const ExportOptions& options, const StreamExportLimits& limits, const ExportProgress& progress) {
    validateExportExtent(width, height);
    if (!rows || options.format != ImageFormat::Pdf || !std::isfinite(options.dpi) || options.dpi <= 0 ||
        !limits.stripeRows || limits.stripeRows > 256 || limits.previewWidth > 1000 || limits.previewHeight > 1000 || limits.maxBufferBytes > 1024ULL * 1024 * 1024)
        throw std::invalid_argument("Invalid PDF export settings");
    const auto size = scaledExportSize(width, height, options.scale);
    const bool scaling = size.width != width || size.height != height;
    cancelled(options.cancelled);

    const auto [previewWidth, previewHeight] = fitPreview(size.width, size.height, limits.previewWidth, limits.previewHeight);
    using Sum = std::array<std::uint64_t, 3>;
    const std::size_t previewPixels = std::size_t(previewWidth) * previewHeight;
    const std::size_t previewStorage = previewPixels * (sizeof(Sum) + 4) + std::size_t(size.width) * sizeof(std::uint32_t);
    constexpr std::size_t renderReserve = 8 * 1024 * 1024;   // as encodeRowsAtomic: the canonical renderer's own buffers
    const std::size_t bytesPerRow = std::size_t(size.width) * (4 + 3);
    if (limits.maxBufferBytes <= previewStorage + renderReserve + bytesPerRow) throw std::invalid_argument("Streaming export buffer budget is too small");
    const auto stripeRows = std::uint32_t(std::min<std::size_t>({size.height, limits.stripeRows, (limits.maxBufferBytes - previewStorage - renderReserve) / bytesPerRow}));
    std::vector<std::uint8_t> rgba(std::size_t(size.width) * stripeRows * 4), rgb(std::size_t(size.width) * stripeRows * 3);
    std::vector<Sum> sums(previewPixels);
    std::vector<std::uint32_t> previewX(size.width);
    for (std::uint32_t x = 0; x < size.width; ++x) previewX[x] = std::uint32_t(std::uint64_t(x) * previewWidth / size.width);
    StreamingExportResult result;
    result.stripeRows = stripeRows;
    result.peakOwnedPixelBytes = rgba.size() + rgb.size() + previewStorage;

    Apartment apartment;
    auto imaging = factory();
    // Where the canvas comes from, a stripe at a time: straight from the renderer, or through WIC's scaler over a
    // premultiplied copy of the whole canvas (a scaled stripe needs source rows on both sides of it).
    ComPtr<IWICBitmapScaler> scaler;
    const auto checkedRows = [&](std::uint32_t top, std::uint32_t count, std::span<std::uint8_t> output, std::size_t stride) {
        rows(top, count, output, stride);
        for (std::uint32_t row = 0; row < count; ++row)
            for (const auto* p = output.data() + std::size_t(row) * stride, *end = p + std::size_t(width) * 4; p != end; p += 4)
                if (p[0] > p[3] || p[1] > p[3] || p[2] > p[3]) throw std::runtime_error("Export rows contain invalid premultiplied pixels");
    };
    if (scaling) {
        ComPtr<IWICBitmap> canvas;
        ok(imaging->CreateBitmap(width, height, GUID_WICPixelFormat32bppPRGBA, WICBitmapCacheOnDemand, &canvas), "Create PDF source canvas");
        {
            const WICRect all{0, 0, INT(width), INT(height)};
            ComPtr<IWICBitmapLock> lock; ok(canvas->Lock(&all, WICBitmapLockWrite, &lock), "Lock PDF source canvas");
            UINT stride = 0, bytes = 0; BYTE* data = nullptr;
            ok(lock->GetStride(&stride), "Read PDF source stride"); ok(lock->GetDataPointer(&bytes, &data), "Read PDF source pixels");
            result.peakOwnedPixelBytes += bytes;
            for (std::uint32_t top = 0; top < height; top += limits.stripeRows) {
                cancelled(options.cancelled);
                const auto count = std::min(limits.stripeRows, height - top);
                checkedRows(top, count, std::span(data + std::size_t(top) * stride, std::size_t(stride) * count), stride);
            }
        }
        // Premultiplied resampling, so transparent pixels do not bleed their color into the edges they border.
        ok(imaging->CreateBitmapScaler(&scaler), "Create PDF scaler");
        ok(scaler->Initialize(canvas.Get(), size.width, size.height, WICBitmapInterpolationModeHighQualityCubic), "Scale the PDF canvas");
    }

    // The page's pixels, over the background, as a lossless 8-bit RGB PNG from which the PDF takes its image data.
    TemporaryFile pngFile(destination);
    {
        ComPtr<IWICStream> stream; ok(imaging->CreateStream(&stream), "Create PDF image stream");
        ok(stream->InitializeFromFilename(pngFile.path.c_str(), GENERIC_WRITE), "Open PDF image stream");
        ComPtr<IWICBitmapEncoder> encoder; ok(imaging->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder), "Create PDF image encoder");
        ok(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "Initialize PDF image encoder");
        ComPtr<IWICBitmapFrameEncode> frame; ComPtr<IPropertyBag2> properties;
        ok(encoder->CreateNewFrame(&frame, &properties), "Create PDF image frame");
        PROPBAG2 filter{}; filter.pstrName = const_cast<LPOLESTR>(L"FilterOption");
        VARIANT adaptive; VariantInit(&adaptive); adaptive.vt = VT_UI1; adaptive.bVal = WICPngFilterAdaptive;
        ok(properties->Write(1, &filter, &adaptive), "Choose adaptive PNG filtering");
        ok(frame->Initialize(properties.Get()), "Initialize PDF image frame");
        ok(frame->SetSize(size.width, size.height), "Set PDF image size");
        GUID format = GUID_WICPixelFormat24bppBGR;
        ok(frame->SetPixelFormat(&format), "Choose PDF image pixel format");
        if (format != GUID_WICPixelFormat24bppBGR) throw std::runtime_error("Encoder changed the required PDF image pixel format");
        const std::array<std::uint8_t, 3> matte{options.matteR, options.matteG, options.matteB};
        for (std::uint32_t top = 0; top < size.height; top += stripeRows) {
            cancelled(options.cancelled);
            const auto count = std::min(stripeRows, size.height - top);
            if (scaler) {
                const WICRect area{0, INT(top), INT(size.width), INT(count)};
                ok(scaler->CopyPixels(&area, size.width * 4, UINT(rgba.size()), rgba.data()), "Scale PDF canvas rows");
            } else checkedRows(top, count, std::span(rgba).first(std::size_t(size.width) * count * 4), std::size_t(size.width) * 4);
            for (std::uint32_t row = 0; row < count; ++row) {
                auto* sum = sums.data() + std::size_t(std::uint64_t(top + row) * previewHeight / size.height) * previewWidth;
                for (std::uint32_t x = 0; x < size.width; ++x) {
                    const auto* p = rgba.data() + (std::size_t(row) * size.width + x) * 4;
                    auto* q = rgb.data() + (std::size_t(row) * size.width + x) * 3;
                    // The JPEG matte formula: the background shows through in proportion to the missing alpha.
                    for (int c = 0; c < 3; ++c) q[2 - c] = std::uint8_t(std::min(255U, unsigned(p[c]) + (unsigned(matte[c]) * (255 - p[3]) + 127) / 255));
                    if (previewPixels) for (int c = 0; c < 3; ++c) sum[previewX[x]][c] += q[2 - c];
                }
            }
            ok(frame->WritePixels(count, size.width * 3, UINT(std::size_t(size.width) * count * 3), rgb.data()), "Encode PDF image rows");
            if (progress) progress(std::uint32_t(std::uint64_t(top + count) * height / size.height), height);
        }
        cancelled(options.cancelled);
        ok(frame->Commit(), "Commit PDF image frame"); ok(encoder->Commit(), "Commit PDF image container");
    }
    cancelled(options.cancelled);
    if (previewPixels) {
        result.preview = {previewWidth, previewHeight, std::size_t(previewWidth) * 4, std::vector<std::uint8_t>(previewPixels * 4)};
        auto boundary = [](std::uint32_t i, std::uint32_t source, std::uint32_t target) { return (std::uint64_t(i) * source + target - 1) / target; };
        for (std::uint32_t y = 0; y < previewHeight; ++y) for (std::uint32_t x = 0; x < previewWidth; ++x) {
            const auto count = (boundary(x + 1, size.width, previewWidth) - boundary(x, size.width, previewWidth)) * (boundary(y + 1, size.height, previewHeight) - boundary(y, size.height, previewHeight));
            auto* out = result.preview.pixels.data() + (std::size_t(y) * previewWidth + x) * 4;
            for (int c = 0; c < 3; ++c) out[c] = std::uint8_t((sums[std::size_t(y) * previewWidth + x][c] + count / 2) / count);
            out[3] = 255;
        }
    }

    // The page is the export's pixels at the document's resolution; PDF points are 1/72 inch.
    const auto image = pngImageData(pngFile.path, size.width, size.height);
    TemporaryFile pdfFile(destination);
    {
        PdfFile pdf(pdfFile.path);
        writePdf(pdf, pngFile.path, image, size, size.width * 72.0 / options.dpi, size.height * 72.0 / options.dpi, options.cancelled);
    }
    result.encodedBytes = std::filesystem::file_size(pdfFile.path);
    pdfFile.replace(destination, options.cancelled);
    return result;
}
}

#include "export_temporary_file.h"
#include "cmyk.h"
#include <propvarutil.h>
#include <array>
#include <cmath>
#include <sstream>

namespace compositor::imaging {
using Microsoft::WRL::ComPtr;
namespace {
using detail::Apartment;
using detail::cancelled;
using detail::factory;
using detail::fitPreview;
using detail::Handle;
using detail::ok;
using detail::regularFile;
using detail::TemporaryFile;
RgbaImage jpegPreview(IWICImagingFactory* imaging, const std::filesystem::path& path, UINT requestedWidth, UINT requestedHeight,
                      std::size_t availableBytes, std::size_t& peakBytes, const std::function<bool()>& check,bool cmyk) {
    cancelled(check);
    ComPtr<IWICBitmapDecoder> decoder;
    ok(imaging->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder), "Open encoded JPEG preview");
    ComPtr<IWICBitmapFrameDecode> frame; ok(decoder->GetFrame(0, &frame), "Read encoded JPEG preview frame");
    ComPtr<IWICBitmapSourceTransform> scaled;
    ok(frame.As(&scaled), "JPEG codec does not support bounded native preview downscaling");
    UINT width = requestedWidth, height = requestedHeight;
    ok(scaled->GetClosestSize(&width, &height), "Choose bounded JPEG decode size");
    GUID format = cmyk?GUID_WICPixelFormat32bppCMYK:GUID_WICPixelFormat32bppBGRA;
    ok(scaled->GetClosestPixelFormat(&format), "Choose bounded JPEG decode format");
    if(cmyk&&format!=GUID_WICPixelFormat32bppCMYK)throw std::runtime_error("JPEG decoder cannot provide bounded CMYK preview pixels");
    UINT channels = (format == GUID_WICPixelFormat32bppBGRA||format==GUID_WICPixelFormat32bppCMYK) ? 4 : format == GUID_WICPixelFormat24bppBGR ? 3 : 0;
    if (!channels || !width || !height || width > 30000 || height > 30000) throw std::runtime_error("Unsupported bounded JPEG preview format");
    const auto bytes = std::uint64_t(width) * height * channels;
    const auto outputBytes = std::uint64_t(requestedWidth) * requestedHeight * 4;
    // The native JPEG decoder supports downscaling by up to eight. A 100MP
    // source therefore needs at most 1,562,500 intermediate pixels.
    if (std::uint64_t(width) * height > 2000000 || bytes * 2 + outputBytes > availableBytes || bytes > UINT_MAX)
        throw std::runtime_error("JPEG preview exceeds the bounded decode budget");
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(bytes));
    ok(scaled->CopyPixels(nullptr, width, height, &format, WICBitmapTransformRotate0, width * channels,
                          UINT(bytes), pixels.data()), "Decode bounded encoded JPEG pixels");
    cancelled(check);
    ComPtr<IWICBitmap> bitmap;
    ok(imaging->CreateBitmapFromMemory(width, height, format, width * channels, UINT(bytes), pixels.data(), &bitmap), "Create small JPEG preview bitmap");
    // Apply the same declared-profile normalization as normal import. Raw JPEG
    // decoder channels can differ by a byte from the normalized document space.
    UINT profileCount = 0; ok(frame->GetColorContexts(0, nullptr, &profileCount), "Read encoded JPEG profile count");
    if (profileCount != 1) throw std::runtime_error("Encoded JPEG is missing its color profile");
    ComPtr<IWICColorContext> profile, srgb;
    ok(imaging->CreateColorContext(&profile), "Create encoded JPEG profile context");
    IWICColorContext* contexts[]{profile.Get()}; ok(frame->GetColorContexts(1, contexts, &profileCount), "Read encoded JPEG profile");
    ok(imaging->CreateColorContext(&srgb), "Create preview sRGB context"); ok(srgb->InitializeFromExifColorSpace(1), "Initialize preview sRGB context");
    ComPtr<IWICColorTransform> normalized; ok(imaging->CreateColorTransformer(&normalized), "Create JPEG preview profile transform");
    ok(normalized->Initialize(bitmap.Get(), profile.Get(), srgb.Get(), GUID_WICPixelFormat32bppRGBA), "Normalize encoded JPEG preview profile");
    ComPtr<IWICBitmapScaler> scaler; ok(imaging->CreateBitmapScaler(&scaler), "Create JPEG preview scaler");
    ok(scaler->Initialize(normalized.Get(), requestedWidth, requestedHeight, WICBitmapInterpolationModeFant), "Scale encoded JPEG preview");
    ComPtr<IWICFormatConverter> converter; ok(imaging->CreateFormatConverter(&converter), "Create JPEG preview converter");
    ok(converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom), "Convert JPEG preview to RGBA");
    RgbaImage result{requestedWidth, requestedHeight, std::size_t(requestedWidth) * 4, std::vector<std::uint8_t>(static_cast<std::size_t>(outputBytes))};
    ok(converter->CopyPixels(nullptr, UINT(result.stride), UINT(result.pixels.size()), result.pixels.data()), "Read encoded JPEG preview");
    peakBytes = std::max(peakBytes, static_cast<std::size_t>(bytes * 2 + outputBytes));
    cancelled(check);
    return result;
}
void writeProfile(IWICImagingFactory* imaging, IWICBitmapFrameEncode* frame, ImageFormat format,CmykProfilePtr profile) {
    if(profile){ComPtr<IWICColorContext> color;ok(imaging->CreateColorContext(&color),"Create CMYK export context");ok(color->InitializeFromMemory(const_cast<BYTE*>(profile->bytes.data()),UINT(profile->bytes.size())),"Read CMYK output profile");IWICColorContext* contexts[]{color.Get()};ok(frame->SetColorContexts(1,contexts),"Embed CMYK output profile");
    }else if (format == ImageFormat::Png) {
        ComPtr<IWICMetadataQueryWriter> metadata; ok(frame->GetMetadataQueryWriter(&metadata), "Get PNG export metadata");
        PROPVARIANT value; PropVariantInit(&value); value.vt = VT_UI1; value.bVal = 0;
        ok(metadata->SetMetadataByName(L"/sRGB/RenderingIntent", &value), "Write PNG sRGB declaration");
    } else {
        wchar_t system[MAX_PATH]{};
        if (!GetSystemDirectoryW(system, MAX_PATH)) throw std::runtime_error("Cannot locate Windows sRGB profile");
        auto path = std::filesystem::path(system) / L"spool/drivers/color/sRGB Color Space Profile.icm";
        ComPtr<IWICColorContext> color; ok(imaging->CreateColorContext(&color), "Create export color context");
        ok(color->InitializeFromFilename(path.c_str()), "Load Windows sRGB profile");
        IWICColorContext* colors[]{color.Get()}; ok(frame->SetColorContexts(1, colors), "Embed export sRGB profile");
    }
}
}

void validateExportExtent(std::uint32_t width, std::uint32_t height) {
    if (!width || !height || width > maxExportSide || height > maxExportSide || std::uint64_t(width) * height > maxExportPixels)
        throw std::invalid_argument("Image export supports canvases up to 100 megapixels and 30,000 pixels per side.");
}

StreamingExportResult encodeRowsAtomic(const std::filesystem::path& destination, std::uint32_t width, std::uint32_t height,
    const RgbaRows& rows, const ExportOptions& options, const StreamExportLimits& limits, const ExportProgress& progress) {
    validateExportExtent(width, height);
    if (!rows || !std::isfinite(options.dpi) || options.dpi <= 0 || !std::isfinite(options.jpegQuality) ||
        (options.format != ImageFormat::Png && options.format != ImageFormat::Jpeg && options.format != ImageFormat::Tiff) || !limits.stripeRows || limits.stripeRows > 256 ||
        limits.previewWidth > 1000 || limits.previewHeight > 1000 || limits.maxBufferBytes > 1024ULL * 1024 * 1024)
        throw std::invalid_argument("Invalid streaming image export settings");
    if(options.cmykProfile&&options.format==ImageFormat::Png)throw std::invalid_argument("PNG does not support CMYK output");
    std::unique_ptr<CmykTransform> color;if(options.cmykProfile)color=std::make_unique<CmykTransform>(options.cmykProfile);
    cancelled(options.cancelled);
    const auto [previewWidth, previewHeight] = fitPreview(width, height, limits.previewWidth, limits.previewHeight);
    const bool losslessPreview = options.format != ImageFormat::Jpeg && previewWidth && previewHeight;
    using Accumulator = std::array<std::uint64_t, 4>;
    const std::size_t previewPixels = std::size_t(previewWidth) * previewHeight;
    const std::size_t previewStorage = losslessPreview ? previewPixels * (sizeof(Accumulator) + 4) + std::size_t(width) * sizeof(UINT) : 0;
    // Reserve 8MiB for the canonical renderer's bounded tile/adjustment buffers.
    constexpr std::size_t renderReserve = 8 * 1024 * 1024;
    const UINT channels = options.format == ImageFormat::Jpeg && !color ? 3U : 4U;
    const std::size_t bytesPerRow = std::size_t(width) * (4 + channels);
    if (limits.maxBufferBytes <= previewStorage + renderReserve + bytesPerRow)
        throw std::invalid_argument("Streaming export buffer budget is too small");
    const auto stripeRows = UINT(std::min<std::size_t>({height, limits.stripeRows, (limits.maxBufferBytes - previewStorage - renderReserve) / bytesPerRow}));
    std::vector<std::uint8_t> rgba(std::size_t(width) * stripeRows * 4), encoded(std::size_t(width) * stripeRows * channels);
    std::vector<Accumulator> sums(losslessPreview ? previewPixels : 0);
    std::vector<UINT> previewX(losslessPreview ? width : 0);
    if (losslessPreview) for (UINT x = 0; x < width; ++x) previewX[x] = UINT(std::uint64_t(x) * previewWidth / width);
    StreamingExportResult result; result.stripeRows = stripeRows;
    result.peakOwnedPixelBytes = rgba.size() + encoded.size() + previewStorage;
    Apartment apartment; auto imaging = factory(); TemporaryFile temporary(destination);
    {
        ComPtr<IWICStream> stream; ok(imaging->CreateStream(&stream), "Create streaming export file");
        ok(stream->InitializeFromFilename(temporary.path.c_str(), GENERIC_WRITE), "Open streaming export file");
        ComPtr<IWICBitmapEncoder> encoder;
        ok(imaging->CreateEncoder(options.format == ImageFormat::Png ? GUID_ContainerFormatPng : options.format==ImageFormat::Tiff?GUID_ContainerFormatTiff:GUID_ContainerFormatJpeg, nullptr, &encoder), "Create streaming image encoder");
        ok(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "Initialize scanline encoder without image cache");
        ComPtr<IWICBitmapFrameEncode> frame; ComPtr<IPropertyBag2> properties;
        ok(encoder->CreateNewFrame(&frame, &properties), "Create streaming export frame");
        if (options.format == ImageFormat::Jpeg) {
            PROPBAG2 property{}; property.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
            VARIANT value; VariantInit(&value); value.vt = VT_R4; value.fltVal = float(std::clamp(options.jpegQuality, 0., 1.));
            ok(properties->Write(1, &property, &value), "Set JPEG export quality");
        }
        if (options.format == ImageFormat::Tiff) {
            PROPBAG2 property{}; property.pstrName = const_cast<LPOLESTR>(L"TiffCompressionMethod");
            VARIANT value; VariantInit(&value); value.vt = VT_UI1; value.bVal = WICTiffCompressionLZW;
            ok(properties->Write(1, &property, &value), "Set lossless TIFF compression");
        }
        ok(frame->Initialize(properties.Get()), "Initialize streaming export frame");
        ok(frame->SetSize(width, height), "Set export dimensions"); ok(frame->SetResolution(options.dpi, options.dpi), "Set export resolution");
        GUID format = color?GUID_WICPixelFormat32bppCMYK:options.format!=ImageFormat::Jpeg?GUID_WICPixelFormat32bppBGRA:GUID_WICPixelFormat24bppBGR;
        const auto requested = format; ok(frame->SetPixelFormat(&format), "Choose export pixel format");
        if (format != requested) throw std::runtime_error("Encoder changed the required export pixel format");
        writeProfile(imaging.Get(), frame.Get(), options.format,options.cmykProfile);
        const std::array<std::uint8_t, 3> matte{options.matteR, options.matteG, options.matteB};
        for (UINT y = 0; y < height; y += stripeRows) {
            cancelled(options.cancelled); const UINT count = std::min(stripeRows, height - y);
            rows(y, count, std::span(rgba).first(std::size_t(width) * count * 4), std::size_t(width) * 4);
            if(color){
                const auto bytes=size_t(width)*count*4;
                color->encode(std::span(rgba).first(bytes),std::span(encoded).first(bytes),matte);
                if(losslessPreview){color->decode(std::span(encoded).first(bytes),std::span(rgba).first(bytes));
                    for(UINT row=0;row<count;++row)for(UINT x=0;x<width;++x){auto& sum=sums[size_t(uint64_t(y+row)*previewHeight/height)*previewWidth+previewX[x]];for(UINT c=0;c<4;++c)sum[c]+=rgba[(size_t(row)*width+x)*4+c];}}
            }else for (UINT row = 0; row < count; ++row) {
                cancelled(options.cancelled);
                for (UINT x = 0; x < width; ++x) {
                    const auto* p = rgba.data() + (std::size_t(row) * width + x) * 4;
                    auto* q = encoded.data() + (std::size_t(row) * width + x) * channels;
                    if (p[0] > p[3] || p[1] > p[3] || p[2] > p[3]) throw std::runtime_error("Export rows contain invalid premultiplied pixels");
                    for (UINT c = 0; c < 3; ++c) q[2 - c] = channels == 3 ? std::uint8_t(std::min(255U, unsigned(p[c]) + (unsigned(matte[c]) * (255 - p[3]) + 127) / 255))
                        : p[3] ? std::uint8_t(std::min(255U, (unsigned(p[c]) * 255 + p[3] / 2) / p[3])) : 0;
                    if (channels == 4) q[3] = p[3];
                    if (losslessPreview) {
                        auto& sum = sums[std::size_t(std::uint64_t(y + row) * previewHeight / height) * previewWidth + previewX[x]];
                        for (UINT c = 0; c < 3; ++c) sum[c] += (unsigned(q[2 - c]) * p[3] + 127) / 255;
                        sum[3] += p[3];
                    }
                }
            }
            ok(frame->WritePixels(count, width * channels, width * count * channels, encoded.data()), "Encode image scanlines");
            if (progress) progress(y + count, height);
        }
        cancelled(options.cancelled); ok(frame->Commit(), "Commit streaming image frame"); ok(encoder->Commit(), "Commit streaming image container");
    }
    cancelled(options.cancelled);
    if (losslessPreview) {
        result.preview = {previewWidth, previewHeight, std::size_t(previewWidth) * 4, std::vector<std::uint8_t>(previewPixels * 4)};
        auto boundary = [](UINT i, UINT source, UINT target) { return (std::uint64_t(i) * source + target - 1) / target; };
        for (UINT y = 0; y < previewHeight; ++y) for (UINT x = 0; x < previewWidth; ++x) {
            const auto count = (boundary(x + 1, width, previewWidth) - boundary(x, width, previewWidth)) * (boundary(y + 1, height, previewHeight) - boundary(y, height, previewHeight));
            for (UINT c = 0; c < 4; ++c) result.preview.pixels[(std::size_t(y) * previewWidth + x) * 4 + c] = std::uint8_t((sums[std::size_t(y) * previewWidth + x][c] + count / 2) / count);
        }
    } else if (previewWidth && previewHeight) {
        // Release stripe allocations before decoding the encoded JPEG thumbnail.
        std::vector<std::uint8_t>().swap(rgba); std::vector<std::uint8_t>().swap(encoded);
        result.preview = jpegPreview(imaging.Get(), temporary.path, previewWidth, previewHeight, limits.maxBufferBytes - renderReserve,
                                     result.peakOwnedPixelBytes, options.cancelled,bool(color));
    }
    result.encodedBytes = std::filesystem::file_size(temporary.path);
    temporary.replace(destination, options.cancelled);
    return result;
}

void copyEncodedAtomic(const std::filesystem::path& source, const std::filesystem::path& destination, const std::function<bool()>& check) {
    cancelled(check); const auto inputPath=win32FilePath(source); TemporaryFile temporary(destination);
    {
        Handle input{CreateFileW(inputPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr)};
        Handle output{CreateFileW(temporary.path.c_str(), GENERIC_WRITE, 0, nullptr, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
        if (input.value == INVALID_HANDLE_VALUE || output.value == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open encoded image for export");
        regularFile(input.value); regularFile(output.value);
        std::vector<std::uint8_t> buffer(1024 * 1024);
        for (;;) {
            cancelled(check); DWORD count = 0;
            if (!ReadFile(input.value, buffer.data(), DWORD(buffer.size()), &count, nullptr)) throw std::runtime_error("Cannot read encoded image");
            if (!count) break;
            DWORD offset = 0;
            while (offset < count) { DWORD written = 0; if (!WriteFile(output.value, buffer.data() + offset, count - offset, &written, nullptr) || !written) throw std::runtime_error("Cannot write encoded image"); offset += written; }
        }
        if (!FlushFileBuffers(output.value)) throw std::runtime_error("Cannot flush exported image");
    }
    temporary.replace(destination, check);
}
}

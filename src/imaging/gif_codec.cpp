#include "gif_codec.h"
#include "export_temporary_file.h"
#include "win32_file_path.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <unordered_map>
#include <propvarutil.h>

namespace compositor::imaging {
namespace {
using Microsoft::WRL::ComPtr;
using detail::ok;
using detail::Apartment;
using detail::factory;
constexpr unsigned maxFrames = 4096;

// Metadata fields are optional in GIF. Accept only the integral types returned by
// the WIC GIF reader, rejecting unexpected types rather than interpreting a union.
unsigned metadata(IWICMetadataQueryReader* reader, const wchar_t* name, unsigned fallback) {
    if (!reader) return fallback;
    PROPVARIANT value; PropVariantInit(&value);
    const HRESULT hr = reader->GetMetadataByName(name, &value);
    if (hr == WINCODEC_ERR_PROPERTYNOTFOUND || hr == WINCODEC_ERR_PROPERTYNOTSUPPORTED) { PropVariantClear(&value); return fallback; }
    ok(hr, "Read GIF metadata");
    unsigned n = 0;
    switch (value.vt) {
    case VT_UI1: n = value.bVal; break;
    case VT_UI2: n = value.uiVal; break;
    case VT_UI4: n = value.ulVal; break;
    case VT_BOOL: n = value.boolVal != VARIANT_FALSE; break;
    default: PropVariantClear(&value); throw std::runtime_error("Invalid GIF metadata type");
    }
    PropVariantClear(&value);
    return n;
}
ComPtr<IWICMetadataQueryReader> reader(IWICBitmapDecoder* decoder) {
    ComPtr<IWICMetadataQueryReader> r; ok(decoder->GetMetadataQueryReader(&r), "Read GIF screen metadata"); return r;
}
ComPtr<IWICMetadataQueryReader> reader(IWICBitmapFrameDecode* frame) {
    ComPtr<IWICMetadataQueryReader> r; const auto hr = frame->GetMetadataQueryReader(&r);
    if (hr != WINCODEC_ERR_UNSUPPORTEDOPERATION) ok(hr, "Read GIF frame metadata");
    return r;
}
void writeByte(std::ostream& out, unsigned value) { out.put(static_cast<char>(value)); }
void writeWord(std::ostream& out, unsigned value) { writeByte(out, value & 255); writeByte(out, (value >> 8) & 255); }
void writeText(std::ostream& out, const char* value, std::streamsize size) { out.write(value, size); }
struct SubBlocks {
    std::ostream& out;
    std::array<unsigned char, 255> data{};
    unsigned size{};
    unsigned bits{};
    unsigned bitCount{};
    void byte(unsigned b) { data[size++] = static_cast<unsigned char>(b); if (size == data.size()) flush(); }
    void flush() { if (size) { writeByte(out, size); out.write(reinterpret_cast<const char*>(data.data()), size); size = 0; } }
    void code(unsigned code) { bits |= code << bitCount; bitCount += 9; while (bitCount >= 8) { byte(bits & 255); bits >>= 8; bitCount -= 8; } }
    void finish() { if (bitCount) byte(bits & 255); flush(); writeByte(out, 0); }
};
void validateFrame(const RgbaImage& image, const std::function<bool()>& check) {
    checkedBytes(image.width,image.height,4);
    if (image.stride < std::size_t(image.width)*4 || image.stride > std::numeric_limits<std::size_t>::max()/image.height || image.pixels.size() < image.stride*image.height)
        throw std::runtime_error("Invalid GIF frame buffer");
    for (unsigned y=0;y<image.height;++y) {
        detail::cancelled(check);
        for (unsigned x=0;x<image.width;++x) {
            const auto* p=image.pixels.data()+std::size_t(y)*image.stride+std::size_t(x)*4;
            if (p[0]>p[3] || p[1]>p[3] || p[2]>p[3]) throw std::runtime_error("GIF frame is not premultiplied");
        }
    }
}
unsigned straight(const std::uint8_t* p,int c){return std::min(255U,(unsigned(p[c])*255U+p[3]/2)/p[3]);}
struct GifPalette {
    std::array<std::array<std::uint8_t,3>,256> colors{};
    unsigned count{};
    bool transparent{};
    std::unordered_map<unsigned,unsigned> exact;
};
unsigned packedRgb(unsigned r,unsigned g,unsigned b){return (r<<16)|(g<<8)|b;}
unsigned nearestIndex(const GifPalette& palette,unsigned r,unsigned g,unsigned b){
    if(auto found=palette.exact.find(packedRgb(r,g,b));found!=palette.exact.end())return found->second;
    unsigned best=palette.transparent?1:0,bestD=~0u;
    for(unsigned i=palette.transparent?1:0;i<palette.count;++i){
        const int dr=int(palette.colors[i][0])-int(r),dg=int(palette.colors[i][1])-int(g),db=int(palette.colors[i][2])-int(b);
        const unsigned d=unsigned(dr*dr+dg*dg+db*db);
        if(d<bestD){bestD=d;best=i;if(!d)break;}
    }
    return best;
}
GifPalette makePalette(const RgbaImage& image,const std::function<bool()>& check){
    GifPalette palette;
    bool anyTransparent=false;
    for(unsigned y=0;y<image.height;++y){
        detail::cancelled(check);
        for(unsigned x=0;x<image.width;++x)if(image.pixels[std::size_t(y)*image.stride+std::size_t(x)*4+3]<128)anyTransparent=true;
    }
    palette.transparent=anyTransparent;
    const unsigned maxColors=anyTransparent?255u:256u;
    const unsigned first=anyTransparent?1u:0u;
    std::vector<std::array<std::uint8_t,3>> unique;
    std::unordered_map<unsigned,unsigned> indexOf;
    std::vector<std::uint32_t> n(32768),sr(32768),sg(32768),sb(32768);
    bool overflow=false;
    for(unsigned y=0;y<image.height;++y){
        detail::cancelled(check);
        for(unsigned x=0;x<image.width;++x){
            const auto* p=image.pixels.data()+std::size_t(y)*image.stride+std::size_t(x)*4;
            if(p[3]<128)continue;
            const unsigned r=straight(p,0),g=straight(p,1),b=straight(p,2),key=packedRgb(r,g,b);
            if(!overflow&&indexOf.emplace(key,unsigned(unique.size())).second){
                unique.push_back({std::uint8_t(r),std::uint8_t(g),std::uint8_t(b)});
                if(unique.size()>maxColors)overflow=true;
            }
            const unsigned i=((r>>3)<<10)|((g>>3)<<5)|(b>>3);
            n[i]+=1;sr[i]+=r;sg[i]+=g;sb[i]+=b;
        }
    }
    if(!overflow){
        palette.count=first+unsigned(unique.size());
        for(unsigned i=0;i<unique.size();++i){
            palette.colors[first+i]=unique[i];
            palette.exact[packedRgb(unique[i][0],unique[i][1],unique[i][2])]=first+i;
        }
        if(!palette.count)palette.count=1;
        return palette;
    }
    struct Box{int r0,r1,g0,g1,b0,b1;std::uint32_t pop;};
    auto shrink=[&](Box box){
        Box out{31,0,31,0,31,0,0};
        for(int r=box.r0;r<=box.r1;++r)for(int g=box.g0;g<=box.g1;++g)for(int b=box.b0;b<=box.b1;++b){
            const unsigned i=unsigned((r<<10)|(g<<5)|b);if(!n[i])continue;
            out.r0=std::min(out.r0,r);out.r1=std::max(out.r1,r);out.g0=std::min(out.g0,g);out.g1=std::max(out.g1,g);out.b0=std::min(out.b0,b);out.b1=std::max(out.b1,b);out.pop+=n[i];
        }
        return out.pop?out:box;
    };
    std::vector<Box> boxes{shrink({0,31,0,31,0,31,0})};
    while(boxes.size()<maxColors){
        detail::cancelled(check);
        size_t split=boxes.size();int bestRange=-1;std::uint32_t bestPop=0;
        for(size_t i=0;i<boxes.size();++i){
            const auto& box=boxes[i];
            const int range=std::max({box.r1-box.r0,box.g1-box.g0,box.b1-box.b0});
            if(range<=0||box.pop<2)continue;
            if(range>bestRange||(range==bestRange&&box.pop>bestPop)){bestRange=range;bestPop=box.pop;split=i;}
        }
        if(split==boxes.size())break;
        Box box=boxes[split];
        const int dr=box.r1-box.r0,dg=box.g1-box.g0,db=box.b1-box.b0;
        const int axis=dr>=dg&&dr>=db?0:dg>=db?1:2;
        std::array<std::uint32_t,32> along{};
        for(int r=box.r0;r<=box.r1;++r)for(int g=box.g0;g<=box.g1;++g)for(int b=box.b0;b<=box.b1;++b){
            const unsigned i=unsigned((r<<10)|(g<<5)|b);if(!n[i])continue;
            along[unsigned(axis==0?r:axis==1?g:b)]+=n[i];
        }
        const int lo=axis==0?box.r0:axis==1?box.g0:box.b0,hi=axis==0?box.r1:axis==1?box.g1:box.b1;
        std::uint32_t run=0;int mid=lo;
        for(int k=lo;k<hi;++k){run+=along[unsigned(k)];if(run*2>=box.pop){mid=k;break;}}
        Box left=box,right=box;
        if(axis==0){left.r1=mid;right.r0=mid+1;}else if(axis==1){left.g1=mid;right.g0=mid+1;}else{left.b1=mid;right.b0=mid+1;}
        left=shrink(left);right=shrink(right);
        if(!left.pop||!right.pop)break;
        boxes[split]=left;boxes.push_back(right);
    }
    palette.count=first;
    for(const auto& box:boxes){
        if(palette.count>=first+maxColors)break;
        std::uint64_t r=0,g=0,b=0,pop=0;
        for(int br=box.r0;br<=box.r1;++br)for(int bg=box.g0;bg<=box.g1;++bg)for(int bb=box.b0;bb<=box.b1;++bb){
            const unsigned i=unsigned((br<<10)|(bg<<5)|bb);r+=sr[i];g+=sg[i];b+=sb[i];pop+=n[i];
        }
        if(!pop)continue;
        palette.colors[palette.count]={std::uint8_t((r+pop/2)/pop),std::uint8_t((g+pop/2)/pop),std::uint8_t((b+pop/2)/pop)};
        ++palette.count;
    }
    if(palette.count<=first)palette.count=first+1;
    return palette;
}
}

GifAnimation decodeGif(const std::filesystem::path& path, const ImportOptions& options) {
    checkCancelled(options); Apartment apartment; auto f = factory();
    ComPtr<IWICBitmapDecoder> decoder; const auto filename = win32FilePath(path);
    ok(f->CreateDecoderFromFilename(filename.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder), "Open GIF");
    GUID type{}; ok(decoder->GetContainerFormat(&type), "Read GIF container");
    if (type != GUID_ContainerFormatGif) throw std::runtime_error("File is not a GIF image");
    UINT count{}; ok(decoder->GetFrameCount(&count), "Count GIF frames");
    if (!count || count > maxFrames) throw std::runtime_error("Invalid GIF frame count");
    auto screen = reader(decoder.Get());
    const unsigned width = metadata(screen.Get(), L"/logscrdesc/Width", 0);
    const unsigned height = metadata(screen.Get(), L"/logscrdesc/Height", 0);
    const auto bytes = checkedBytes(width, height, 4, options);
    // All frames are retained for editing. Include the working canvas and the
    // disposal-previous snapshot in the allocation budget as well.
    if (std::uint64_t(width) * height > options.remainingPixels / count ||
        std::uint64_t(bytes) > options.maxWorkingBytes / (std::uint64_t(count) + 3))
        throw std::runtime_error("GIF exceeds cumulative frame budget");
    const unsigned backgroundIndex = metadata(screen.Get(), L"/logscrdesc/BackgroundColorIndex", 0);
    std::array<std::uint8_t,4> background{};
    ComPtr<IWICPalette> palette; ok(f->CreatePalette(&palette), "Create GIF palette");
    if (SUCCEEDED(decoder->CopyPalette(palette.Get()))) {
        UINT n{}; ok(palette->GetColorCount(&n), "Read GIF palette size");
        if (n > 256) throw std::runtime_error("Invalid GIF palette");
        std::array<WICColor,256> colors{}; UINT actual{};
        ok(palette->GetColors(n, colors.data(), &actual), "Read GIF palette");
        if (backgroundIndex < actual) { const auto c=colors[backgroundIndex]; background={std::uint8_t(c>>16),std::uint8_t(c>>8),std::uint8_t(c),255}; }
    }
    std::vector<std::uint8_t> canvas(bytes, 0), saved;
    GifAnimation result; result.frames.reserve(count);
    unsigned previousDisposal=0, px=0, py=0, pw=0, ph=0; std::array<std::uint8_t,4> previousBackground{};
    for (UINT index=0; index<count; ++index) {
        checkCancelled(options);
        if (previousDisposal == 2) {
            for (unsigned y=py; y<py+ph; ++y) for (unsigned x=px; x<px+pw; ++x)
                std::copy(previousBackground.begin(), previousBackground.end(), canvas.begin()+(std::size_t(y)*width+x)*4);
        } else if (previousDisposal == 3) {
            if (saved.size() != bytes) throw std::runtime_error("Invalid GIF restore state");
            canvas.swap(saved);
        }
        ComPtr<IWICBitmapFrameDecode> frame; ok(decoder->GetFrame(index, &frame), "Read GIF frame");
        auto meta=reader(frame.Get());
        const unsigned x=metadata(meta.Get(),L"/imgdesc/Left",0), y=metadata(meta.Get(),L"/imgdesc/Top",0);
        UINT w{},h{}; ok(frame->GetSize(&w,&h), "Read GIF frame dimensions");
        if (!w || !h || x > width || y > height || w > width-x || h > height-y)
            throw std::runtime_error("GIF frame exceeds logical screen");
        const unsigned disposal=metadata(meta.Get(),L"/grctlext/Disposal",0);
        if (disposal > 3) throw std::runtime_error("Unsupported GIF disposal mode");
        const bool transparent=metadata(meta.Get(),L"/grctlext/TransparencyFlag",0)!=0;
        const unsigned transparentIndex=metadata(meta.Get(),L"/grctlext/TransparentColorIndex",0);
        auto clearColor=(transparent && transparentIndex==backgroundIndex) ? std::array<std::uint8_t,4>{} : background;
        if (index == 0 && clearColor[3]) {
            for (std::size_t i=0;i<bytes;i+=4) std::copy(clearColor.begin(),clearColor.end(),canvas.begin()+i);
        }
        if (disposal == 3) saved=canvas; else saved.clear();
        ComPtr<IWICFormatConverter> convert; ok(f->CreateFormatConverter(&convert), "Create GIF frame converter");
        ok(convert->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom), "Convert GIF frame");
        const auto frameBytes=std::uint64_t(w)*h*4;
        if (frameBytes > std::numeric_limits<UINT>::max()) throw std::runtime_error("GIF frame exceeds decoder buffer limit");
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(frameBytes));
        ok(convert->CopyPixels(nullptr,w*4,static_cast<UINT>(frameBytes),pixels.data()), "Decode GIF pixels");
        for (unsigned row=0;row<h;++row) {
            checkCancelled(options);
            for (unsigned col=0;col<w;++col) {
                const auto* src=pixels.data()+(std::size_t(row)*w+col)*4;
                if (src[3] == 0) continue;
                auto* dest=canvas.data()+(std::size_t(y+row)*width+x+col)*4;
                const unsigned a=src[3], inv=255-a;
                for (int c=0;c<3;++c) dest[c]=std::uint8_t((unsigned(src[c])*a+127)/255+(unsigned(dest[c])*inv+127)/255);
                dest[3]=std::uint8_t(a+(unsigned(dest[3])*inv+127)/255);
            }
        }
        result.frames.push_back({RgbaImage{width,height,std::size_t(width)*4,canvas}, metadata(meta.Get(),L"/grctlext/Delay",0)});
        previousDisposal=disposal; px=x;py=y;pw=w;ph=h;previousBackground=clearColor;
    }
    return result;
}

void encodeGif(const std::filesystem::path& destination, const GifAnimation& animation, const std::function<bool()>& cancelled) {
    if (animation.frames.empty() || animation.frames.size()>maxFrames) throw std::runtime_error("Invalid GIF frame count");
    const auto width=animation.frames.front().image.width, height=animation.frames.front().image.height;
    if (width>65535 || height>65535) throw std::runtime_error("GIF dimensions exceed format limit");
    const auto pixels=std::uint64_t(width)*height;
    if (!pixels || pixels>100000000 || pixels>100000000/animation.frames.size()) throw std::runtime_error("GIF exceeds cumulative export budget");
    for (const auto& frame:animation.frames) {
        detail::cancelled(cancelled); validateFrame(frame.image,cancelled);
        if (frame.image.width!=width || frame.image.height!=height || frame.delayCentiseconds>65535)
            throw std::runtime_error("Invalid GIF frame dimensions or duration");
    }
    detail::TemporaryFile temporary(destination);
    {
        std::ofstream out(temporary.path,std::ios::binary|std::ios::trunc);
        if (!out) throw std::runtime_error("Cannot open GIF export");
        writeText(out,"GIF89a",6);writeWord(out,width);writeWord(out,height);writeByte(out,0x70);writeByte(out,0);writeByte(out,0);
        // NETSCAPE loop count 0 = loop forever.
        writeByte(out,0x21);writeByte(out,0xff);writeByte(out,11);writeText(out,"NETSCAPE2.0",11);writeByte(out,3);writeByte(out,1);writeWord(out,0);writeByte(out,0);
        for (const auto& frame:animation.frames) {
            detail::cancelled(cancelled);
            const auto palette=makePalette(frame.image,cancelled);
            // Disposal 2 clears the previous full frame. Index 0 is transparent when used.
            writeByte(out,0x21);writeByte(out,0xf9);writeByte(out,4);writeByte(out,palette.transparent?0x09:0x08);writeWord(out,frame.delayCentiseconds);writeByte(out,0);writeByte(out,0);
            writeByte(out,0x2c);writeWord(out,0);writeWord(out,0);writeWord(out,width);writeWord(out,height);writeByte(out,0x87);
            for(unsigned i=0;i<256;++i){writeByte(out,palette.colors[i][0]);writeByte(out,palette.colors[i][1]);writeByte(out,palette.colors[i][2]);}
            writeByte(out,8);
            SubBlocks blocks{out};
            // Literal-only LZW keeps encoding bounded. Periodic clear codes keep
            // the dictionary below 512 entries, so all codes stay nine bits wide.
            unsigned literals=0;
            blocks.code(256);
            for (unsigned y=0;y<height;++y) {
                detail::cancelled(cancelled);
                for (unsigned x=0;x<width;++x) {
                    const auto* p=frame.image.pixels.data()+std::size_t(y)*frame.image.stride+std::size_t(x)*4;
                    unsigned index=0;
                    if(p[3]>=128)index=nearestIndex(palette,straight(p,0),straight(p,1),straight(p,2));
                    else if(!palette.transparent)index=nearestIndex(palette,0,0,0);
                    if (literals == 200) { blocks.code(256); literals=0; }
                    blocks.code(index); ++literals;
                }
            }
            blocks.code(257);blocks.finish();
        }
        writeByte(out,0x3b);if (!out) throw std::runtime_error("Failed writing GIF export");
    }
    temporary.replace(destination,cancelled);
}
}

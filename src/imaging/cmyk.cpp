#include "cmyk.h"
#include <windows.h>
#include <icm.h>
#include <QCryptographicHash>
#include <QByteArrayView>
#include <QString>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <set>

namespace compositor::imaging {
namespace {
uint32_t be32(std::span<const uint8_t> bytes,size_t at){return uint32_t(bytes[at])<<24|uint32_t(bytes[at+1])<<16|uint32_t(bytes[at+2])<<8|bytes[at+3];}
struct Profile {
    HPROFILE value{};
    ~Profile(){if(value)CloseColorProfile(value);}
    Profile()=default;Profile(const Profile&)=delete;Profile& operator=(const Profile&)=delete;
};
void open(Profile& profile,std::span<const uint8_t> bytes){
    PROFILE input{PROFILE_MEMBUFFER,const_cast<uint8_t*>(bytes.data()),DWORD(bytes.size())};
    profile.value=OpenColorProfileW(&input,PROFILE_READ,FILE_SHARE_READ,OPEN_EXISTING);
    BOOL valid=FALSE;
    if(!profile.value||!IsColorProfileValid(profile.value,&valid)||!valid)throw std::runtime_error("Windows rejected the ICC profile");
}
struct Transform {
    HTRANSFORM value{};
    ~Transform(){if(value)DeleteColorTransform(value);}
};
}
CmykProfilePtr makeCmykProfile(std::span<const uint8_t> bytes,std::string name){
    if(bytes.size()<132||bytes.size()>maxCmykProfileBytes||be32(bytes,0)!=bytes.size()||
       std::memcmp(bytes.data()+36,"acsp",4)||std::memcmp(bytes.data()+16,"CMYK",4)||
       std::memcmp(bytes.data()+12,"prtr",4)||
       (std::memcmp(bytes.data()+20,"XYZ ",4)&&std::memcmp(bytes.data()+20,"Lab ",4)))
        throw std::runtime_error("Choose a valid CMYK output ICC profile (up to 16 MiB)");
    const auto count=be32(bytes,128);
    if(count>4096||132ULL+uint64_t(count)*12>bytes.size())throw std::runtime_error("Invalid CMYK ICC tag table");
    std::set<uint32_t> tags;
    for(uint32_t i=0;i<count;++i){const size_t at=132+size_t(i)*12;const auto offset=be32(bytes,at+4),length=be32(bytes,at+8);
        if(!tags.insert(be32(bytes,at)).second||offset%4||offset<132ULL+uint64_t(count)*12||length<8||uint64_t(offset)+length>bytes.size())throw std::runtime_error("Invalid CMYK ICC tag bounds");}
    Profile native;open(native,bytes);
    auto profile=std::make_shared<CmykProfile>();
    profile->name=name.empty()?"CMYK profile":name.substr(0,1024);
    profile->bytes.assign(bytes.begin(),bytes.end());
    profile->sha256=QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char*>(bytes.data()),qsizetype(bytes.size())),QCryptographicHash::Sha256).toHex().toStdString();
    // Verify both directions before accepting a profile for import, proof or output.
    CmykTransform check(profile);
    return profile;
}
CmykProfilePtr readCmykProfile(const std::filesystem::path& path){
    std::ifstream file(path,std::ios::binary|std::ios::ate);const auto size=file.tellg();
    if(!file||size<132||size>std::streamoff(maxCmykProfileBytes))throw std::runtime_error("CMYK profile is missing or exceeds 16 MiB");
    std::vector<uint8_t> bytes(static_cast<size_t>(size));file.seekg(0);
    if(!file.read(reinterpret_cast<char*>(bytes.data()),size))throw std::runtime_error("Cannot read CMYK profile");
    return makeCmykProfile(bytes,QString::fromStdWString(path.stem().wstring()).toUtf8().toStdString());
}
struct CmykTransform::Impl {
    CmykProfilePtr profile;
    Profile rgb,ink;Transform forward,reverse;
    explicit Impl(CmykProfilePtr p):profile(std::move(p)){
        if(!profile)throw MissingCmykProfile();
        open(ink,profile->bytes);
        wchar_t directory[MAX_PATH]{};if(!GetSystemDirectoryW(directory,MAX_PATH))throw std::runtime_error("Cannot locate sRGB profile");
        auto path=std::filesystem::path(directory)/L"spool/drivers/color/sRGB Color Space Profile.icm";
        PROFILE input{PROFILE_FILENAME,const_cast<wchar_t*>(path.c_str()),DWORD((path.native().size()+1)*sizeof(wchar_t))};
        rgb.value=OpenColorProfileW(&input,PROFILE_READ,FILE_SHARE_READ,OPEN_EXISTING);
        if(!rgb.value)throw std::runtime_error("Cannot open Windows sRGB profile");
        HPROFILE profiles[]{rgb.value,ink.value};DWORD intents[]{INTENT_RELATIVE_COLORIMETRIC};
        forward.value=CreateMultiProfileTransform(profiles,2,intents,1,0,INDEX_DONT_CARE);
        std::swap(profiles[0],profiles[1]);reverse.value=CreateMultiProfileTransform(profiles,2,intents,1,0,INDEX_DONT_CARE);
        if(!forward.value||!reverse.value)throw std::runtime_error("CMYK profile does not support RGB conversion in both directions");
    }
};
CmykTransform::CmykTransform(CmykProfilePtr p):impl_(std::make_unique<Impl>(std::move(p))){}
CmykTransform::~CmykTransform()=default;
void CmykTransform::toRgb(std::span<const uint16_t> input,std::span<uint16_t> output){
    if(input.size()%4||output.size()!=input.size()/4*3)throw std::invalid_argument("Invalid CMYK conversion buffers");
    std::array<COLOR,1024> in{},out{};
    for(size_t first=0;first<input.size()/4;first+=in.size()){
        const auto n=DWORD(std::min(in.size(),input.size()/4-first));
        for(DWORD i=0;i<n;++i){const auto* p=input.data()+(first+i)*4;in[i].cmyk={p[0],p[1],p[2],p[3]};}
        if(!TranslateColors(impl_->reverse.value,in.data(),n,COLOR_CMYK,out.data(),COLOR_RGB))throw std::runtime_error("CMYK to RGB conversion failed");
        for(DWORD i=0;i<n;++i){auto* p=output.data()+(first+i)*3;p[0]=out[i].rgb.red;p[1]=out[i].rgb.green;p[2]=out[i].rgb.blue;}
    }
}
void CmykTransform::toCmyk(std::span<const uint16_t> input,std::span<uint16_t> output){
    if(input.size()%3||output.size()!=input.size()/3*4)throw std::invalid_argument("Invalid RGB conversion buffers");
    std::array<COLOR,1024> in{},out{};
    for(size_t first=0;first<input.size()/3;first+=in.size()){
        const auto n=DWORD(std::min(in.size(),input.size()/3-first));
        for(DWORD i=0;i<n;++i){const auto* p=input.data()+(first+i)*3;in[i].rgb={p[0],p[1],p[2]};}
        if(!TranslateColors(impl_->forward.value,in.data(),n,COLOR_RGB,out.data(),COLOR_CMYK))throw std::runtime_error("RGB to CMYK conversion failed");
        for(DWORD i=0;i<n;++i){auto* p=output.data()+(first+i)*4;p[0]=out[i].cmyk.cyan;p[1]=out[i].cmyk.magenta;p[2]=out[i].cmyk.yellow;p[3]=out[i].cmyk.black;}
    }
}
void CmykTransform::encode(std::span<const uint8_t> rgba,std::span<uint8_t> ink,std::array<uint8_t,3> matte){
    if(rgba.size()%4||ink.size()!=rgba.size())throw std::invalid_argument("Invalid CMYK export buffers");
    std::array<uint16_t,3072> rgb{};std::array<uint16_t,4096> cmyk{};
    for(size_t first=0;first<rgba.size()/4;first+=1024){const auto n=std::min(size_t(1024),rgba.size()/4-first);
        for(size_t i=0;i<n;++i){const auto* p=rgba.data()+(first+i)*4;for(size_t c=0;c<3;++c){if(p[c]>p[3])throw std::runtime_error("Invalid premultiplied export pixels");rgb[i*3+c]=uint16_t((p[c]+(unsigned(matte[c])*(255-p[3])+127)/255)*257);}}
        toCmyk(std::span(rgb).first(n*3),std::span(cmyk).first(n*4));
        for(size_t i=0;i<n*4;++i)ink[first*4+i]=uint8_t((unsigned(cmyk[i])+128)/257);
    }
}
void CmykTransform::decode(std::span<const uint8_t> ink,std::span<uint8_t> rgba){
    if(ink.size()%4||rgba.size()!=ink.size())throw std::invalid_argument("Invalid CMYK preview buffers");
    std::array<uint16_t,3072> rgb{};std::array<uint16_t,4096> cmyk{};
    for(size_t first=0;first<ink.size()/4;first+=1024){const auto n=std::min(size_t(1024),ink.size()/4-first);
        for(size_t i=0;i<n*4;++i)cmyk[i]=uint16_t(ink[first*4+i]*257);
        toRgb(std::span(cmyk).first(n*4),std::span(rgb).first(n*3));
        for(size_t i=0;i<n;++i){for(size_t c=0;c<3;++c)rgba[(first+i)*4+c]=uint8_t((unsigned(rgb[i*3+c])+128)/257);rgba[(first+i)*4+3]=255;}
    }
}
std::vector<float> cmykProofLookup(CmykProfilePtr profile,const std::function<bool()>& cancelled){
    CmykTransform transform(std::move(profile));constexpr size_t n=33,count=n*n*n;
    std::vector<float> lookup(count*4,1.f);std::array<uint16_t,n*3> rgb{},back{};std::array<uint16_t,n*4> ink{};
    for(size_t b=0;b<n;++b)for(size_t g=0;g<n;++g){if(cancelled&&cancelled())throw std::runtime_error("CMYK proof preparation cancelled");
        for(size_t r=0;r<n;++r){rgb[r*3]=uint16_t((r*65535+16)/32);rgb[r*3+1]=uint16_t((g*65535+16)/32);rgb[r*3+2]=uint16_t((b*65535+16)/32);}
        transform.toCmyk(rgb,ink);transform.toRgb(ink,back);
        for(size_t r=0;r<n;++r)for(size_t c=0;c<3;++c)lookup[((b*n+g)*n+r)*4+c]=float(back[r*3+c])/65535.f;
    }return lookup;
}
}

namespace compositor::imaging {
CmykProfilePtr photoshopCmykProfile(const std::filesystem::path& path,CmykProfilePtr fallback){
    std::ifstream file(path,std::ios::binary|std::ios::ate);const auto end=file.tellg();file.seekg(0);
    auto read=[&](void* data,size_t size){if(!file.read(static_cast<char*>(data),std::streamsize(size)))throw std::runtime_error("PSD ended early");};
    auto skip=[&](uint64_t n){const auto at=file.tellg();if(at<0||end<at||n>uint64_t(end-at))throw std::runtime_error("PSD resource exceeds file bounds");file.seekg(std::streamoff(n),std::ios::cur);};
    auto u32=[&](){std::array<uint8_t,4> b{};read(b.data(),b.size());return be32(b,0);};
    std::array<uint8_t,26> header{};read(header.data(),header.size());
    if(std::memcmp(header.data(),"8BPS",4)||header[4]||header[5]<1||header[5]>2)throw std::runtime_error("Invalid Photoshop header");
    if(header[24]!=0||header[25]!=4)return {};
    skip(u32());const auto length=u32();const auto start=file.tellg();if(start<0||end<start||uint64_t(length)>uint64_t(end-start))throw std::runtime_error("Invalid PSD resource section");
    const auto resourceEnd=start+std::streamoff(length);CmykProfilePtr profile;
    while(file.tellg()<resourceEnd){if(resourceEnd-file.tellg()<12)throw std::runtime_error("Truncated PSD resource");std::array<uint8_t,7> record{};read(record.data(),record.size());if(std::memcmp(record.data(),"8BIM",4))throw std::runtime_error("Invalid PSD resource signature");
        const auto id=uint16_t(unsigned(record[4])*256+record[5]);skip(record[6]);if((record[6]+1)%2)skip(1);const auto size=u32();
        const auto at=file.tellg();if(at>resourceEnd||uint64_t(size)+(size%2)>uint64_t(resourceEnd-at))throw std::runtime_error("Invalid PSD resource bounds");
        if(id==1039){if(profile||size<132||size>maxCmykProfileBytes)throw std::runtime_error("Invalid PSD ICC profile");std::vector<uint8_t> bytes(size);read(bytes.data(),size);profile=makeCmykProfile(bytes,"Embedded CMYK profile");}else skip(size);
        if(size%2)skip(1);
    }
    if(profile)return profile;if(fallback)return fallback;throw MissingCmykProfile();
}
}

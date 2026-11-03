#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace cmyk_test {
using Bytes=std::vector<std::uint8_t>;
inline void integer(Bytes& out,std::uint64_t value,int bytes){for(int shift=(bytes-1)*8;shift>=0;shift-=8)out.push_back(std::uint8_t(value>>shift));}
inline void append(Bytes& out,const Bytes& bytes){out.insert(out.end(),bytes.begin(),bytes.end());}
inline void ascii(Bytes& out,std::string_view text){out.insert(out.end(),text.begin(),text.end());}
// Four pixels: paper, solid black, cyan with half alpha, neutral with quarter alpha.
// Photoshop stores ink complements and its 16-bit samples on the 0..32768 scale.
inline Bytes psd(std::span<const std::uint8_t> profile={},bool psb=false,int depth=8,bool rle=false,bool layered=true,bool missingBlack=false){
    const std::array<std::array<unsigned,4>,6> planes{{{255,255,0,128},{255,255,255,128},{255,255,255,128},{255,0,255,128},{255,255,128,64},{255,128,255,0}}};
    std::array<Bytes,6> raw,encoded;
    for(size_t c=0;c<planes.size();++c){for(auto sample:planes[c])integer(raw[c],depth==16?(sample*32768+127)/255:sample,depth/8);
        integer(encoded[c],rle?1:0,2);if(rle){integer(encoded[c],raw[c].size()+1,psb?4:2);encoded[c].push_back(std::uint8_t(raw[c].size()-1));}append(encoded[c],raw[c]);}
    Bytes out;ascii(out,"8BPS");integer(out,psb?2:1,2);integer(out,0,6);integer(out,4,2);integer(out,1,4);integer(out,4,4);integer(out,depth,2);integer(out,4,2);integer(out,0,4);
    Bytes resources;if(!profile.empty()){ascii(resources,"8BIM");integer(resources,1039,2);integer(resources,0,2);integer(resources,profile.size(),4);resources.insert(resources.end(),profile.begin(),profile.end());if(profile.size()%2)resources.push_back(0);}
    integer(out,resources.size(),4);append(out,resources);
    Bytes section;
    if(layered){Bytes info;integer(info,1,2);integer(info,0,4);integer(info,0,4);integer(info,1,4);integer(info,4,4);integer(info,missingBlack?5:6,2);
        for(size_t c=0;c<planes.size();++c){if(c==3&&missingBlack)continue;integer(info,c<4?c:c==4?65535:65534,2);integer(info,encoded[c].size(),psb?8:4);}
        ascii(info,"8BIMnorm");integer(info,255,1);integer(info,0,3);
        Bytes extra;integer(extra,20,4);integer(extra,0,4);integer(extra,0,4);integer(extra,1,4);integer(extra,4,4);integer(extra,255,1);integer(extra,0,3);integer(extra,0,4);integer(extra,3,1);ascii(extra,"Ink");
        integer(info,extra.size(),4);append(info,extra);for(size_t c=0;c<planes.size();++c)if(c!=3||!missingBlack)append(info,encoded[c]);if(info.size()%2)info.push_back(0);
        integer(section,info.size(),psb?8:4);append(section,info);integer(section,0,4);
    }
    integer(out,section.size(),psb?8:4);append(out,section);integer(out,rle?1:0,2);
    if(rle)for(int c=0;c<4;++c)integer(out,raw[c].size()+1,psb?4:2);
    for(int c=0;c<4;++c){if(rle)out.push_back(std::uint8_t(raw[c].size()-1));append(out,raw[c]);}
    return out;
}
}

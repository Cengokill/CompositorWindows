#include "imaging/gif_codec.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace compositor::imaging;
namespace {
void require(bool b, const char* message) { if (!b) throw std::runtime_error(message); }
RgbaImage frame(std::uint8_t r,std::uint8_t g,std::uint8_t b, unsigned left) {
    RgbaImage image{4,3,16,std::vector<std::uint8_t>(48)};
    for (unsigned y=0;y<3;++y) for(unsigned x=left;x<left+2;++x) {
        auto* p=image.pixels.data()+y*image.stride+x*4;p[0]=r;p[1]=g;p[2]=b;p[3]=255;
    }
    return image;
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {
        QTemporaryDir temp; require(temp.isValid(),"Cannot create temporary directory");
        const std::filesystem::path path=std::filesystem::path(temp.path().toStdWString())/L"animation.gif";
        GifAnimation original{{{frame(255,0,0,0),3},{frame(0,255,0,2),12},{frame(0,0,255,0),21}}};
        encodeGif(path,original);
        auto decoded=decodeGif(path);
        require(decoded.frames.size()==original.frames.size(),"Frame count changed");
        for (size_t i=0;i<original.frames.size();++i) {
            require(decoded.frames[i].delayCentiseconds==original.frames[i].delayCentiseconds,"Frame delay changed");
            require(decoded.frames[i].image.pixels==original.frames[i].image.pixels,"Frame pixels or transparency changed");
        }
        std::ifstream beforeFile(path,std::ios::binary); const std::string before((std::istreambuf_iterator<char>(beforeFile)),{});beforeFile.close();
        bool stopped=false; try { encodeGif(path,original,[]{return true;}); } catch (const std::exception&) {stopped=true;}
        require(stopped,"Cancelled encoding succeeded");
        std::ifstream afterFile(path,std::ios::binary); const std::string after((std::istreambuf_iterator<char>(afterFile)),{});
        require(before==after,"Cancelled encoding replaced destination");
        ImportOptions options; options.remainingPixels=20;
        bool rejected=false;try { decodeGif(path,options); } catch (const std::exception&) { rejected=true; }
        require(rejected,"Cumulative frame pixel budget not enforced");
        options={}; options.cancelled=[] {return true;}; rejected=false;
        try { decodeGif(path,options); } catch (const std::exception&) { rejected=true; }
        require(rejected,"Cancelled decoding succeeded");
        auto bad=std::filesystem::path(temp.path().toStdWString())/L"bad.gif";
        std::ofstream invalid(bad,std::ios::binary);invalid<<"GIF89a";invalid.close();rejected=false;
        try { decodeGif(bad); } catch (const std::exception&) { rejected=true; }
        require(rejected,"Truncated GIF accepted");
        // Generated with Pillow from three 3x2 RGBA frames; the second frame
        // occupies only one pixel and uses restore-previous disposal.
        const unsigned char fixture[]{71,73,70,56,57,97,3,0,2,0,129,0,0,255,0,0,0,0,0,0,0,0,0,0,0,33,249,4,4,5,0,0,0,44,0,0,0,0,3,0,2,0,0,8,6,0,1,8,28,24,16,0,33,249,4,13,8,0,2,0,44,1,0,0,0,1,0,1,0,129,255,0,0,0,255,0,0,0,0,0,0,0,8,4,0,3,4,4,0,33,249,4,5,12,0,2,0,44,0,0,0,0,2,0,2,0,129,255,0,0,0,0,255,0,0,0,0,0,0,8,7,0,5,0,8,32,32,32,0,59};
        auto partial=std::filesystem::path(temp.path().toStdWString())/L"partial.gif";
        std::ofstream partialFile(partial,std::ios::binary);partialFile.write(reinterpret_cast<const char*>(fixture),sizeof fixture);partialFile.close();
        auto composed=decodeGif(partial);
        require(composed.frames.size()==3,"Partial GIF frames lost");
        auto pixel=[&](size_t index,unsigned x,unsigned y){const auto& image=composed.frames[index].image;return image.pixels.data()+y*image.stride+x*4;};
        auto green=pixel(1,1,0); require(green[0]==0&&green[1]==255&&green[2]==0&&green[3]==255,"Partial GIF frame offset wrong");
        auto restored=pixel(2,1,0); require(restored[0]==255&&restored[1]==0&&restored[2]==0&&restored[3]==255,"GIF restore-previous disposal wrong");
        auto blue=pixel(2,0,1); require(blue[0]==0&&blue[1]==0&&blue[2]==255&&blue[3]==255,"GIF frame composition wrong");
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}

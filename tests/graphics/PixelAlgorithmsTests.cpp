#include "graphics/PixelAlgorithms.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>

using namespace compositor::graphics;
namespace {
int passed=0,failed=0;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> void test(const char* id,F f){try{f();++passed;std::cout<<"PASS "<<id<<'\n';}catch(const std::exception& e){++failed;std::cout<<"FAIL "<<id<<": "<<e.what()<<'\n';}}
template<class F> void rejects(F f){bool threw=false;try{f();}catch(const std::invalid_argument&){threw=true;}require(threw,"invalid input accepted");}
struct Image {
    uint32_t w,h;size_t stride;std::vector<uint8_t> bytes;
    Image(uint32_t width,uint32_t height,size_t padding=0):w(width),h(height),stride(size_t(w)*4+padding),bytes(stride*h,0){}
    Rgba8View view(){return{bytes,w,h,stride};}
    ConstRgba8View read()const{return{bytes,w,h,stride};}
    uint8_t* pixel(uint32_t x,uint32_t y){return bytes.data()+y*stride+x*4;}
    void fill(uint8_t r,uint8_t g,uint8_t b,uint8_t a){for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x){auto p=pixel(x,y);p[0]=r;p[1]=g;p[2]=b;p[3]=a;}}
};
int winding(const WandOutline& outline,double x,double y) {
    size_t offset=0;int result=0;
    for(auto count:outline.loopLengths){for(int j=0;j<count;++j){size_t a=offset+size_t(j)*2,b=offset+size_t((j+1)%count)*2;
        double ax=outline.points[a],ay=outline.points[a+1],bx=outline.points[b],by=outline.points[b+1];
        double cross=(bx-ax)*(y-ay)-(x-ax)*(by-ay);
        if(ay<=y&&by>y&&cross>0)++result;if(ay>y&&by<=y&&cross<0)--result;}offset+=size_t(count)*2;}
    return result;
}
}
int main(){
    test("G-C-001-alpha-stride-bounds",[]{Image a(5,3,7);a.pixel(2,1)[3]=128;a.pixel(4,2)[3]=255;
        require(alphaBounds(a.read())==std::array<size_t,4>{2,1,5,3},"wrong half-open bounds");
        std::vector<uint8_t> gray(8*3,99);Gray8View m{gray,5,3,8};extractAlpha(a.read(),m);
        require(gray[8+2]==128&&gray[16+4]==255&&gray[5]==99,"alpha/stride mismatch");
        require(coverageBounds(readOnly(m))==std::array<int64_t,4>{2,1,5,3},"wrong gray bounds");});
    test("G-C-002-alpha-roundtrip",[]{Image a(256,1);for(unsigned i=0;i<256;++i){auto p=a.pixel(i,0);p[0]=uint8_t(i/3);p[1]=uint8_t(i/2);p[2]=uint8_t(i);p[3]=uint8_t(i);}
        auto before=a.bytes;std::vector<uint8_t> alpha(256);Gray8View m{alpha,256,1,256};extractAlpha(a.read(),m);unpremultiplyOpaque(a.view());
        require(a.bytes[3]==255&&a.bytes[0]==0,"transparent pixel opacity conversion");restoreAlpha(a.view(),readOnly(m));require(a.bytes==before,"premultiplication round trip");});
    test("G-C-003-clamp-ringing",[]{Image a(2,1,4);a.bytes={255,100,40,50,4,5,6,0,91,92,93,94};clampPremultiplied(a.view());require(a.bytes==std::vector<uint8_t>{50,50,40,50,0,0,0,0,91,92,93,94},"clamp alpha/padding");});
    test("G-C-004-levels-identity",[]{Image a(256,2,11);for(unsigned y=0;y<2;++y)for(unsigned x=0;x<256;++x){auto p=a.pixel(x,y);p[0]=uint8_t(x/2);p[1]=uint8_t(x);p[2]=0;p[3]=uint8_t(x);}
        auto before=a.bytes;std::array<float,768> table{};for(int c=0;c<3;++c)for(int i=0;i<256;++i)table[c*256+i]=float(i)/255;
        applyLevels(a.view(),table);require(a.bytes==before,"identity levels changed bytes");});
    test("G-C-005-histogram-alpha-coverage",[]{Image a(3,1);a.bytes={128,0,0,128,0,255,0,255,0,0,0,0};std::vector<uint8_t> m{255,128,255};ConstGray8View mask{m,3,1,3};auto bins=histogram(a.read(),&mask);
        double expected=256.0/255.0;for(int c=0;c<4;++c)require(std::abs(std::accumulate(bins.begin()+c*256,bins.begin()+(c+1)*256,0.0)-expected)<1e-12,"histogram weighting");});
    test("G-C-006-gradient-map-alpha",[]{Image a(2,1);a.bytes={20,30,40,64,0,0,0,0};std::array<uint8_t,768> table{};for(int i=0;i<256;++i){table[i*3]=255;table[i*3+2]=128;}
        gradientMap(a.view(),table);require(a.bytes==std::vector<uint8_t>{64,0,32,64,0,0,0,0},"gradient map premultiplication");});
    test("G-C-007-grain-global-coordinates",[]{Image full(37,13),part(17,13);full.fill(60,70,80,128);part.fill(60,70,80,128);
        grain(full.view(),70,2.5,55,123,-5,-8,1.25);grain(part.view(),70,2.5,55,123,-5+20*1.25,-8,1.25);
        for(unsigned y=0;y<13;++y)for(unsigned x=0;x<17;++x)for(int c=0;c<4;++c)require(full.pixel(x+20,y)[c]==part.pixel(x,y)[c],"grain changed at tile boundary");});
    test("G-C-008-noise-seed-alpha-and-monochrome",[]{for(bool gaussian:{false,true}){Image a(31,17,5);a.fill(64,64,64,128);a.pixel(2,2)[3]=0;a.pixel(2,2)[0]=0;a.pixel(2,2)[1]=0;a.pixel(2,2)[2]=0;auto b=a;
        addNoise(a.view(),50,gaussian,true,77);addNoise(b.view(),50,gaussian,true,77);require(a.bytes==b.bytes,"noise nondeterministic");
        for(unsigned y=0;y<a.h;++y)for(unsigned x=0;x<a.w;++x){auto p=a.pixel(x,y);require(p[0]==p[1]&&p[1]==p[2]&&p[0]<=p[3],"noise channels or premultiplication");require(p[3]==((x==2&&y==2)?0:128),"noise alpha changed");}}});
    test("G-C-009-lens-identity-and-narrow-30k",[]{Image a(30000,1,16),b(30000,1,16);for(unsigned x=0;x<a.w;++x){auto p=a.pixel(x,0);p[0]=uint8_t(x%256);p[3]=255;}
        lensDistort(a.read(),b.view(),0);require(a.bytes==b.bytes,"30k exact identity");rejects([&]{lensDistort(a.read(),a.view(),0);});});
    test("G-C-010-lens-sign-transparent-corners",[]{Image a(11,11),b(11,11);a.fill(128,64,32,255);lensDistort(a.read(),b.view(),-1);require(b.pixel(0,0)[3]==0&&b.pixel(5,5)[3]==255,"pincushion edge behavior");lensDistort(a.read(),b.view(),1);require(b.pixel(0,0)[3]==255,"barrel corner crop");});
    test("G-C-011-wand-contiguous",[]{Image a(10,4);for(unsigned y=0;y<4;++y)for(unsigned x=0;x<10;++x){auto p=a.pixel(x,y);p[0]=(x<3||x>=6)?255:0;p[2]=p[0]?0:255;p[3]=255;}
        std::vector<uint8_t> m(13*4,99);Gray8View mask{m,10,4,13};require(wandMask(a.read(),1,2,0,32,true,mask)==12,"contiguous count");require(wandMask(a.read(),1,2,0,32,false,mask)==28,"global count");require(m[10]==99,"wand padding changed");});
    test("G-C-012-wand-alpha-tolerance-and-average",[]{Image a(4,1);a.bytes={100,100,100,255,132,100,100,255,133,100,100,255,100,100,100,222};std::vector<uint8_t> m(4);Gray8View mask{m,4,1,4};
        require(wandMask(a.read(),0,0,0,0,false,mask)==1,"zero tolerance");require(wandMask(a.read(),0,0,0,32,false,mask)==2,"inclusive tolerance");require(wandMask(a.read(),0,0,0,33,false,mask)==4,"alpha tolerance");
        Image dot(5,5);dot.fill(0,0,0,255);for(int c=0;c<3;++c)dot.pixel(2,2)[c]=255;std::vector<uint8_t> dm(25);require(wandMask(dot.read(),2,2,1,30,false,{dm,5,5,5})==24,"3x3 sample average");});
    test("G-C-013-wand-outline-holes-and-corners",[]{std::vector<uint8_t> m(8*6);for(unsigned y=0;y<3;++y)for(unsigned x=0;x<3;++x)if(x!=1||y!=1)m[y*8+x]=255;m[4*8+5]=255;m[5*8+6]=255;
        auto outline=wandOutline({m,8,6,8});require(outline.loopLengths.size()==4,"ring and diagonal loop count");for(unsigned y=0;y<6;++y)for(unsigned x=0;x<8;++x)require((winding(outline,x+0.5,y+0.5)!=0)==(m[y*8+x]!=0),"outline does not reproduce mask");});
    test("G-C-014-content-fill-donor-and-no-donor",[]{Image a(13,13);a.fill(60,70,80,255);auto original=a.bytes;std::vector<uint8_t> m(169);m[84]=255;a.pixel(6,6)[0]=200;
        require(contentFill(a.view(),{m,13,13,13}),"constant donor failed");require(a.bytes==original,"constant source did not restore pixel");std::fill(m.begin(),m.end(),uint8_t{255});require(!contentFill(a.view(),{m,13,13,13}),"missing donor accepted");require(a.bytes==original,"failed fill modified original");});
    for(int mode=0;mode<3;++mode)test((std::string("G-C-015-heal-mode-")+std::to_string(mode)).c_str(),[=]{Image a(120,80);std::vector<uint8_t> mask(120*80);
        for(unsigned y=0;y<80;++y)for(unsigned x=0;x<120;++x){auto p=a.pixel(x,y);bool red=x>=55&&x<65&&y>=35&&y<45;auto gray=uint8_t(x%4<2?100:112);p[0]=red?230:gray;p[1]=p[2]=red?20:gray;p[3]=255;
            if((int(x)-60)*(int(x)-60)+(int(y)-40)*(int(y)-40)<=144)mask[y*120+x]=255;}
        auto before=a.bytes;spotHeal(a.view(),{mask,120,80,120},1,mode,17);for(auto xy:{std::array<unsigned,2>{60,40},{56,36},{64,44}}){auto p=a.pixel(xy[0],xy[1]);require(int(p[0])-int(p[1])<30&&p[1]>=80&&p[1]<=130&&p[3]==255,"blemish not healed into gray");}
        for(size_t i=0;i<mask.size();++i)if(!mask[i])for(size_t c=0;c<4;++c)require(a.bytes[i*4+c]==before[i*4+c],"healing changed unaffected pixel");});
    test("G-C-016-reject-invalid-extents-and-parameters",[]{std::vector<uint8_t> bytes(16);rejects([&]{validate(Rgba8View{bytes,2,2,7});});rejects([&]{validate(Rgba8View{bytes,2,2,std::numeric_limits<size_t>::max()});});rejects([&]{validate(Rgba8View{bytes,30001,1,120004});});
        Image a(1,1);rejects([&]{addNoise(a.view(),std::numeric_limits<float>::quiet_NaN(),false,false,0);});rejects([&]{grain(a.view(),50,0,50,0,0,0,1);});
        rejects([&]{extractAlpha(a.read(),{a.bytes,1,1,1});});rejects([&]{restoreAlpha(a.view(),{a.bytes,1,1,1});});});
    test("G-C-017-wand-LLP64-accumulator-regression",[]{
        static_assert(sizeof(long)==4 && sizeof(size_t)==8,"This regression targets Windows x64 LLP64");
        // 4105 squared opaque white samples sum to > UINT32_MAX per channel.
        // The original unsigned long silently wrapped under Windows LLP64.
        Image a(4105,4105);a.fill(255,255,255,255);std::vector<uint8_t> m(size_t(a.w)*a.h);
        require(wandMask(a.read(),2052,2052,10000,0,false,{m,a.w,a.h,a.w})==16851025,"LLP64 sample sum overflow");
        require(std::all_of(m.begin(),m.end(),[](uint8_t b){return b==255;}),"large sample average selected wrong pixels");});
    test("G-C-018-content-fill-repeating-texture",[]{Image a(80,64);std::vector<uint8_t> m(80*64);
        for(unsigned y=0;y<a.h;++y)for(unsigned x=0;x<a.w;++x){auto p=a.pixel(x,y);bool target=x>=32&&x<44&&y>=26&&y<36;uint8_t value=(x/4)%2==0?51:204;
            p[0]=target?255:value;p[1]=p[2]=target?0:value;p[3]=255;m[y*80+x]=target?255:0;}
        auto before=a.bytes;require(contentFill(a.view(),{m,80,64,80}),"repeating texture fill failed");int matching=0;
        for(unsigned y=26;y<36;++y)for(unsigned x=32;x<44;++x)if(std::abs(int(a.pixel(x,y)[0])-((x/4)%2==0?51:204))<=1)++matching;
        require(matching>=114,"upstream repeating texture criterion failed");for(size_t i=0;i<m.size();++i)if(!m[i])for(size_t c=0;c<4;++c)require(a.bytes[i*4+c]==before[i*4+c],"fill changed pixels outside coverage");});
    std::cout<<"{\"suite\":\"native_C_algorithms\",\"passed\":"<<passed<<",\"failed\":"<<failed<<",\"mac_differential\":false}\n";
    return failed?1:0;
}


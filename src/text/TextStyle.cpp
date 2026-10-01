#include "TextStyle.h"
#include <QString>
#include <algorithm>
#include <cmath>

namespace compositor::text {
namespace {
QString unitsOf(std::string_view utf8){return QString::fromUtf8(utf8.data(),int(utf8.size()));}
struct Color { double red,green,blue; bool operator==(const Color&) const = default; };
struct Face { std::string family; double size; bool operator==(const Face&) const = default; };
std::pair<int,int> rangeOf(int location,int length,int count){
    if(count<0)count=0;
    if(location<0)location=0;
    if(location>count)location=count;
    if(length<0)length=0;
    const int end=length>count-location?count:location+length;
    return {location,end};
}
Color baseColor(const TextContent& text){return {text.red,text.green,text.blue};}
Face baseFace(const TextContent& text){return {text.fontFamily,text.fontSize};}
std::vector<Color> colorsOf(const TextContent& text,int count){
    std::vector<Color> colors(size_t(count),baseColor(text));
    for(const auto& run:text.colorRuns){
        if(!run.hasColor||run.location<0||run.length<=0)continue;
        const Color color{run.red,run.green,run.blue};
        for(int index=run.location;index<std::min(count,run.location+run.length);++index)colors[size_t(index)]=color;
    }
    return colors;
}
std::vector<Face> facesOf(const TextContent& text,int count){
    std::vector<Face> faces(size_t(count),baseFace(text));
    for(const auto& run:text.fontRuns){
        if(!run.hasFont||run.location<0||run.length<=0||run.fontFamily.empty())continue;
        const Face face{run.fontFamily,run.fontSize};
        for(int index=run.location;index<std::min(count,run.location+run.length);++index)faces[size_t(index)]=face;
    }
    return faces;
}
void writeColors(TextContent& text,const std::vector<Color>& colors){
    const Color base=baseColor(text);
    std::vector<TextRun> runs;
    for(int index=0;index<int(colors.size());++index){
        const Color& color=colors[size_t(index)];
        if(color==base)continue;
        if(!runs.empty()&&runs.back().location+runs.back().length==index&&runs.back().red==color.red&&runs.back().green==color.green&&runs.back().blue==color.blue)++runs.back().length;
        else{TextRun run;run.location=index;run.length=1;run.hasColor=true;run.red=color.red;run.green=color.green;run.blue=color.blue;runs.push_back(run);}
    }
    text.colorRuns=std::move(runs);
}
void writeFaces(TextContent& text,const std::vector<Face>& faces){
    if(!faces.empty()&&std::all_of(faces.begin(),faces.end(),[&](const Face& face){return face==faces.front();})){
        text.fontFamily=faces.front().family;text.fontSize=faces.front().size;text.fontRuns.clear();return;
    }
    const Face base=baseFace(text);
    std::vector<TextRun> runs;
    for(int index=0;index<int(faces.size());++index){
        const Face& face=faces[size_t(index)];
        if(face==base)continue;
        if(!runs.empty()&&runs.back().location+runs.back().length==index&&runs.back().fontFamily==face.family&&runs.back().fontSize==face.size)++runs.back().length;
        else{TextRun run;run.location=index;run.length=1;run.hasFont=true;run.fontFamily=face.family;run.fontSize=face.size;runs.push_back(run);}
    }
    text.fontRuns=std::move(runs);
}
bool colorInRange(double value){return std::isfinite(value)&&value>=0&&value<=1;}
bool faceAllowed(const std::string& family){return !family.empty()&&family.size()<=256&&family.find('\n')==std::string::npos&&family.find('\r')==std::string::npos;}
}
int utf16Length(std::string_view utf8){return unitsOf(utf8).size();}
bool textRunsValid(const TextContent& text){
    if(text.value.size()>100000||!faceAllowed(text.fontFamily)||!(text.fontSize>=1)||text.fontSize>1000)return false;
    if(!colorInRange(text.red)||!colorInRange(text.green)||!colorInRange(text.blue)||!colorInRange(text.alpha))return false;
    const int count=utf16Length(text.value);
    auto fits=[&](const std::vector<TextRun>& runs,bool font){
        if(runs.size()>256)return false;
        int end=0;
        for(const auto& run:runs){
            if(run.location<end||run.length<=0||run.location>count||run.length>count-run.location)return false;
            end=run.location+run.length;
            if(font){if(!run.hasFont||!faceAllowed(run.fontFamily)||!(run.fontSize>=1)||run.fontSize>1000)return false;}
            else if(!run.hasColor||!colorInRange(run.red)||!colorInRange(run.green)||!colorInRange(run.blue))return false;
        }
        return true;
    };
    return fits(text.colorRuns,false)&&fits(text.fontRuns,true);
}
void setTextColor(TextContent& text,double red,double green,double blue,int location,int length){
    if(!colorInRange(red)||!colorInRange(green)||!colorInRange(blue))return;
    const int count=utf16Length(text.value);
    const auto [start,end]=rangeOf(location,length,count);
    if(start==end||(start==0&&end==count)){text.red=red;text.green=green;text.blue=blue;text.colorRuns.clear();return;}
    auto colors=colorsOf(text,count);
    for(int index=start;index<end;++index)colors[size_t(index)]={red,green,blue};
    writeColors(text,colors);
}
void setTextFont(TextContent& text,std::string family,int location,int length){
    if(!faceAllowed(family))return;
    const int count=utf16Length(text.value);
    const auto [start,end]=rangeOf(location,length,count);
    if(start==end||(start==0&&end==count)){text.fontFamily=std::move(family);text.fontRuns.clear();return;}
    auto faces=facesOf(text,count);
    for(int index=start;index<end;++index)faces[size_t(index)]={family,text.fontSize};
    writeFaces(text,faces);
}
void setTextSize(TextContent& text,double size){
    if(!(size>=1)||size>1000||!std::isfinite(size))return;
    text.fontSize=size;
    for(auto& run:text.fontRuns)run.fontSize=size;
}
bool replaceText(TextContent& text,int location,int length,std::string_view insertion){
    auto content=unitsOf(text.value);
    const int count=content.size();
    const auto [start,end]=rangeOf(location,length,count);
    const QString inserted=QString::fromUtf8(insertion.data(),int(insertion.size()));
    if(content.size()- (end-start)+inserted.size()>100000)return false;
    QString next=content;next.replace(start,end-start,inserted);
    const auto utf8=next.toUtf8();
    if(utf8.size()>100000)return false;
    if(!text.colorRuns.empty()){
        auto colors=colorsOf(text,count);
        const Color inherited=start>0?colors[size_t(start-1)]:(end>start?colors[size_t(start)]:baseColor(text));
        colors.erase(colors.begin()+start,colors.begin()+end);
        colors.insert(colors.begin()+start,size_t(inserted.size()),inherited);
        writeColors(text,colors);
    }
    if(!text.fontRuns.empty()){
        auto faces=facesOf(text,count);
        const Face inherited=start>0?faces[size_t(start-1)]:(end>start?faces[size_t(start)]:baseFace(text));
        faces.erase(faces.begin()+start,faces.begin()+end);
        faces.insert(faces.begin()+start,size_t(inserted.size()),inherited);
        writeFaces(text,faces);
    }
    text.value=utf8.toStdString();
    return true;
}
std::string fontAt(const TextContent& text,int index){
    const int count=utf16Length(text.value);
    if(count<=0)return text.fontFamily;
    index=std::clamp(index,0,count-1);
    for(const auto& run:text.fontRuns)if(run.hasFont&&run.location<=index&&index<run.location+run.length)return run.fontFamily;
    return text.fontFamily;
}
std::string uniformFont(const TextContent& text,int location,int length){
    const int count=utf16Length(text.value);
    const auto [start,end]=rangeOf(location,length,count);
    if(end<=start)return {};
    const std::string face=fontAt(text,start);
    for(int index=start;index<end;++index)if(fontAt(text,index)!=face)return {};
    return face;
}
}

#include "text/TextStyle.h"
#include "text/TextRaster.h"
#include <algorithm>
#include <iostream>
#include <map>
#include <objbase.h>
#include <stdexcept>
#include <string>

using namespace compositor;
using namespace compositor::text;
#define REQUIRE(...) do { if(!(__VA_ARGS__)) throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #__VA_ARGS__); } while(false)

TextContent sample(std::string value="Hello"){TextContent text;text.value=std::move(value);text.fontFamily="Segoe UI";text.fontSize=48;text.red=0;text.green=0;text.blue=0;text.alpha=1;return text;}

void partial_color(){
    auto text=sample();
    setTextColor(text,1,0,0,1,3);
    REQUIRE(textRunsValid(text));
    REQUIRE(text.red==0&&text.colorRuns.size()==1);
    REQUIRE(text.colorRuns[0].location==1&&text.colorRuns[0].length==3&&text.colorRuns[0].red==1);
    REQUIRE(text.colorRuns[0].hasColor);
}
void whole_color(){
    auto text=sample();
    setTextColor(text,0,0,1,1,3);
    setTextColor(text,1,0,0,0,0);
    REQUIRE(text.colorRuns.empty()&&text.red==1&&text.green==0&&text.blue==0);
    REQUIRE(textRunsValid(text));
}
void inherit_insert(){
    auto text=sample("Hello");
    setTextColor(text,0,1,0,1,3);
    REQUIRE(replaceText(text,2,0,"Z"));
    REQUIRE(text.value=="HeZllo");
    REQUIRE(text.colorRuns.size()==1&&text.colorRuns[0].location==1&&text.colorRuns[0].length==4);
    REQUIRE(text.colorRuns[0].green==1);
}
void reject_overflow(){
    auto text=sample("Hi");
    TextRun run;run.location=0;run.length=5;run.hasColor=true;run.red=1;
    text.colorRuns={run};
    REQUIRE(!textRunsValid(text));
    text=sample("Hi");
    TextRun first;first.location=0;first.length=2;first.hasColor=true;first.red=1;
    TextRun second;second.location=1;second.length=1;second.hasColor=true;second.blue=1;
    text.colorRuns={first,second};
    REQUIRE(!textRunsValid(text));
}
void utf16_pair(){
    const std::string emoji="A\xF0\x9F\x98\x80""B";
    REQUIRE(utf16Length(emoji)==4);
    auto text=sample(emoji);
    setTextColor(text,1,0,0,1,2);
    REQUIRE(textRunsValid(text)&&text.colorRuns.size()==1&&text.colorRuns[0].location==1&&text.colorRuns[0].length==2);
    REQUIRE(replaceText(text,1,0,"Z"));
    REQUIRE(utf16Length(text.value)==5);
    REQUIRE(text.colorRuns.size()==1&&text.colorRuns[0].location==2&&text.colorRuns[0].length==2);
}
void font_range(){
    auto text=sample();
    setTextFont(text,"Courier New",1,3);
    REQUIRE(textRunsValid(text)&&text.fontFamily=="Segoe UI"&&text.fontRuns.size()==1);
    REQUIRE(text.fontRuns[0].location==1&&text.fontRuns[0].length==3&&text.fontRuns[0].fontFamily=="Courier New");
    REQUIRE(text.fontRuns[0].fontSize==48&&text.fontRuns[0].hasFont);
    REQUIRE(uniformFont(text,1,3)=="Courier New"&&uniformFont(text,0,5).empty());
    setTextFont(text,"Courier New",0,0);
    REQUIRE(text.fontFamily=="Courier New"&&text.fontRuns.empty());
    setTextSize(text,72);
    REQUIRE(text.fontSize==72&&textRunsValid(text));
}
void paragraph_layout(){
    const HRESULT started=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    REQUIRE(SUCCEEDED(started)||started==RPC_E_CHANGED_MODE);
    struct Cleanup{HRESULT hr;~Cleanup(){if(SUCCEEDED(hr))CoUninitialize();}} cleanup{started};
    auto boxed=sample("Hello Hello Hello");
    boxed.boxWidth=80;boxed.boxHeight=160;
    REQUIRE(textRunsValid(boxed)&&hasTextBox(boxed));
    auto wrapped=layoutText(boxed);
    REQUIRE(wrapped.width==80&&wrapped.carets.size()>4);
    int lines=0;for(const auto& caret:wrapped.carets)lines=std::max(lines,caret.line+1);
    REQUIRE(lines>1);
    auto left=sample("Hi");left.boxWidth=200;left.boxHeight=80;
    auto center=left;center.alignment=TextAlignment::Center;
    REQUIRE(layoutText(center).carets.front().x>layoutText(left).carets.front().x);
    auto plain=sample("Hi");auto tracked=plain;tracked.tracking=40;
    const float trackedX=layoutText(tracked).carets[1].x,plainX=layoutText(plain).carets[1].x;
    if(!(trackedX>plainX+10))throw std::runtime_error("tracking "+std::to_string(plainX)+" -> "+std::to_string(trackedX));
    auto led=sample("A\nB");led.boxWidth=200;led.boxHeight=200;led.leading=80;
    auto automatic=led;automatic.leading=0;
    REQUIRE(layoutText(led).carets.back().top>layoutText(automatic).carets.back().top+10);
    boxed.alignment=TextAlignment::Center;boxed.tracking=20;boxed.leading=70;boxed.fontStyle="Bold";
    auto regular=layoutText(sample("WWW"));
    auto bold=sample("WWW");bold.fontStyle="Bold";auto boldLayout=layoutText(bold);
    REQUIRE(boldLayout.carets.back().x!=regular.carets.back().x);
    auto replaced=boxed;REQUIRE(replaceText(replaced,0,0,""));
    REQUIRE(replaced.tracking==20&&replaced.leading==70&&replaced.alignment==TextAlignment::Center&&replaced.boxWidth==80&&replaced.fontStyle=="Bold");
    boxed.tracking=1001;REQUIRE(!textRunsValid(boxed));
    boxed.tracking=20;boxed.leading=5001;REQUIRE(!textRunsValid(boxed));
    boxed.leading=70;boxed.boxWidth=8;REQUIRE(!textRunsValid(boxed));
}
void caret_positions(){
    const HRESULT started=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    REQUIRE(SUCCEEDED(started)||started==RPC_E_CHANGED_MODE);
    struct Cleanup{HRESULT hr;~Cleanup(){if(SUCCEEDED(hr))CoUninitialize();}} cleanup{started};
    auto empty=layoutText(sample(""));
    REQUIRE(!empty.raster&&empty.carets.size()==1&&empty.carets[0].height>0);
    auto laid=layoutText(sample("Hi"));
    REQUIRE(laid.raster&&laid.carets.size()==3&&laid.width>2&&laid.height>2);
    REQUIRE(laid.carets[1].x>laid.carets[0].x);
}
void selection_clusters(){
    const HRESULT started=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    REQUIRE(SUCCEEDED(started)||started==RPC_E_CHANGED_MODE);
    struct Cleanup{HRESULT hr;~Cleanup(){if(SUCCEEDED(hr))CoUninitialize();}} cleanup{started};
    auto italic=sample("Italic");italic.fontStyle="Italic";
    auto laid=layoutText(italic);
    REQUIRE(!laid.clusters.empty());
    float width=0;
    for(const auto& cluster:laid.clusters)width+=cluster.width;
    REQUIRE(width>0);
    auto bold=sample("Bold");bold.fontStyle="Bold";
    auto boldLaid=layoutText(bold);
    REQUIRE(!boldLaid.clusters.empty());
    REQUIRE(boldLaid.clusters.front().width>0||boldLaid.clusters.size()>1);
    auto word=layoutText(sample("Hello"));
    REQUIRE(word.clusters.size()>=2);
    float total=0,widest=0;
    for(const auto& cluster:word.clusters){total+=cluster.width;widest=std::max(widest,cluster.width);}
    REQUIRE(widest<total*0.8f);
}

int main(int argc,char** argv){
    std::map<std::string,void(*)()> tests{{"partial_color",partial_color},{"whole_color",whole_color},{"inherit_insert",inherit_insert},{"reject_overflow",reject_overflow},{"utf16_pair",utf16_pair},{"font_range",font_range},{"caret_positions",caret_positions},{"paragraph_layout",paragraph_layout},{"selection_clusters",selection_clusters}};
    try{if(argc!=2||!tests.contains(argv[1]))throw std::runtime_error("Specify one text style test case");tests.at(argv[1])();std::cout<<"PASS "<<argv[1]<<"\n";return 0;}
    catch(const std::exception& error){std::cerr<<"FAIL "<<(argc>1?argv[1]:"arguments")<<": "<<error.what()<<"\n";return 1;}
}

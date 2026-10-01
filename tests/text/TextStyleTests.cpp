#include "text/TextStyle.h"
#include "text/TextRaster.h"
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

int main(int argc,char** argv){
    std::map<std::string,void(*)()> tests{{"partial_color",partial_color},{"whole_color",whole_color},{"inherit_insert",inherit_insert},{"reject_overflow",reject_overflow},{"utf16_pair",utf16_pair},{"font_range",font_range},{"caret_positions",caret_positions}};
    try{if(argc!=2||!tests.contains(argv[1]))throw std::runtime_error("Specify one text style test case");tests.at(argv[1])();std::cout<<"PASS "<<argv[1]<<"\n";return 0;}
    catch(const std::exception& error){std::cerr<<"FAIL "<<(argc>1?argv[1]:"arguments")<<": "<<error.what()<<"\n";return 1;}
}

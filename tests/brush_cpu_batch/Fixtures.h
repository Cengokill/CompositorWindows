#pragma once
#include "graphics/BrushCoverage.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>
namespace batch_fixture {
using namespace compositor::graphics;
struct Point{double x,y;};
inline BrushSegment segment(Point a,Point b){return{float(a.x),float(a.y),float(b.x),float(b.y)};}
#include "FrozenCurve.inc"
inline Point trajectory(int i){return i<=60?Point{700.,3200.-i*40.}:Point{700.+(i-60)*40.,800.};}
inline BrushUniforms uniform(float radius=400,float hardness=0,float x=0,float y=0){BrushUniforms u;u.geometry={x,y,radius,hardness};u.canvas={4000,4000,1,std::max(.25f,radius*2*(hardness>=1?.015f:.025f))};return u;}
inline std::set<int> affected(std::span<const BrushSegment> segments){std::set<int> keys;for(const auto& s:segments){const double left=std::max(0.,double(std::min(s.x0,s.x1))-402),right=std::min(4000.,double(std::max(s.x0,s.x1))+402),top=std::max(0.,double(std::min(s.y0,s.y1))-402),bottom=std::min(4000.,double(std::max(s.y0,s.y1))+402);if(left>=right||top>=bottom)continue;for(int y=int(std::floor(top))/256;y<=(int(std::ceil(bottom))-1)/256;++y)for(int x=int(std::floor(left))/256;x<=(int(std::ceil(right))-1)/256;++x)keys.insert(y*16+x);}return keys;}
struct Event{std::vector<BrushSegment> settled,tail;std::vector<int> keys;};
inline std::vector<Event> corpus(){std::vector<Event> events;std::vector<Point> samples;std::set<int> previousTail;for(int i=0;i<=120;++i){samples.push_back(trajectory(i));if(samples.size()>4)samples.erase(samples.begin());const auto n=samples.size();Event event;if(n==1)event.settled.push_back(segment(samples[0],samples[0]));else if(n>=3)event.settled=brushContinuousCurve(samples[n-3],samples[n-2],samples[n>=4?n-4:0],samples[n-1]);if(n>=2)event.tail.push_back(segment(samples[n-2],samples[n-1]));auto nextTail=affected(event.tail),keys=affected(event.settled);keys.insert(nextTail.begin(),nextTail.end());keys.insert(previousTail.begin(),previousTail.end());previousTail=std::move(nextTail);event.keys.assign(keys.begin(),keys.end());events.push_back(std::move(event));}return events;}
inline std::vector<BrushTileRender> requests(const Event& event,std::map<int,BrushTile>& tiles){std::vector<BrushTileRender> requests;for(int key:event.keys){const int x=(key%16)*256,y=(key/16)*256;auto [it,added]=tiles.try_emplace(key,uint32_t(std::min(256,4000-x)),uint32_t(std::min(256,4000-y)));(void)added;requests.push_back({&it->second,uniform(400,0,float(x),float(y))});}return requests;}
}

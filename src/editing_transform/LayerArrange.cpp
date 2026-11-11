// Behavior based on Compositor 002737ada0d0b50ac407cc8dbfa9473a91992577.
// Copyright (c) 2026 Wonder Assembly LLC. MIT license in upstream/LICENSE.
#include "LayerArrange.h"
#include "TransformGeometry.h"
#include "editing/Selection.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_map>
#include <unordered_set>
namespace compositor::editing_transform {
namespace {
struct Item { std::vector<std::string> members; Rect box; };
Rect unite(Rect a,Rect b){double x=std::min(a.x,b.x),y=std::min(a.y,b.y);return {x,y,std::max(a.x+a.width,b.x+b.width)-x,std::max(a.y+a.height,b.y+b.height)-y};}
std::vector<Item> items(const Document& d,const layers::SelectionState& selection){
    std::unordered_set<std::string> selected(selection.ids.begin(),selection.ids.end());
    std::unordered_map<std::string,const Layer*> byId;for(const auto& l:d.layers)byId.emplace(l.id,&l);
    std::unordered_map<std::string,size_t> owners;std::vector<Item> out;
    for(const auto& p:visiblePlacements(d)){
        const Layer* l=byId.at(p.id);std::string owner=selected.contains(l->id)?l->id:std::string{};
        for(auto parent=l->parentId;!parent.empty();){auto found=byId.find(parent);if(found==byId.end())break;if(selected.contains(parent))owner=parent;parent=found->second->parentId;}
        if(owner.empty())continue;auto box=bounds(p.transform);auto [it,inserted]=owners.emplace(owner,out.size());
        if(inserted)out.push_back({{},box});else out[it->second].box=unite(out[it->second].box,box);
        out[it->second].members.push_back(p.id);
        if(owner!=p.id)for(auto parent=l->parentId;!parent.empty();){const auto* folder=byId.at(parent);if(folder->mask)out[it->second].members.push_back(parent);if(parent==owner)break;parent=folder->parentId;}
    }return out;
}
}
size_t arrangeItemCount(const Document& d,const layers::SelectionState& s){return items(d,s).size();}
layers::EditResult arrange(const Document& d,layers::SelectionState selection,Arrange command){
    validateDocument(d);selection=layers::normalizeSelection(d,std::move(selection));auto groups=items(d,selection);Document next=d;
    const bool distribute=command>=Arrange::CentersX;
    const char* names[]{"Align Left Edges","Align Horizontal Centers","Align Right Edges","Align Top Edges","Align Vertical Centers","Align Bottom Edges","Distribute Horizontal Centers","Distribute Vertical Centers","Distribute Horizontal Spacing","Distribute Vertical Spacing"};
    const auto name=names[static_cast<int>(command)];
    if(groups.empty()||(distribute&&groups.size()<3))return {std::move(next),std::move(selection),false,name};
    std::vector<Point> offsets(groups.size());
    if(!distribute){
        Rect target=groups.front().box;bool hasSelection=false;
        if(d.selection){if(d.selection->outline&&!d.selection->outline->empty()){auto b=d.selection->outline->bounds();target={b.x,b.y,b.width,b.height};hasSelection=true;}
            else if(d.selection->coverage&&d.selection->coverage->hasCoverage()){auto b=d.selection->coverage->nonzeroBounds();target={double(b.x),double(b.y),double(b.width),double(b.height)};hasSelection=true;}}
        if(!hasSelection){if(groups.size()==1)target={0,0,double(d.width),double(d.height)};else for(size_t i=1;i<groups.size();++i)target=unite(target,groups[i].box);}
        for(size_t i=0;i<groups.size();++i){auto b=groups[i].box;switch(command){
        case Arrange::Left:offsets[i].x=target.x-b.x;break;case Arrange::CenterX:offsets[i].x=target.x+target.width/2-b.x-b.width/2;break;case Arrange::Right:offsets[i].x=target.x+target.width-b.x-b.width;break;
        case Arrange::Top:offsets[i].y=target.y-b.y;break;case Arrange::CenterY:offsets[i].y=target.y+target.height/2-b.y-b.height/2;break;case Arrange::Bottom:offsets[i].y=target.y+target.height-b.y-b.height;break;default:break;}}
    }else{
        const bool horizontal=command==Arrange::CentersX||command==Arrange::SpacingX;const bool spacing=command==Arrange::SpacingX||command==Arrange::SpacingY;
        auto start=[&](size_t i){return horizontal?groups[i].box.x:groups[i].box.y;};auto length=[&](size_t i){return horizontal?groups[i].box.width:groups[i].box.height;};
        std::vector<size_t> order(groups.size());std::iota(order.begin(),order.end(),0);std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){return start(a)+length(a)/2<start(b)+length(b)/2;});
        const auto first=order.front(),last=order.back();double sum=0;for(auto i:order)sum+=length(i);const double gap=(start(last)+length(last)-start(first)-sum)/double(order.size()-1);double position=start(first);
        for(size_t rank=0;rank<order.size();++rank){auto i=order[rank];double delta=spacing?position-start(i):(start(first)+length(first)/2+(start(last)+length(last)/2-start(first)-length(first)/2)*double(rank)/double(order.size()-1))-(start(i)+length(i)/2);if(horizontal)offsets[i].x=delta;else offsets[i].y=delta;position+=length(i)+gap;}
    }
    std::unordered_map<std::string,Point> moves;for(size_t i=0;i<groups.size();++i){Point p{std::round(offsets[i].x),std::round(offsets[i].y)};if(p.x||p.y)for(const auto& id:groups[i].members)moves.emplace(id,p);}
    for(auto& l:next.layers){auto it=moves.find(l.id);if(it==moves.end())continue;auto old=l.transform;l.transform.x+=it->second.x;l.transform.y+=it->second.y;if(!l.group&&l.mask&&(l.mask->raster->width!=1||l.mask->raster->height!=1)){if(l.mask->linked&&l.mask->placement){l.mask->placement->x+=it->second.x;l.mask->placement->y+=it->second.y;}else if(!l.mask->linked&&!l.mask->placement)l.mask->placement=old;}}
    validateDocument(next);return {std::move(next),std::move(selection),!moves.empty(),name};
}
}

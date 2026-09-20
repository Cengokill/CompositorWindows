#include "Document.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace compositor {
std::shared_ptr<const Raster> CompositeCache::render(const Document& doc) {
    if(doc.layers.size()==1){const auto&layer=doc.layers.front();
        if(layer.visible&&!layer.group&&layer.raster&&layer.raster->width==doc.width&&layer.raster->height==doc.height&&
           layer.opacity==1&&layer.blend==Blend::Normal&&!layer.mask&&layer.parentId.empty()&&layer.maskSourceId.empty()&&
           layer.adjustmentJson.empty()&&layer.transform.x==0&&layer.transform.y==0&&layer.transform.width==doc.width&&
           layer.transform.height==doc.height&&layer.transform.rotation==0&&!layer.transform.flipX&&!layer.transform.flipY){
            previous_=doc;output_=layer.raster;return output_;
        }
    }
    const int columns=(doc.width+255)/256, rows=(doc.height+255)/256;
    bool all=!previous_ || previous_->width!=doc.width || previous_->height!=doc.height || previous_->layers.size()!=doc.layers.size();
    std::set<size_t> dirty;
    if(!all) for(size_t i=0;i<doc.layers.size();++i) {
        const auto& before=previous_->layers[i]; const auto& after=doc.layers[i];
        if(before==after) continue;
        auto metadata=before; metadata.raster=after.raster;
        if(metadata!=after || !before.raster || !after.raster ||
           before.raster->width!=after.raster->width || before.raster->height!=after.raster->height ||
           std::any_of(doc.layers.begin(),doc.layers.end(),[](const Layer&l){return !l.maskSourceId.empty()||!l.adjustmentJson.empty();})) {all=true;break;}
        const int sourceColumns=(after.raster->width+255)/256;
        for(size_t t=0;t<after.raster->tiles.size();++t) if(before.raster->tiles[t]!=after.raster->tiles[t]) {
            int x=int(t%sourceColumns)*256,y=int(t/sourceColumns)*256;
            double x0=1e20,y0=1e20,x1=-1e20,y1=-1e20;
            for(auto corner:std::array<Point,4>{Point{double(x-1),double(y-1)},Point{double(x+257),double(y-1)},Point{double(x-1),double(y+257)},Point{double(x+257),double(y+257)}}) {
                auto p=after.transform.fromUnit({corner.x/after.raster->width,corner.y/after.raster->height});
                x0=std::min(x0,p.x);y0=std::min(y0,p.y);x1=std::max(x1,p.x);y1=std::max(y1,p.y);
            }
            for(int ty=std::max(0,int(std::floor(y0/256)));ty<std::min(rows,int(std::ceil(y1/256)));++ty)
                for(int tx=std::max(0,int(std::floor(x0/256)));tx<std::min(columns,int(std::ceil(x1/256)));++tx)dirty.insert(size_t(ty)*columns+tx);
        }
    }
    auto result=std::make_shared<Raster>();result->width=doc.width;result->height=doc.height;
    if(!all)result->tiles=output_->tiles;
    else {result->tiles.resize(size_t(columns)*rows);for(size_t i=0;i<result->tiles.size();++i)dirty.insert(i);}
    SoftwareRenderer renderer;
    for(auto index:dirty){int x=int(index%columns)*256,y=int(index/columns)*256;
        result->tiles[index]=renderer.render(doc,x,y,std::min(256,doc.width-x),std::min(256,doc.height-y))->tiles.front();}
    previous_=doc;output_=result;return output_;
}
}

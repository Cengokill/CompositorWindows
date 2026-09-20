// Bounded independent review probe. Nonzero result preserves discovered defects;
// this is not included in native graphics pass counts.
#include "core/Document.h"
#include <iostream>
#include <stdexcept>
using namespace compositor;
int main(){int issues=0;
    Pixel p{};const uint8_t source[]={1,2,3,255,99,4,5,6,255};
    try{auto r=Raster::fromRgba(1,2,source,5);p=r->pixel(0,1);
        if(p!=Pixel{4,5,6,255}){++issues;std::cout<<"ISSUE R1 odd byte stride: expected 4,5,6,255; actual "<<int(p.r)<<','<<int(p.g)<<','<<int(p.b)<<','<<int(p.a)<<'\n';}}
    catch(const std::exception&){std::cout<<"RESOLVED R1: unaligned byte stride explicitly rejected\n";}
    Document doc;doc.width=1;doc.height=1;Layer base;base.id="base";base.raster=Raster::filled(1,1,{0,0,128,128});base.transform={0,0,1,1};Layer child;child.id="child";child.raster=Raster::filled(1,1,{255,0,0,255});child.transform={0,0,1,1};child.maskSourceId="base";doc.layers={base,child};
    p=SoftwareRenderer().render(doc,0,0,1,1)->pixel(0,0);
    if(p!=Pixel{128,0,0,128}){++issues;std::cout<<"ISSUE R2 visible Normal clipping stack: expected 128,0,0,128; actual "<<int(p.r)<<','<<int(p.g)<<','<<int(p.b)<<','<<int(p.a)<<'\n';}
    auto malformed=std::make_shared<Raster>();malformed->width=1;malformed->height=1;doc.layers[0].raster=malformed;bool rejected=false;
    try{validateDocument(doc);}catch(const std::exception&){rejected=true;}if(!rejected){++issues;std::cout<<"ISSUE R3 malformed Raster with empty tile storage passes validateDocument\n";}
    p=Raster::filled(1,1,{255,20,10,0})->pixel(0,0);if(p.r>p.a||p.g>p.a||p.b>p.a){++issues;std::cout<<"ISSUE R4 Raster::filled admits nonpremultiplied pixels\n";}
    History history;history.byteLimit=0;std::optional<Document> d=Document{};auto selection=std::make_shared<GrayRaster>();selection->width=100;selection->height=100;selection->pixels.resize(10000,255);d->selection=Selection{selection};
    history.begin("Deselect",d,"");d->selection.reset();history.end(d,"");if(history.undoCount()!=0){++issues;std::cout<<"ISSUE R5 history byteLimit=0 retains 10000-byte selection; reported retainedBytes="<<history.retainedBytes(d)<<'\n';}
    std::cout<<"review_issues="<<issues<<'\n';return issues?1:0;
}

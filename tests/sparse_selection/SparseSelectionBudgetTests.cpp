#include "editing/Selection.h"
#include "editing_transform/PixelTransform.h"
#include <QCoreApplication>
#include <cstdlib>
#include <new>
#include <cstdio>
#include <stdexcept>
namespace {bool guardLargeAllocations=false;int attempts=0;struct UnexpectedRasterAllocation{};}
void* operator new(size_t size){if(guardLargeAllocations&&size>=128*1024){++attempts;throw UnexpectedRasterAllocation{};}if(auto* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}
int main(int argc,char** argv){QCoreApplication app(argc,argv);using namespace compositor;
    auto coverage=editing::SelectionOutline::rectangle({0,0,30000,30000},false).rasterize(30000,30000);
    Layer layer;layer.id="budget";layer.transform={15000,12000,32,32};layer.raster=Raster::filled(32,32,{80,100,120,255});
    editing_transform::PixelTransformSession session(layer,coverage,editing_transform::PixelTransformKind::Affine);
    guardLargeAllocations=true;bool rejected=false;
    try{session.begin();}catch(const std::invalid_argument&){rejected=true;}catch(const UnexpectedRasterAllocation&){guardLargeAllocations=false;std::fprintf(stderr,"FAIL allocated a floating tile before checking the output budget (attempts=%d)\n",attempts);return 1;}
    guardLargeAllocations=false;
    if(!rejected||attempts){std::fprintf(stderr,"FAIL giant floating selection did not reject before allocation\n");return 1;}
    std::puts("PASS affine allocation budget checked before creating floating pixels");return 0;
}

#include "ui/ImportCommit.h"
#include <cstdlib>
#include <cstdio>
#include <malloc.h>
#include <new>
#include <string_view>

namespace fault {
thread_local long remaining=-1;
thread_local bool failed=false;
thread_local size_t allocationsAfterFailure{};
void allocation(){if(remaining<0)return;if(failed)++allocationsAfterFailure;if(remaining==0){failed=true;throw std::bad_alloc();}--remaining;}
void arm(long count){remaining=count;failed=false;allocationsAfterFailure=0;}
void disarm(){remaining=-1;}
}
void* operator new(size_t size){fault::allocation();if(auto* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void* operator new[](size_t size){return ::operator new(size);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}
void operator delete[](void* p,size_t)noexcept{std::free(p);}
void* operator new(size_t size,std::align_val_t alignment){fault::allocation();if(auto* p=_aligned_malloc(size?size:1,size_t(alignment)))return p;throw std::bad_alloc();}
void* operator new[](size_t size,std::align_val_t alignment){return ::operator new(size,alignment);}
void operator delete(void* p,std::align_val_t)noexcept{_aligned_free(p);}
void operator delete[](void* p,std::align_val_t)noexcept{_aligned_free(p);}
void operator delete(void* p,size_t,std::align_val_t)noexcept{_aligned_free(p);}
void operator delete[](void* p,size_t,std::align_val_t)noexcept{_aligned_free(p);}

using namespace compositor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Fixture {
    EditorProject project;
    ui::ImportResult result;
    Fixture(){
        Document document;document.id=std::string(64,'d');document.width=64;document.height=48;
        Layer layer;layer.id=std::string(64,'a');layer.name=std::string(64,'n');layer.raster=Raster::filled(2,2,{25,50,75,255});layer.transform={0,0,2,2};
        auto mask=std::make_shared<GrayRaster>();mask->width=2;mask->height=2;mask->pixels={255,128,64,0};layer.mask=Mask{mask};document.layers={layer};
        project.document=std::move(document);project.active=layer.id;project.selected={layer.id};project.maskSelected=true;
        project.collapsedGroups={std::string(64,'g'),std::string(64,'h')};
        edit(65);edit(66);auto before=project.history.undo();project.document=std::move(before->document);project.active=std::move(before->activeLayer);project.history.markSaved();
        result.before={project.document,project.active};result.after=result.before;
        layer.id=std::string(64,'b');layer.name="Imported";result.after.document->layers.push_back(layer);result.after.active=layer.id;result.imported=1;result.expandedGroups={std::string(64,'g')};
    }
    void edit(int width){project.history.begin("Earlier",project.document,project.active);project.document->width=width;project.history.end(project.document,project.active);}
    void unchanged(){
        require(project.document==result.before.document&&project.active==result.before.active,"failed import changed document or active layer");
        require(project.selected==std::vector<std::string>{result.before.active}&&project.maskSelected,"failed import changed selected layers or mask target");
        require(project.collapsedGroups==std::unordered_set<std::string>{std::string(64,'g'),std::string(64,'h')},"failed import changed collapsed groups");
        require(project.history.undoCount()==1&&project.history.canUndo()&&project.history.canRedo()&&!project.history.modified(),"failed import changed history, redo, or saved revision");
        require(!project.history.cancel(),"failed import left a pending history transaction");
    }
    void committed(){
        require(project.document==result.after.document&&project.active==result.after.active,"import did not install prepared state");
        require(project.selected==std::vector<std::string>{result.after.active}&&!project.maskSelected,"successful import did not select imported image");
        require(project.collapsedGroups==std::unordered_set<std::string>{std::string(64,'h')},"successful import did not expand only imported parent groups");
        require(project.history.undoCount()==2&&project.history.canUndo()&&!project.history.canRedo()&&project.history.modified(),"successful import did not record exactly one edit");
    }
};
void allocationSweep(){
    size_t failures=0;
    for(long point=0;point<4096;++point){
        Fixture fixture;bool failed=false;
        try{fault::arm(point);require(!ui::commitPreparedImport(fixture.project,fixture.result),"existing document reported first import");}
        catch(const std::bad_alloc&){failed=true;}
        const auto rollbackAllocations=fault::allocationsAfterFailure;fault::disarm();
        if(failed){
            ++failures;require(rollbackAllocations==0,"import rollback attempted allocation after failure");fixture.unchanged();
            ui::commitPreparedImport(fixture.project,fixture.result);fixture.committed();
            auto prior=fixture.project.history.undo();require(prior&&prior->document==fixture.result.before.document&&prior->activeLayer==fixture.result.before.active&&!fixture.project.history.modified(),"retry undo did not restore saved state");
        }else{fixture.committed();require(failures>0,"allocation sweep did not inject failures");std::printf("PASS allocation_sweep allocation_points=%zu\n",failures);return;}
    }
    throw std::runtime_error("allocation sweep did not terminate");
}
void staleResult(){Fixture fixture;fixture.result.before.document->width=999;bool rejected=false;try{ui::commitPreparedImport(fixture.project,fixture.result);}catch(const std::runtime_error&){rejected=true;}fixture.result.before.document->width=65;require(rejected,"stale result was not rejected");fixture.unchanged();std::puts("PASS stale_result");}
}
int main(int argc,char** argv){try{require(argc==2,"provide named import fault case");const std::string_view name=argv[1];if(name=="allocation_sweep")allocationSweep();else if(name=="stale_result")staleResult();else throw std::runtime_error("unknown import fault case");return 0;}catch(const std::exception& error){fault::disarm();std::fprintf(stderr,"FAIL %s\n",error.what());return 1;}}

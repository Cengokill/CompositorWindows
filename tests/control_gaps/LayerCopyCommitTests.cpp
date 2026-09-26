#include "ui/LayerCopyCommit.h"
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
bool commit(EditorProject& project,const std::optional<Document>& before,const std::string& beforeActive,const layers::EditResult& prepared){
#ifdef LAYER_COPY_COMMIT_BEFORE
    // Exact original menu transaction after copySubtree, retained to expose its
    // allocation failure. The witness prepares its mutable CopyResult first.
    (void)before;(void)beforeActive;const bool first=!project.document;
    layers::CopyResult result;result.edit=prepared;auto* target=&project;
    target->history.begin("Copy Layers from Project",target->document,target->active);target->document=std::move(result.edit.document);target->active=result.edit.selection.primary;target->selected=result.edit.selection.ids;target->maskSelected=false;target->history.end(target->document,target->active);
    return first;
#else
    return ui::commitPreparedLayerCopy(project,before,beforeActive,prepared);
#endif
}
struct Fixture {
    EditorProject project;
    std::optional<Document> before;
    std::string active;
    layers::EditResult prepared;
    Fixture(bool empty=false){
        Document document;document.id=std::string(64,'d');document.width=64;document.height=48;
        Layer layer;layer.id=std::string(64,'a');layer.name=std::string(64,'n');layer.raster=Raster::filled(2,2,{25,50,75,255});layer.transform={0,0,2,2};
        auto mask=std::make_shared<GrayRaster>();mask->width=2;mask->height=2;mask->pixels={255,128,64,0};layer.mask=Mask{mask};document.layers={layer};
        if(!empty){project.document=document;project.active=layer.id;project.selected={layer.id};project.maskSelected=true;edit(65);edit(66);auto prior=project.history.undo();project.document=std::move(prior->document);project.active=std::move(prior->activeLayer);project.history.markSaved();}
        project.collapsedGroups={std::string(64,'g'),std::string(64,'h')};before=project.document;active=project.active;
        layer.id=std::string(64,'b');layer.name="Copied";prepared.document=before?*before:document;prepared.document.layers.push_back(layer);prepared.selection={{layer.id},layer.id};prepared.changed=true;
    }
    void edit(int width){project.history.begin("Earlier",project.document,project.active);project.document->width=width;project.history.end(project.document,project.active);}
    void unchanged(){
        require(project.document==before&&project.active==active,"failed copy changed document or active layer");
        require(project.selected==(before?std::vector<std::string>{active}:std::vector<std::string>{})&&project.maskSelected==before.has_value(),"failed copy changed selected layers or mask target");
        require(project.collapsedGroups==std::unordered_set<std::string>{std::string(64,'g'),std::string(64,'h')},"failed copy changed collapsed groups");
        require(project.history.undoCount()==(before?1u:0u)&&project.history.canRedo()==before.has_value()&&!project.history.modified(),"failed copy changed history, redo, or saved revision");
        require(!project.history.cancel(),"failed copy left a pending history transaction");
    }
    void committed(){
        require(project.document==prepared.document&&project.active==prepared.selection.primary,"copy did not install prepared state");
        require(project.selected==prepared.selection.ids&&!project.maskSelected,"successful copy did not select copied image");
        require(project.collapsedGroups==std::unordered_set<std::string>{std::string(64,'g'),std::string(64,'h')},"copy changed collapsed groups");
        require(project.history.undoCount()==(before?2u:1u)&&!project.history.canRedo()&&project.history.modified(),"successful copy did not record exactly one edit");
    }
};
void allocationSweep(bool empty){
    size_t failures=0;
    for(long point=0;point<4096;++point){
        Fixture fixture(empty);bool failed=false;
        try{fault::arm(point);require(commit(fixture.project,fixture.before,fixture.active,fixture.prepared)==empty,"copy first-document flag");}
        catch(const std::bad_alloc&){failed=true;}
        const auto rollbackAllocations=fault::allocationsAfterFailure;fault::disarm();
        if(failed){
            ++failures;require(rollbackAllocations==0,"copy rollback attempted allocation after failure");fixture.unchanged();
            commit(fixture.project,fixture.before,fixture.active,fixture.prepared);fixture.committed();
            auto prior=fixture.project.history.undo();require(prior&&prior->document==fixture.before&&prior->activeLayer==fixture.active&&!fixture.project.history.modified(),"retry undo did not restore saved state");
        }else{fixture.committed();require(failures>0,"allocation sweep did not inject failures");std::printf("PASS %s allocation_points=%zu rollback_allocations=0\n",empty?"empty_allocation_sweep":"allocation_sweep",failures);return;}
    }
    throw std::runtime_error("allocation sweep did not terminate");
}
void staleResult(){Fixture fixture;auto stale=fixture.before;stale->width=999;bool rejected=false;try{commit(fixture.project,stale,fixture.active,fixture.prepared);}catch(const std::runtime_error&){rejected=true;}require(rejected,"stale result was not rejected");fixture.unchanged();std::puts("PASS stale_result");}
}
int main(int argc,char** argv){try{require(argc==2,"provide named copy fault case");const std::string_view name=argv[1];if(name=="allocation_sweep")allocationSweep(false);else if(name=="empty_allocation_sweep")allocationSweep(true);else if(name=="stale_result")staleResult();else throw std::runtime_error("unknown copy fault case");return 0;}catch(const std::exception& error){fault::disarm();std::fprintf(stderr,"FAIL %s\n",error.what());return 1;}}

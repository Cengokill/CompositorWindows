#include "layers/LayerOperations.h"
#include "graphics/StackRenderer.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace compositor;
using namespace compositor::layers;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> void fails(F f,const char* message){try{f();}catch(const std::exception&){return;}throw std::runtime_error(message);}
SelectionState selected(std::initializer_list<std::string> ids,std::string primary={}){SelectionState s{ids,primary};if(s.primary.empty()&&!s.ids.empty())s.primary=s.ids.back();return s;}
const Layer& get(const Document& d,const std::string& id){auto at=std::find_if(d.layers.begin(),d.layers.end(),[&](const Layer& l){return l.id==id;});if(at==d.layers.end())throw std::runtime_error("Missing test layer");return *at;}
Layer layer(std::string id,std::string parent={},bool group=false){Layer l;l.id=std::move(id);l.name=l.id;l.parentId=std::move(parent);l.group=group;l.transform={0,0,2,2};l.transform.sampling=Transform::Sampling::Nearest;return l;}
Document document(std::vector<Layer> layers={},int w=2,int h=2){Document d;d.id="document";d.width=w;d.height=h;d.layers=std::move(layers);validateDocument(d);return d;}
std::shared_ptr<const Raster> alpha(std::initializer_list<std::uint8_t> values,std::uint8_t color=255){std::vector<std::uint8_t> p;for(auto a:values){p.push_back(std::uint8_t(unsigned(color)*a/255));p.push_back(0);p.push_back(0);p.push_back(a);}return Raster::fromRgba(2,2,p.data(),8);}
Mask solidMask(std::uint8_t value){return {std::make_shared<GrayRaster>(GrayRaster{1,1,{value}}),true,true,{}};}
std::vector<std::string> ids(const Document& d){std::vector<std::string> out;for(const auto& l:d.layers)out.push_back(l.id);return out;}
void hierarchy(){
    auto outer=layer("outer",{},true),inner=layer("inner","outer",true),child=layer("child","inner"),other=layer("other");auto d=document({outer,inner,child,other});
    check(!canPlace(d,"outer","inner")&&!canPlace(d,"inner","inner")&&!canPlace(d,"child","other"),"cycles and raster parent rejected");auto rows=entries(d,false);check(rows.size()==4&&rows[0].depth==0&&rows[1].depth==1&&rows[2].depth==2,"nested hierarchy depths");rows=entries(d,true,{"outer"});check(rows.size()==2&&rows[0].id=="other"&&rows[1].id=="outer","top-first collapsed rows");d.layers[0].visible=false;rows=entries(d);check(!rows[0].visible&&!rows[1].visible&&!rows[2].visible&&rows[3].visible,"hidden ancestor overrides children");
    auto out=moveOut(d,selected({"child"}));check(get(out.document,"child").parentId=="outer"&&get(d,"child").parentId=="inner","move out preserves input");auto erased=erase(d,selected({"outer"}),DeleteMode::RemoveLinks);check(ids(erased.document)==std::vector<std::string>{"other"}&&erased.selection.primary=="other","folder delete carries descendants");
    auto invalid=d;invalid.layers[0].parentId="inner";fails([&]{entries(invalid);},"invalid graph must fail");
}
void grouping(){
    auto d=document({layer("a")},100,100);auto first=group(d,selected({"a"}));auto inner=first.selection.primary;auto second=group(first.document,first.selection);auto outer=second.selection.primary;check(get(second.document,"a").parentId==inner&&get(second.document,inner).parentId==outer&&get(second.document,outer).parentId.empty(),"single folder wraps instead of child-folder creation");
    auto fixture=document({layer("folder",{},true),layer("child","folder"),layer("sibling")});auto grouped=group(fixture,selected({"folder","child","sibling"}));auto wrapper=grouped.selection.primary;check(grouped.selection.ids==std::vector<std::string>{wrapper},"group selects wrapper only");check(get(grouped.document,"folder").parentId==wrapper&&get(grouped.document,"sibling").parentId==wrapper&&get(grouped.document,"child").parentId=="folder","selected descendants stay inside selected folder");auto rows=entries(grouped.document);std::vector<std::string> pixels;for(const auto& row:rows)if(!get(grouped.document,row.id).group)pixels.push_back(row.id);check(pixels==std::vector<std::string>({"child","sibling"}),"group preserves hierarchical draw order");
    fixture=document({layer("one",{},true),layer("a","one"),layer("two",{},true),layer("b","two")});grouped=group(fixture,selected({"a","b"}));wrapper=grouped.selection.primary;check(get(grouped.document,wrapper).parentId.empty()&&get(grouped.document,"a").parentId==wrapper&&get(grouped.document,"b").parentId==wrapper,"different parents use common ancestor");auto emptyGroup=group(document(),{});check(emptyGroup.document.layers.size()==1&&emptyGroup.document.layers[0].group,"empty selection creates empty group");auto nested=addGroup(emptyGroup.document,emptyGroup.selection);check(get(nested.document,nested.selection.primary).parentId==emptyGroup.selection.primary,"new folder goes inside active folder");
}
void movesAndDuplicate(){
    auto a=layer("a"),b=layer("b"),c=layer("c");a.raster=alpha({255,128,32,0});a.mask=solidMask(128);auto d=document({a,b,c});auto copy=duplicateLayer(d,selected({"a"}),"a",Placement{{},"c",false});auto id=copy.selection.primary;check(copy.action=="Duplicate Layer"&&copy.document.layers.back().id==id&&get(copy.document,id).name=="a copy"&&get(copy.document,id).raster==a.raster&&get(copy.document,id).mask->raster==a.mask->raster,"option-drag duplicate location and shared assets");check(ids(d)==std::vector<std::string>({"a","b","c"}),"duplicate original stays unchanged");auto folder=document({layer("folder",{},true)});fails([&]{duplicateLayer(folder,selected({"folder"}),"folder");},"source folder duplication restriction");
    auto reordered=reorderTopFirst(d,selected({"a"}),{2},0);check(ids(reordered.document)==std::vector<std::string>({"b","c","a"}),"flat top-first offsets move to correct stack position");check(canMoveSibling(d,"b",1)&&!canMoveSibling(d,"c",1),"sibling move bounds");auto moved=moveSibling(d,selected({"b"}),1);check(ids(moved.document)==std::vector<std::string>({"a","c","b"}),"sibling swap");fails([&]{place(d,{},"a",{{},"missing",false});},"unknown drop target rejected");
}
void clipping(){
    auto d=document({layer("base"),layer("one"),layer("two")});check(!canToggleClipping(d,"base")&&canToggleClipping(d,"one"),"source clipping eligibility");d=toggleClipping(d,selected({"one"}),"one").document;d=toggleClipping(d,selected({"two"}),"two").document;check(get(d,"one").maskSourceId=="base"&&get(d,"two").maskSourceId=="base","clipped siblings share base");
    auto released=releaseClipping(d,{},"two");check(get(released.document,"two").maskSourceId.empty()&&get(released.document,"one").maskSourceId=="base","release child leaves lower siblings");released=releaseClipping(d,{},"one");check(get(released.document,"one").maskSourceId.empty()&&get(released.document,"two").maskSourceId.empty(),"release lower child releases contiguous higher siblings");
    auto moved=place(d,{},"two",{{},{},true});check(moved.document.layers.front().id=="two"&&get(moved.document,"two").maskSourceId.empty()&&get(moved.document,"one").maskSourceId=="base","drag outside clipping stack releases only detached link");
    d.layers.push_back(layer("drop"));auto adopted=place(d,{},"drop",{{},"base",false});check(get(adopted.document,"drop").maskSourceId=="base"&&get(adopted.document,"one").maskSourceId=="base","drop into stack adopts shared base");
    check(!canLink(d,"one","base")&&!canLink(d,"base","base"),"cyclic links rejected");auto adjustment=layer("adjustment");adjustment.adjustmentJson="{}";d.layers.push_back(adjustment);check(!canLink(d,"adjustment","drop")&&canLink(d,"base","adjustment"),"adjustment may be target but not alpha source");
}
void bakeAndDelete(){
    auto target=layer("target"),source=layer("source");target.raster=alpha({255,255,255,255});source.raster=alpha({255,0,128,255},0);source.visible=false;target.maskSourceId="source";auto d=document({target,source});graphics::StackRenderer renderer;auto before=renderer.render(d,0,0,2,2);auto plan=deletionPlan(d,selected({"source"}));check(plan.dependents==std::vector<std::string>{"target"},"delete exposes dependent choice");auto cancelled=erase(d,selected({"source"}),DeleteMode::Cancel);check(!cancelled.changed&&cancelled.document==d,"cancel preserves original graph");
    auto baked=erase(d,selected({"source"}),DeleteMode::Bake);auto after=renderer.render(baked.document,0,0,2,2);check(before->rgba()==after->rgba()&&get(baked.document,"target").maskSourceId.empty(),"source hidden-black alpha bake and delete preserves appearance");auto unlinked=erase(d,selected({"source"}),DeleteMode::RemoveLinks);check(get(unlinked.document,"target").raster==target.raster&&renderer.render(unlinked.document,0,0,2,2)->pixel(1,0).a==255,"remove links reveals original pixels");
    source.opacity=.5;target.opacity=.25;target.blend=Blend::Multiply;target.mask=solidMask(128);d=document({target,source});baked=erase(d,selected({"source"}),DeleteMode::Bake);const auto& kept=get(baked.document,"target");check(kept.opacity==.25&&kept.blend==Blend::Multiply&&kept.mask==target.mask&&kept.raster->pixel(0,0).a==128&&kept.raster->pixel(0,1).a==64,"bake only live dependency retains target appearance and own raster mask");check(kept.transform==target.transform,"bake preserves target transform");
    auto chain=layer("chain");chain.raster=alpha({0,255,255,255});chain.visible=false;source.opacity=1;source.maskSourceId="chain";target.opacity=1;target.mask.reset();d=document({target,source,chain});auto raster=bakeLiveMask(d,"target");check(raster->pixel(0,0).a==0&&raster->pixel(1,0).a==0&&raster->pixel(0,1).a==128&&raster->pixel(1,1).a==255,"source chains multiply");
    d.layers[1].maskSourceId.clear();d.layers[1].transform.x=1;raster=bakeLiveMask(d,"target");check(raster->pixel(0,0).a==0&&raster->pixel(1,0).a==255&&raster->pixel(0,1).a==0&&raster->pixel(1,1).a==128,"source movement changes bake coverage");
    Limits low;low.maxWorkingBytes=1;fails([&]{bakeLiveMask(d,"target",low);},"bake memory budget");low={};low.cancelled=[]{return true;};fails([&]{erase(d,selected({"source"}),DeleteMode::Bake,low);},"bake cancellation");
}
void multiDeleteAndHistory(){
    auto d=document({layer("keep"),layer("folder",{},true),layer("child","folder"),layer("top")});auto chosen=selected({"folder","top"});auto edited=erase(d,chosen,DeleteMode::RemoveLinks);check(ids(edited.document)==std::vector<std::string>{"keep"}&&edited.selection.primary=="keep"&&edited.action=="Delete Layers","multi deletion selection fallback");History history;std::optional<Document> current=d;history.begin(edited.action,current,chosen.primary);current=edited.document;history.end(current,edited.selection.primary);check(history.canUndo(),"one history edit available");auto undone=history.undo();check(undone&&undone->document==d&&!history.canUndo(),"one undo restores complete graph");check(history.redo()->document==edited.document,"redo reapplies immutable graph");
    auto normalized=normalizeSelection(d,{{"child","missing","keep","keep"},"missing"});check(normalized.ids==std::vector<std::string>({"keep","child"})&&normalized.primary=="keep","multiselection filters invalid and duplicate IDs");
}
void copyGraph(){
    auto folder=layer("folder",{},true),base=layer("base","folder"),child=layer("child","folder"),external=layer("external");base.raster=alpha({255,0,128,255});child.raster=alpha({255,255,255,255});child.maskSourceId="base";child.mask=solidMask(255);child.mask->placement=Transform{5,6,2,2};external.raster=alpha({255,255,255,255});auto source=document({folder,base,child,external});auto destination=document({layer("existing")},20,20);auto copied=copySubtree(source,"folder",destination);auto groupID=copied.ids.at("folder"),baseID=copied.ids.at("base"),childID=copied.ids.at("child");check(copied.ids.size()==3&&copied.edit.document.layers.size()==4&&groupID!="folder","copy remaps subtree UUIDs");check(get(copied.edit.document,childID).parentId==groupID&&get(copied.edit.document,childID).maskSourceId==baseID&&get(copied.edit.document,baseID).raster==base.raster,"copy preserves internal graph and immutable assets");check(get(copied.edit.document,childID).transform.x==9&&get(copied.edit.document,childID).mask->placement->x==14,"copy translates images and explicit masks by common center delta");check(source.layers.size()==4&&destination.layers.size()==1,"copy preserves both source snapshots");
    child.parentId.clear();child.mask.reset();source=document({base,child,external,folder});auto singleCopy=copySubtree(source,"child",document(),Point{1,1});const auto& detached=get(singleCopy.edit.document,singleCopy.ids.at("child"));check(detached.maskSourceId.empty()&&detached.raster->pixel(1,0).a==0&&detached.raster->pixel(0,1).a==128,"external clipping source baked during copy");
}
void merging(){
    auto low=layer("low"),high=layer("high");low.raster=alpha({255,128,32,0},0);high.raster=alpha({255,255,255,255});high.opacity=.5;auto d=document({low,high});graphics::StackRenderer renderer;auto before=renderer.render(d,0,0,2,2);auto p=mergePlan(d,selected({"high"}));check(p&&p->action=="Merge Down"&&p->name=="low"&&p->ids==std::vector<std::string>({"low","high"}),"merge-down plan uses lower name and siblings");auto merged=merge(d,selected({"high"}),renderer);check(merged.document.layers.size()==1&&renderer.render(merged.document,0,0,2,2)->rgba()==before->rgba()&&merged.document.layers[0].opacity==1&&merged.document.layers[0].blend==Blend::Normal,"merge bakes composite appearance");
    auto groupLayer=layer("folder",{},true);low.parentId=high.parentId="folder";d=document({groupLayer,low,high});p=mergePlan(d,selected({"folder"}));check(p&&p->action=="Merge Group"&&p->removed.size()==3,"merge group includes descendants");merged=merge(d,selected({"folder"}),renderer);check(merged.document.layers.size()==1&&merged.document.layers[0].name=="folder"&&!merged.document.layers[0].group,"merge group replaces folder");
    auto bottom=layer("bottom"),middle=layer("middle"),top=layer("top");bottom.raster=alpha({0,0,0,255});top.raster=alpha({0,0,0,128});middle.raster=alpha({255,255,255,255});middle.maskSourceId="bottom";d=document({bottom,middle,top});merged=merge(d,selected({"bottom","top"}),renderer);check(merged.document.layers.size()==2&&merged.document.layers.back().name=="top"&&get(merged.document,"middle").maskSourceId==merged.selection.primary,"merge selected retargets external clipping links and uses anchor");check(merged.document.layers.back().raster->width==1&&merged.document.layers.back().transform.x==1&&merged.document.layers.back().transform.y==1,"merge trims to document alpha bounds");
    check(!mergePlan(document({layer("only")}),selected({"only"}))&&!mergePlan(document({layer("empty",{},true)}),selected({"empty"})),"empty group or bottom layer ineligible");
    auto clear=document({layer("a"),layer("b")},7,5);merged=merge(clear,selected({"b"}),renderer);check(merged.document.layers[0].raster->width==7&&merged.document.layers[0].raster->height==5,"transparent merge keeps canvas grid");
    struct Failure:IRasterBackend{std::shared_ptr<const Raster> render(const Document&,int,int,int,int)const override{throw std::runtime_error("injected renderer failure");}} failure;fails([&]{merge(clear,selected({"b"}),failure);},"renderer failure propagates atomically");check(clear.layers.size()==2,"failed merge leaves source unchanged");
}
void depth(){
    std::vector<std::uint8_t> bytes{128,0,0,128,7,3,1,9,255,254,1,255,0,0,0,0};const auto pixels=Raster::fromRgba(2,2,bytes.data(),8);
    auto folder=layer("folder",{},true),eight=layer("eight","folder"),text=layer("text","folder"),shape=layer("shape","folder"),adjustment=layer("adjustment","folder"),gif=layer("gif","folder"),blank=layer("blank","folder"),sixteen=layer("sixteen"),outside=layer("outside");
    eight.raster=text.raster=shape.raster=gif.raster=sixteen.raster=outside.raster=pixels;sixteen.deep=deepFromRaster(*pixels);
    text.text=TextContent{"Text"};shape.shapeJson="{}";adjustment.adjustmentJson="{}";gif.animation=GifAnimation{{{pixels,100}},0};
    eight.opacity=.5;eight.blend=Blend::Multiply;eight.mask=solidMask(128);blank.transform={1,1,1,1};blank.mask=solidMask(255);
    const auto d=document({folder,eight,text,shape,adjustment,gif,blank,sixteen,outside});
    const auto chosen=selected({"eight","folder","sixteen"},"sixteen");
    check(depthTargets(d,chosen)==std::vector<std::string>({"eight","blank","sixteen"}),"targets cover selected folders once and skip unsupported layers");
    check(canConvertDepth(d,chosen,BitDepth::Sixteen)&&canConvertDepth(d,chosen,BitDepth::Eight),"a mixed selection can go either way");
    check(!canConvertDepth(d,selected({"text","shape","adjustment","gif"}),BitDepth::Sixteen),"unsupported layers enable nothing");

    const auto wide=convertDepth(d,chosen,BitDepth::Sixteen);
    check(wide.changed&&wide.action=="Convert to 16-Bit"&&wide.selection.ids==normalizeSelection(d,chosen).ids&&wide.selection.primary=="sixteen","conversion keeps the selection");
    const auto& converted=get(wide.document,"eight");
    check(converted.deep&&converted.raster==pixels&&converted.opacity==.5&&converted.blend==Blend::Multiply&&converted.mask==eight.mask&&converted.transform==eight.transform,"8-bit layer keeps pixels and appearance");
    for(std::size_t i=0;i<bytes.size();++i)check(converted.deep->samples[i]==promote8(bytes[i]),"16-bit samples are the exact 8-bit equivalents");
    const auto& filled=get(wide.document,"blank");
    check(filled.deep&&filled.raster&&filled.raster->width==2&&filled.raster->height==2&&std::all_of(filled.deep->samples.begin(),filled.deep->samples.end(),[](std::uint16_t v){return v==0;}),"blank layer gets clear canvas pixels");
    check(filled.transform.x==0&&filled.transform.y==0&&filled.transform.width==2&&filled.transform.height==2&&filled.mask&&filled.mask->placement==blank.transform,"blank layer covers the canvas and its mask stays in place");
    for(const char* id:{"folder","text","shape","adjustment","gif","sixteen","outside"})check(get(wide.document,id)==get(d,id),"excluded, unselected and already 16-bit layers are untouched");
    check(!canConvertDepth(wide.document,chosen,BitDepth::Sixteen)&&!convertDepth(wide.document,chosen,BitDepth::Sixteen).changed,"nothing left to widen");

    const auto narrow=convertDepth(wide.document,chosen,BitDepth::Eight);
    check(narrow.action=="Convert to 8-Bit"&&!get(narrow.document,"eight").deep&&!get(narrow.document,"blank").deep&&!get(narrow.document,"sixteen").deep,"every selected deep layer narrows");
    check(get(narrow.document,"eight")==get(d,"eight"),"8 to 16 to 8 restores the layer exactly");
    check(rasterFromDeep(*converted.deep)->rgba()==pixels->rgba(),"dithering promoted pixels returns the same bytes");
    std::vector<std::uint8_t> ramp;for(int a=0;a<256;++a)for(int v=0;v<256;++v){const auto value=std::uint8_t(std::min(v,a));ramp.insert(ramp.end(),{value,value,value,std::uint8_t(a)});}
    const auto every=Raster::fromRgba(256,256,ramp.data(),1024);check(rasterFromDeep(*deepFromRaster(*every))->rgba()==ramp,"every premultiplied 8-bit value survives the round trip");

    History history;std::optional<Document> current=d;history.begin(wide.action,current,chosen.primary);current=wide.document;history.end(current,wide.selection.primary);
    check(history.undoCount()==1,"one history step");const auto undone=history.undo();check(undone&&undone->document==d&&!history.canUndo(),"one undo restores every layer");const auto redone=history.redo();check(redone&&redone->document==wide.document,"one redo converts every layer again");

    auto linked=layer("linked"),placed=layer("placed");linked.mask=solidMask(255);placed.transform={1,0,1,1};placed.mask=solidMask(255);placed.mask->placement=Transform{5,6,2,2};
    const auto masks=document({linked,placed});const auto masked=convertDepth(masks,selected({"linked","placed"}),BitDepth::Sixteen);
    check(get(masked.document,"placed").mask==placed.mask,"a placed mask keeps its placement");
    check(!get(masked.document,"linked").mask->placement||get(masked.document,"linked").transform!=linked.transform,"a mask on a canvas-sized blank layer is not moved");

    auto hdr=layer("hdr");auto samples=makeDeep(2,2,DeepImage::Format::HalfFloat);samples->setPixel(0,0,{2,.5f,0,1});hdr.deep=samples;hdr.raster=rasterFromDeep(*samples);
    const auto bright=document({hdr});
    check(!canConvertDepth(bright,selected({"hdr"}),BitDepth::Sixteen)&&canConvertDepth(bright,selected({"hdr"}),BitDepth::Eight),"HDR only goes to 8 bits here");
    const auto clipped=convertDepth(bright,selected({"hdr"}),BitDepth::Eight);check(!clipped.document.layers[0].deep&&clipped.document.layers[0].raster==hdr.raster,"HDR keeps its clipped 8-bit presentation");

    const auto nested=document({layer("deepest","inner"),layer("outer",{},true),layer("inner","outer",true),layer("loose")});
    check(depthTargets(nested,selected({"outer"}))==std::vector<std::string>({"deepest"}),"a selected folder covers every level below it, whatever the list order");
    check(!depthChoices(nested,selected({"missing"})).sixteen&&depthChoices(nested,selected({"inner"})).sixteen,"choices follow the selected targets");

    std::vector<Layer> blanks;SelectionState all;for(int i=0;i<6;++i){blanks.push_back(layer("blank"+std::to_string(i)));all.ids.push_back(blanks.back().id);}all.primary=all.ids.front();
    const auto large=document(std::move(blanks),10000,10000);const auto before=large;
    check(canConvertDepth(large,all,BitDepth::Sixteen),"oversized request is still offered");
    fails([&]{convertDepth(large,all,BitDepth::Sixteen);},"six 100 MP blank layers exceed the project pixel budget");check(large==before,"rejected conversion changes nothing");
}
}
int main(int argc,char** argv){try{const std::vector<std::pair<std::string,void(*)()>> cases{{"hierarchy",hierarchy},{"grouping",grouping},{"moves_duplicate",movesAndDuplicate},{"depth",depth},{"clipping",clipping},{"bake_delete",bakeAndDelete},{"multi_history",multiDeleteAndHistory},{"copy_graph",copyGraph},{"merge",merging}};bool ran=false;for(const auto& [name,run]:cases)if(argc==1||name==argv[1]){run();std::cout<<"PASS layers."<<name<<'\n';ran=true;}check(ran,"Unknown test case");return 0;}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}

#include "core/Document.h"
#include "layers/LayerOperations.h"
#include "persistence/ProjectStore.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <stdexcept>

using namespace compositor;
namespace {
QJsonArray checks;
int failures{};
struct SourceRequirementFailure:std::runtime_error {using std::runtime_error::runtime_error;};
void guard(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void check(int line,bool value,const char* kind){
    checks.append(QJsonObject{{"source_line",line},{"kind",kind},{"passed",value}});
    if(!value){++failures;std::fprintf(stderr,"FAIL source %s line%d\n",kind,line);}
}
void expect(int line,bool value){check(line,value,"expect");}
void required(int line,bool value){check(line,value,"require");if(!value)throw SourceRequirementFailure("Source requirement failed at "+std::to_string(line));}

std::shared_ptr<const Raster> asset(std::array<uint8_t,4> alpha,uint8_t color=255){
    std::array<uint8_t,16> bytes{};
    for(size_t i=0;i<4;++i){bytes[4*i]=uint8_t(unsigned(color)*alpha[i]/255);bytes[4*i+3]=alpha[i];}
    auto image=Raster::fromRgba(2,2,bytes.data(),8);
    required(40,image&&image->width==2&&image->height==2&&image->tiles.size()==1&&image->tiles[0]);
    return image;
}
std::vector<uint8_t> bytes(const std::shared_ptr<const Raster>& image){
    guard(image&&image->width==2&&image->height==2,"Source reader has the required2x2 backing image");
    auto result=image->rgba();guard(result.size()==16,"Native source reader has16 RGBA bytes");return result;
}
std::array<uint8_t,4> alpha(const std::shared_ptr<const Raster>& image){
    const auto p=bytes(image);return {p[3],p[7],p[11],p[15]};
}
struct Session {
    std::optional<Document> document;
    std::string active;
    History history;
    Session(){
        history.begin("New Canvas",document,active);Document value;value.id=newId();value.width=value.height=2;
        document=std::move(value);history.end(document,active);
    }
    Document& doc(){guard(document.has_value(),"Native session has a document");return *document;}
    layers::SelectionState selected()const{return {{active},active};}
    void insert(std::shared_ptr<const Raster> raster,const char* name="Fixture"){
        guard(bool(raster),"Imported image backing exists");Layer layer;layer.id=newId();layer.name=name;layer.raster=std::move(raster);layer.transform={0,0,2,2};
        history.begin("Import",document,active);doc().layers.push_back(std::move(layer));active=doc().layers.back().id;history.end(document,active);
    }
    bool apply(layers::EditResult result){
        if(!result.changed)return false;
        history.begin(result.action,document,active);document=std::move(result.document);active=std::move(result.selection.primary);history.end(document,active);return true;
    }
    bool link(const std::string& source,const std::string& target){
        if(!layers::canLink(doc(),source,target))return false;
        apply(layers::link(doc(),selected(),source,target));return true;
    }
    void toggle(const std::string& id){apply(layers::toggleClipping(doc(),selected(),id));}
    bool placeBottom(const std::string& id){return apply(layers::place(doc(),selected(),id,{{},{},true}));}
    void restore(std::optional<Snapshot> state){guard(state.has_value(),"Requested source history operation exists");document=std::move(state->document);active=std::move(state->activeLayer);}
    void undo(){restore(history.undo());}
    void redo(){restore(history.redo());}
    Document snapshot(int line){required(line,document.has_value());validateDocument(doc());return doc();}
};
std::shared_ptr<const Raster> render(const Document& document){return SoftwareRenderer().render(document,0,0,2,2);}
std::shared_ptr<const Raster> render(Session& session,int line){return render(session.snapshot(line));}
Session fixture(){
    Session s;s.insert(asset({255,255,255,255}));s.insert(asset({255,0,128,255},0));
    for(auto& layer:s.doc().layers)layer.transform.sampling=Transform::Sampling::Nearest;
    s.doc().layers[1].visible=false;
    expect(56,s.link(s.doc().layers[1].id,s.doc().layers[0].id));return s;
}

void clipping_color(){
    Session s;s.insert(asset({255,128,32,0},0));s.insert(asset({255,255,255,255}));
    required(10,!s.active.empty());const auto id=s.active;s.toggle(id);
    for(auto& layer:s.doc().layers)layer.transform.sampling=Transform::Sampling::Nearest;
    const auto result=render(s,13);expect(14,alpha(result)==std::array<uint8_t,4>{255,128,32,0});
    auto p=bytes(result);
    for(size_t i=0;i<4;++i){expect(19,p[i*4]==p[i*4+3]);expect(20,p[i*4+1]==0&&p[i*4+2]==0);}
    s.doc().layers[1].opacity=.5;
    const auto translucent=render(s,23);expect(24,alpha(translucent)==std::array<uint8_t,4>{255,128,32,0});
    s.doc().layers[1].opacity=1;
    // The source makes an image and thumbnail independently from its white context.
    const auto white=Raster::filled(2,2,{255,255,255,255});required(29,bool(white));
    const auto thumbnail=Raster::filled(2,2,{255,255,255,255});required(29,bool(thumbnail));
    s.insert(white,"White");required(30,!s.active.empty());const auto background=s.active;
    (void)s.placeBottom(background);
    const auto flattened=render(s,32);expect(33,alpha(flattened)==std::array<uint8_t,4>{255,255,255,255});
    p=bytes(flattened);for(size_t i=0;i<4;++i)expect(35,p[i*4]==255);
}
void option_stack(){
    Session s;for(int i=0;i<3;++i)s.insert(asset({255,255,255,255}));
    const std::array<std::string,3> ids{s.doc().layers[0].id,s.doc().layers[1].id,s.doc().layers[2].id};
    expect(63,!layers::canToggleClipping(s.doc(),ids[0])&&layers::canToggleClipping(s.doc(),ids[1]));
    s.toggle(ids[0]);expect(65,s.doc().layers[0].maskSourceId.empty());
    s.toggle(ids[1]);s.toggle(ids[2]);expect(67,s.doc().layers[1].maskSourceId==ids[0]);expect(68,s.doc().layers[2].maskSourceId==ids[0]);
    expect(69,layers::canToggleClipping(s.doc(),ids[2]));s.toggle(ids[2]);
    expect(71,s.doc().layers[2].maskSourceId.empty());expect(72,s.doc().layers[1].maskSourceId==ids[0]);
    s.toggle(ids[2]);s.toggle(ids[1]);expect(74,s.doc().layers[1].maskSourceId.empty()&&s.doc().layers[2].maskSourceId.empty());
    s.undo();expect(75,s.doc().layers[2].maskSourceId==ids[0]);expect(76,s.placeBottom(ids[2]));
    expect(77,s.doc().layers.front().id==ids[2]&&s.doc().layers.front().maskSourceId.empty());
    expect(78,s.doc().layers.back().maskSourceId==ids[0]);s.undo();
    expect(80,s.doc().layers.back().id==ids[2]&&s.doc().layers.back().maskSourceId==ids[0]);
}
void hidden_source(){
    auto s=fixture();auto raster=render(s,84);expect(85,alpha(raster)==std::array<uint8_t,4>{255,0,128,255});
    s.doc().layers[1].opacity=.5;raster=render(s,87);const auto a=alpha(raster);
    expect(89,std::abs(int(a[0])-128)<=1&&a[1]==0&&std::abs(int(a[2])-64)<=1);
    auto gray=std::make_shared<GrayRaster>(GrayRaster{2,2,std::vector<uint8_t>(4,128)});
    required(92,gray&&gray->width==2&&gray->height==2&&gray->pixels.size()==4);
    s.doc().layers[0].mask=Mask{gray};raster=render(s,93);expect(94,alpha(raster)[0]<a[0]);
}
void persistence_bake(){
    auto s=fixture();const auto target=s.doc().layers[0].id,source=s.doc().layers[1].id;
    expect(98,!s.link(target,source));expect(99,!s.link(target,target));
    s.undo();expect(100,s.doc().layers[0].maskSourceId.empty());s.redo();expect(101,s.doc().layers[0].maskSourceId==source);
    const auto snapshot=s.snapshot(102);
    QTemporaryDir directory;guard(directory.isValid(),"Native temporary package directory exists");
    const auto path=std::filesystem::path(directory.filePath(QString::fromUtf8("LiveMask-\xE6\xB5\x8B\xE8\xAF\x95.comp")).toStdWString());
    ProjectStore store(makeWicProjectCodec());store.save(path,snapshot,s.active);const auto loaded=store.load(path);
    expect(107,loaded.document.layers[0].maskSourceId==source);const auto before=render(loaded.document);
    const auto baked=layers::bakeLiveMask(loaded.document,target);required(109,bool(baked));
    // Native erase owns its bake job. Verify the actual replacement is exactly
    // the same raster value observed from the source's loaded-snapshot bake.
    auto erased=layers::erase(s.doc(),{{source},source},layers::DeleteMode::Bake);
    const auto replacement=std::find_if(erased.document.layers.begin(),erased.document.layers.end(),[&](const Layer& l){return l.id==target;});
    guard(replacement!=erased.document.layers.end()&&bytes(replacement->raster)==bytes(baked),"Native delete uses the observed loaded-snapshot bake bytes");
    s.apply(std::move(erased));const auto after=render(s,111);
    expect(112,alpha(before)==alpha(after));expect(113,s.doc().layers[0].maskSourceId.empty());
    s.undo();expect(114,s.doc().layers.size()==2&&s.doc().layers[0].maskSourceId==source);
    auto bad=snapshot;bad.layers[1].maskSourceId=target;bool threw=false;
    try{validateDocument(bad);}catch(const std::exception&){threw=true;}expect(116,threw);
}
void moved_chain(){
    auto s=fixture();s.doc().layers[1].transform.x+=1;auto raster=render(s,121);expect(122,alpha(raster)==std::array<uint8_t,4>{0,255,0,128});
    s.doc().layers[1].transform.x-=1;s.insert(asset({0,255,255,255}));s.doc().layers[2].visible=false;
    expect(126,s.link(s.doc().layers[2].id,s.doc().layers[1].id));raster=render(s,127);expect(128,alpha(raster)==std::array<uint8_t,4>{0,0,128,255});
}
}
int main(int argc,char** argv){
    QCoreApplication application(argc,argv);
    if(argc!=3){std::fprintf(stderr,"Usage: live_mask_source_tests CASE ABSOLUTE_NEW_REPORT.json\n");return 2;}
    const std::string key=argv[1];const auto output=QString::fromLocal8Bit(argv[2]);
    if(!QDir::isAbsolutePath(output)||QFile::exists(output)){std::fprintf(stderr,"Report must be new and absolute\n");return 2;}
    QString status="passed",error;
    try{
        if(key=="clipping_color")clipping_color();else if(key=="option_stack")option_stack();else if(key=="hidden_source")hidden_source();
        else if(key=="persistence_bake")persistence_bake();else if(key=="moved_chain")moved_chain();else throw std::runtime_error("Unknown source invocation");
        if(failures)status="failed";
    }catch(const SourceRequirementFailure& exception){status="failed";error=QString::fromUtf8(exception.what());}
    catch(const std::exception& exception){status="error";error=QString::fromUtf8(exception.what());}
    QFile report(output);if(!report.open(QIODevice::WriteOnly|QIODevice::NewOnly)){std::fprintf(stderr,"Cannot write actual source report\n");return 2;}
    const auto encoded=QJsonDocument(QJsonObject{{"schema","LIVE_MASK_SOURCE_RESULT_V1"},{"case",QString::fromStdString(key)},
        {"status",status},{"error",error},{"source_expect_failures",failures},{"checks",checks},
        {"adapter","Native production graph/render/history/WIC persistence; generated2x2 fixtures; no Mac runtime or image differential claim"}}).toJson(QJsonDocument::Indented);
    if(report.write(encoded)!=encoded.size())return 2;report.close();
    std::printf("%s %s checks=%lld failures=%d\n",status=="passed"?"PASS":"FAIL",key.c_str(),static_cast<long long>(checks.size()),failures);
    return status=="passed"?0:status=="failed"?1:2;
}

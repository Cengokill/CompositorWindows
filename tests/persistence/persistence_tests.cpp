#include "persistence/ProjectStore.h"
#include "imaging/win32_file_path.h"
#include "editing/DocumentGeometry.h"
#include "editing/Shapes.h"
#include "graphics/StackRenderer.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <Windows.h>
#include <winioctl.h>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace compositor;
namespace fs=std::filesystem;
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,"Expected rejection");}
QString q(const fs::path&p){return QString::fromStdWString(p.wstring());}
void write(const fs::path&p,const QByteArray&data){QFile f(q(p));check(f.open(QIODevice::WriteOnly),"Cannot write fixture");check(f.write(data)==data.size(),"Fixture write failed");}
QByteArray read(const fs::path&p){QFile f(q(p));check(f.open(QIODevice::ReadOnly),"Cannot read fixture");return f.readAll();}
QJsonObject json(const fs::path&p){return QJsonDocument::fromJson(read(p)).object();}
void json(const fs::path&p,const QJsonObject&o){write(p,QJsonDocument(o).toJson());}
std::string id(int i){char b[37];std::snprintf(b,sizeof b,"00000000-0000-4000-8000-%012d",i);return b;}
Document blank(){Document d;d.id=id(1);d.width=19;d.height=17;d.resolution=144;Layer l;l.id=id(2);l.name="Paint & sky \xF0\x9F\x8C\xA4";l.transform={-2.25,3.5,17,13,38,true,false,Transform::Sampling::Nearest};d.layers.push_back(l);return d;}
// Test-only codec: an IHDR-shaped header followed by raw bytes. It deliberately does not
// masquerade as production PNG/WIC verification; it exercises persistence independently.
QByteArray header(int w,int h,bool gray){QByteArray a(33,0);const unsigned char sig[]{137,80,78,71,13,10,26,10};for(int i=0;i<8;++i)a[i]=char(sig[i]);a[11]=13;a.replace(12,4,"IHDR");for(int i=0;i<4;++i){a[16+i]=char(uint32_t(w)>>(24-i*8));a[20+i]=char(uint32_t(h)>>(24-i*8));}a[24]=8;a[25]=gray?0:6;return a;}
int sizeAt(const QByteArray&a,int i){return int((uint32_t(uint8_t(a[i]))<<24)|(uint32_t(uint8_t(a[i+1]))<<16)|(uint32_t(uint8_t(a[i+2]))<<8)|uint8_t(a[i+3]));}
ProjectAssetCodec codec(int* reads=nullptr){ProjectAssetCodec c;
 c.readColor=[reads](const fs::path&p){if(reads)++*reads;auto a=read(p);int w=sizeAt(a,16),h=sizeAt(a,20);check(a.size()==33+qsizetype(w)*h*4,"Invalid mock color payload");return Raster::fromRgba(w,h,reinterpret_cast<const uint8_t*>(a.constData()+33),size_t(w)*4);};
 c.readGray=[reads](const fs::path&p){if(reads)++*reads;auto a=read(p);auto r=std::make_shared<GrayRaster>();r->width=sizeAt(a,16);r->height=sizeAt(a,20);check(a.size()==33+qsizetype(r->width)*r->height,"Invalid mock gray payload");r->pixels.assign(a.constData()+33,a.constData()+a.size());return std::shared_ptr<const GrayRaster>(r);};
 c.writeColor=[](const fs::path&p,const Raster&r){auto a=header(r.width,r.height,false);auto b=r.rgba();a.append(reinterpret_cast<const char*>(b.data()),qsizetype(b.size()));write(p,a);};
 c.writeGray=[](const fs::path&p,const GrayRaster&r){auto a=header(r.width,r.height,true);a.append(reinterpret_cast<const char*>(r.pixels.data()),qsizetype(r.pixels.size()));write(p,a);};return c;}
void mutate(const fs::path&p,const std::function<void(QJsonObject&)>&f){auto o=json(p/L"manifest.json");f(o);json(p/L"manifest.json",o);}
void mutateLayer(const fs::path&p,const std::function<void(QJsonObject&)>&f){mutate(p,[&](QJsonObject&o){auto a=o["layers"].toArray();auto l=a[0].toObject();f(l);a[0]=l;o["layers"]=a;});}
fs::path suffix(const fs::path&p,const std::string&s){return p.parent_path()/(p.filename().string()+s);}
void journal(const fs::path&p,const std::string&tx,const std::string&phase,bool original){json(suffix(p,".compositor-save.json"),{{"transaction",QString::fromStdString(tx)},{"target",q(p.filename())},{"stage",q(suffix(p,".stage-"+tx).filename())},{"backup",q(suffix(p,".backup-"+tx).filename())},{"phase",QString::fromStdString(phase)},{"hadOriginal",original}});}
bool junction(const fs::path&link,const fs::path&target){fs::create_directory(link);std::wstring substitute=L"\\??\\"+target.wstring(),printed=target.wstring();struct Buffer{DWORD tag;WORD length,reserved,subOffset,subLength,printOffset,printLength;WCHAR path[2048];}b{};b.tag=IO_REPARSE_TAG_MOUNT_POINT;b.subLength=WORD(substitute.size()*sizeof(WCHAR));b.printOffset=WORD(b.subLength+sizeof(WCHAR));b.printLength=WORD(printed.size()*sizeof(WCHAR));b.length=WORD(8+b.printOffset+b.printLength+sizeof(WCHAR));std::copy(substitute.begin(),substitute.end(),b.path);std::copy(printed.begin(),printed.end(),b.path+substitute.size()+1);auto handle=CreateFileW(link.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);if(handle==INVALID_HANDLE_VALUE){fs::remove(link);return false;}DWORD returned=0;bool ok=DeviceIoControl(handle,FSCTL_SET_REPARSE_POINT,&b,b.length+8,nullptr,0,&returned,nullptr)!=0;CloseHandle(handle);if(!ok)fs::remove(link);return ok;}
int main(int argc,char**argv){QCoreApplication app(argc,argv);try{check(argc==2,"Need case name");std::string test=argv[1];QTemporaryDir temp;check(temp.isValid(),"Temp directory unavailable");fs::path root(temp.path().toStdWString());auto p=root/L"Test.comp";ProjectStore store(codec());auto d=blank();
 if(test=="blank_roundtrip"){d.selection=Selection{std::make_shared<GrayRaster>(GrayRaster{1,1,{0}})};store.save(p,d,id(2));auto result=store.load(p);check(result.readVersion==7&&result.document.id==d.id&&result.activeLayer==id(2),"Identity lost");check(result.document.layers[0].transform==d.layers[0].transform,"Fractional transform lost");check(!result.document.selection&&result.document.layers[0].name==d.layers[0].name,"Session state or Unicode contract broken");auto m=json(p/L"manifest.json");check(m["version"]==7&&m["layers"].toArray()[0].toObject()["transform"].toObject()["origin"].isArray(),"Wrong wire shape");}
 else if(test=="raster_mask_roundtrip"){d.layers[0].raster=Raster::filled(17,13,{16,32,48,64});auto gray=std::make_shared<GrayRaster>(GrayRaster{3,2,{0,64,128,192,254,255}});d.layers[0].mask=Mask{gray,false,false,d.layers[0].transform};d.layers[0].opacity=.625;d.layers[0].blend=Blend::Luminosity;store.save(p,d,id(2));auto r=store.load(p).document.layers[0];check(r.raster->rgba()==d.layers[0].raster->rgba(),"Raster changed");check(r.mask->raster->pixels==gray->pixels&&!r.mask->enabled&&!r.mask->linked&&r.mask->placement==d.layers[0].mask->placement,"Mask contract lost");check(r.opacity==.625&&r.blend==Blend::Luminosity,"Appearance lost");d.layers[0].raster.reset();d.layers[0].mask.reset();store.save(p,d,id(2));check(fs::is_empty(p/L"images"),"Removed assets retained");}
 else if(test=="legacy_version_gates"){for(int version=1;version<=7;++version){store.save(p,d,id(2));mutate(p,[&](auto&o){o["version"]=version;o.remove("resolution");auto a=o["layers"].toArray();auto l=a[0].toObject();for(const char*k:{"opacity","blendMode","isGroup"})l.remove(k);a[0]=l;o["layers"]=a;});auto r=store.load(p);check(r.readVersion==version&&r.document.resolution==72&&r.document.layers[0].opacity==1,"Legacy default wrong");}store.save(p,d,id(2));mutate(p,[](auto&o){o["version"]=2;});mutateLayer(p,[](auto&l){l["opacity"]=.5;});rejects([&]{store.load(p);});store.save(p,d,id(2));mutate(p,[](auto&o){o["version"]=1;});mutateLayer(p,[](auto&l){l["isGroup"]=true;});rejects([&]{store.load(p);});}
 else if(test=="malformed_metadata"){for(const char*k:{"format","version","colorSpace","width","documentID","layers"}){store.save(p,d,id(2));mutate(p,[&](auto&o){o.remove(k);});rejects([&]{store.load(p);});}for(const char*k:{"rotation","flipX","flipY","sampling","origin","size"}){store.save(p,d,id(2));mutateLayer(p,[&](auto&l){auto t=l["transform"].toObject();t.remove(k);l["transform"]=t;});rejects([&]{store.load(p);});}store.save(p,d,id(2));mutateLayer(p,[](auto&l){l["imageFile"]="../../outside.png";});rejects([&]{store.load(p);});store.save(p,d,id(2));mutateLayer(p,[](auto&l){l["name"]="  \n";});rejects([&]{store.load(p);});store.save(p,d,id(2));mutateLayer(p,[](auto&l){l["isVisible"]=1;});rejects([&]{store.load(p);});}
 else if(test=="unsupported_gif_project"){for(int version:{7,17}){store.save(p,d,id(2));mutate(p,[version](auto&o){o["version"]=version;});mutateLayer(p,[](auto&l){l["gif"]=QJsonObject{{"sourceFile","source.gif"},{"selectedFrameIndex",0}};});auto before=read(p/L"manifest.json");rejects([&]{store.load(p);});check(read(p/L"manifest.json")==before,"Rejected GIF project was modified");}store.save(p,d,id(2));mutateLayer(p,[](auto&l){l["gif"]=QJsonValue::Null;});check(store.load(p).document.layers.size()==1,"Null GIF metadata rejected");}
 else if(test=="graph_boundaries"){Document tree=d;tree.layers.clear();for(int i=0;i<64;++i){Layer l;l.id=id(100+i);l.group=true;if(i)l.parentId=id(99+i);tree.layers.push_back(l);}Layer leaf;leaf.id=id(999);leaf.parentId=id(163);tree.layers.push_back(leaf);store.save(p,tree,leaf.id);check(store.load(p).document.layers.size()==65,"Valid depth64 leaf rejected");tree.layers.back().group=true;rejects([&]{store.save(p,tree,leaf.id);});Document chain=d;chain.layers.clear();for(int i=0;i<256;++i){Layer l;l.id=id(100+i);if(i<255)l.maskSourceId=id(101+i);chain.layers.push_back(l);}store.save(p,chain,id(100));check(store.load(p).document.layers.size()==256,"Valid 256-node chain rejected");chain.layers.back().maskSourceId=chain.layers.front().id;rejects([&]{store.save(p,chain,id(100));});chain.layers.back().maskSourceId=id(800);Layer extra;extra.id=id(800);chain.layers.push_back(extra);rejects([&]{store.save(p,chain,id(100));});chain.layers.resize(2);chain.layers[0].maskSourceId=chain.layers[1].id;chain.layers[1].maskSourceId.clear();chain.layers[1].group=true;rejects([&]{store.save(p,chain,id(100));});}
 else if(test=="rollback_faults"){for(auto point:{SaveFaultPoint::AfterStage,SaveFaultPoint::AfterJournal,SaveFaultPoint::AfterBackup,SaveFaultPoint::AfterInstall,SaveFaultPoint::BeforeCleanup}){store.save(p,d,id(2));auto before=read(p/L"manifest.json");auto changed=d;changed.layers[0].name="Edited";ProjectStore faults(codec(),[&](auto actual){if(actual==point)throw std::runtime_error("injected");});rejects([&]{faults.save(p,changed,id(2));});check(read(p/L"manifest.json")==before,"Failed save changed prior project");check(!fs::exists(suffix(p,".compositor-save.json")),"Rollback left journal");for(auto&e:fs::directory_iterator(root))check(e.path()==p,"Rollback leaked staging files");}}
 else if(test=="crash_recovery"){store.save(p,d,id(2));std::string tx=id(700);auto stage=suffix(p,".stage-"+tx),backup=suffix(p,".backup-"+tx);auto changed=d;changed.layers[0].name="Edited";store.save(stage,changed,id(2));write(stage/L".compositor-transaction",QByteArray::fromStdString(tx));journal(p,tx,"backed_up",true);fs::rename(p,backup);store.recover(p);check(store.load(p).document.layers[0].name==d.layers[0].name,"Missing-target recovery did not restore original");store.save(stage,changed,id(2));write(stage/L".compositor-transaction",QByteArray::fromStdString(tx));fs::rename(p,backup);fs::rename(stage,p);journal(p,tx,"installed",true);store.recover(p);check(store.load(p).document.layers[0].name=="Edited"&&!fs::exists(backup),"Installed recovery failed");}
 else if(test=="allocation_preflight"){d.layers[0].raster=Raster::filled(1,1,{0,0,0,0});store.save(p,d,id(2));write(p/L"images"/fs::path(id(2)+".png"),header(30000,4000,false));int reads=0;ProjectStore guarded(codec(&reads));rejects([&]{guarded.load(p);});check(reads==0,"Oversized image reached decoder");}
 else if(test=="hostile_journal"){store.save(p,d,id(2));auto before=read(p/L"manifest.json");journal(p,id(700),"prepared",true);auto f=suffix(p,".compositor-save.json");auto j=json(f);j["stage"]="../unrelated";json(f,j);rejects([&]{store.recover(p);});check(read(p/L"manifest.json")==before&&fs::exists(f),"Unsafe journal mutated project");}
 else if(test=="reparse_rejection"){store.save(p,d,id(2));auto link=root/L"Link.comp";if(!CreateSymbolicLinkW(link.c_str(),p.c_str(),SYMBOLIC_LINK_FLAG_DIRECTORY|0x2)&&!junction(link,p)){std::cout<<"SKIP: symbolic link and junction creation unavailable\n";return 77;}rejects([&]{store.load(link);});fs::remove(link);}
 else if(test=="real_png_exchange"){ProjectStore actual(makeWicProjectCodec());d.layers[0].raster=Raster::filled(17,13,{16,32,48,64});auto gray=std::make_shared<GrayRaster>(GrayRaster{3,2,{0,64,128,192,254,255}});d.layers[0].mask=Mask{gray,false,false,d.layers[0].transform};actual.save(p,d,id(2));auto result=actual.load(p);check(result.document.layers[0].raster->rgba()==d.layers[0].raster->rgba(),"WIC project pixels changed");check(result.document.layers[0].mask->raster->pixels==gray->pixels,"WIC mask coverage changed");auto encoded=read(p/L"images"/fs::path(id(2)+".mask.png"));check(encoded.size()>33&&encoded[24]==8&&encoded[25]==0,"Mask PNG is not gray8");}
 else if(test=="rotated_roundtrip"){
    // A rotated project must reopen with the same placement, guides, and shadow angle, and render identically to the in-memory rotated document. Rotation direction is covered by editing.rotate_canvas_pixels.
    std::vector<uint8_t> pattern(17*13*4);for(int y=0;y<13;++y)for(int x=0;x<17;++x){size_t i=(size_t(y)*17+x)*4;pattern[i]=uint8_t(x*15);pattern[i+1]=uint8_t(y*19);pattern[i+2]=64;pattern[i+3]=255;}
    d.layers[0].raster=Raster::fromRgba(17,13,pattern.data(),17*4);d.guides={{id(9),true,5.5},{id(10),false,4}};d.layers[0].effects.specified=true;d.layers[0].effects.dropShadow.enabled=true;d.layers[0].effects.dropShadow.angle=120;
    auto rotated=editing::rotateCanvas(d,true);store.save(p,rotated,id(2));auto loaded=store.load(p).document;
    check(loaded.width==rotated.width&&loaded.height==rotated.height&&loaded.width==17&&loaded.height==19,"Rotated canvas size lost");check(loaded.guides==rotated.guides,"Rotated guides changed");check(loaded.layers[0].transform==rotated.layers[0].transform,"Rotated transform changed");check(loaded.layers[0].effects.dropShadow.angle==rotated.layers[0].effects.dropShadow.angle,"Rotated shadow angle changed");
    check(graphics::StackRenderer().render(loaded,0,0,loaded.width,loaded.height)->rgba()==graphics::StackRenderer().render(rotated,0,0,rotated.width,rotated.height)->rgba(),"Rendered rotated project changed after reopen");
 }
 else if(test=="guides_roundtrip"){d.guides.push_back({id(9),true,12.5});d.guides.push_back({id(10),false,4});store.save(p,d,id(2));auto loaded=store.load(p);check(loaded.readVersion==8&&loaded.document.guides.size()==2,"Guide save did not round-trip");check(loaded.document.guides[0]==Guide{id(9),true,12.5}&&loaded.document.guides[1]==Guide{id(10),false,4},"Guide orientation or position changed");auto manifest=json(p/L"manifest.json");auto guides=manifest["guides"].toArray();check(manifest["version"]==8&&guides.size()==2&&guides[0].toObject()["orientation"]=="horizontal"&&guides[1].toObject()["orientation"]=="vertical","Guide manifest shape changed");}
  else if(test=="shape_stroke_roundtrip"){editing::ShapeStyle style{editing::ShapeKind::Rectangle,0,0,1,0};style.strokeEnabled=true;style.strokeWidth=3;style.strokeRed=1;style.strokeBlue=.5;Document shaped;shaped.id=id(1);shaped.width=20;shaped.height=20;shaped.resolution=72;auto layer=*editing::createShapeLayer({0,0,8,6},style,"Stroke");layer.id=id(2);shaped.layers.push_back(layer);store.save(p,shaped,id(2));auto loaded=store.load(p);check(loaded.readVersion==14&&loaded.document.layers.size()==1&&editing::decodeShapeStyle(loaded.document.layers[0].shapeJson)==style,"Stroked shape did not round-trip as format v14");auto manifest=json(p/L"manifest.json");check(manifest["version"]==14,"Stroked shape did not write format v14");manifest["version"]=13;json(p/L"manifest.json",manifest);rejects([&]{store.load(p);});}
  else if(test=="shape_smoothing_roundtrip"){
    ProjectStore actual(makeWicProjectCodec());
    d.width=120;
    d.height=100;
    editing::ShapeStyle style{editing::ShapeKind::Rectangle,0,0,1,20};
    const auto ordinary=*editing::createShapeLayer({0,0,100,80},style,"Rounded");
    style.smoothing=true;
    d.layers[0]=*editing::createShapeLayer({0,0,100,80},style,"Smooth");
    const auto active=d.layers[0].id;
    actual.save(p,d,active);
    const auto loaded=actual.load(p);
    check(loaded.readVersion==17&&loaded.activeLayer==active,"Smooth shape did not use format v17");
    auto reopened=loaded.document.layers[0];
    check(editing::decodeShapeStyle(reopened.shapeJson)==style,"Smooth style was lost");
    check(reopened.raster->rgba()==d.layers[0].raster->rgba(),"Reopened shape pixels changed");
    reopened.transform.width=120;
    reopened=editing::redrawShape(reopened);
    check(reopened.raster->rgba()==editing::shapeRaster(style,120,80)->rgba(),"Reopened shape did not resize with smoothing");

    auto legacyDocument=d;
    legacyDocument.layers[0].raster=ordinary.raster;
    legacyDocument.layers[0].shapeJson=ordinary.shapeJson;
    const auto cachedPixels=ordinary.raster->rgba();
    for(int version:{7,14,16}){
        actual.save(p,legacyDocument,active);
        mutate(p,[version](auto& manifest){manifest["version"]=version;});
        mutateLayer(p,[](auto& layer){
            auto shape=layer["shape"].toObject();
            shape["smoothing"]=true;
            layer["shape"]=shape;
        });
        const auto legacy=actual.load(p);
        const auto& layer=legacy.document.layers[0];
        check(legacy.readVersion==version&&legacy.activeLayer==active,"Legacy smooth project identity changed");
        check(editing::decodeShapeStyle(layer.shapeJson)==style,"Legacy smooth style was lost");
        check(layer.raster&&layer.raster->rgba()==cachedPixels,"Legacy cached shape pixels changed on load");

        actual.save(p,legacy.document,legacy.activeLayer);
        const auto migrated=actual.load(p);
        const auto& migratedLayer=migrated.document.layers[0];
        check(migrated.readVersion==17&&migrated.activeLayer==active,"Legacy smooth project did not migrate to format v17");
        check(editing::decodeShapeStyle(migratedLayer.shapeJson)==style,"Migrated smooth style was lost");
        check(migratedLayer.raster&&migratedLayer.raster->rgba()==cachedPixels,"Legacy cached shape pixels changed on resave");
    }
    actual.save(p,d,active);
    mutateLayer(p,[](auto& layer){
        auto shape=layer["shape"].toObject();
        shape["smoothing"]="true";
        layer["shape"]=shape;
    });
    rejects([&]{actual.load(p);});
    actual.save(p,d,active);
    mutateLayer(p,[](auto& layer){
        auto shape=layer["shape"].toObject();
        shape["kind"]="Ellipse";
        layer["shape"]=shape;
    });
    rejects([&]{actual.load(p);});

    d.layers[0]=ordinary;
    actual.save(p,d,ordinary.id);
    auto legacy=actual.load(p);
    check(legacy.readVersion==7&&legacy.document.layers[0].raster->rgba()==ordinary.raster->rgba(),"Ordinary shape changed in the old format");
    mutateLayer(p,[](auto& layer){
        auto shape=layer["shape"].toObject();
        shape["smoothing"]=false;
        layer["shape"]=shape;
    });
    legacy=actual.load(p);
    check(!editing::decodeShapeStyle(legacy.document.layers[0].shapeJson).smoothing,"False enabled smoothing");
    actual.save(p,legacy.document,legacy.activeLayer);
    check(actual.load(p).readVersion==7,"False raised the project version");
  }
  else if(test=="long_image_path"){fs::path deep=root;const auto imageName=fs::path(id(2)+".png");bool longEnough=false;for(int i=0;i<40;++i){if(fs::absolute(deep/L"Saved.comp"/L"images"/imageName).native().size()>=260){longEnough=true;break;}deep/=L"long-path-segment";}check(longEnough,"Could not construct an image path of at least 260 characters");fs::create_directories(imaging::win32FilePath(deep));auto project=deep/L"Saved.comp";ProjectStore actual(makeWicProjectCodec());d.layers[0].raster=Raster::filled(2,2,{9,8,7,255});actual.save(project,d,id(2));auto loaded=actual.load(project);check(loaded.document.layers[0].raster->rgba()==d.layers[0].raster->rgba(),"Long-path WIC pixels changed");int entries=0;for(auto& entry:fs::directory_iterator(imaging::win32FilePath(deep))){++entries;check(entry.path().filename()==L"Saved.comp","Long-path save left a recovery directory");}check(entries==1,"Long-path save left recovery files");std::error_code ignored;fs::remove_all(imaging::win32FilePath(root),ignored);}
  else if(test=="long_journal_interrupt"){
    fs::path deep=root;bool longEnough=false;
    for(int i=0;i<48;++i){if(fs::absolute(deep/L"Saved.comp.compositor-save.json").native().size()>=260){longEnough=true;break;}deep/=L"long-path-segment";}
    check(longEnough,"Could not construct a journal path of at least 260 characters");
    fs::create_directories(imaging::win32FilePath(deep));
    auto project=deep/L"Saved.comp";ProjectStore actual(makeWicProjectCodec());
    d.layers[0].name="Original";d.layers[0].raster=Raster::filled(2,2,{9,8,7,255});
    actual.save(project,d,id(2));
    auto changed=d;changed.layers[0].name="Replacement";changed.layers[0].raster=Raster::filled(2,2,{1,2,3,255});
    bool interrupted=false;std::string hookFailure;
    ProjectStore faults(makeWicProjectCodec(),[&](SaveFaultPoint point){
      if(point!=SaveFaultPoint::BeforeJournalReplace)return;
      interrupted=true;
      try{
        auto journal=fs::absolute(deep/L"Saved.comp.compositor-save.json").lexically_normal();
        QFile file(QString::fromStdWString(imaging::win32FilePath(journal).wstring()));
        check(file.open(QIODevice::ReadOnly),"Previous journal was not readable at the interruption");
        auto bytes=file.readAll();file.close();
        check(!bytes.isEmpty(),"Previous journal was empty at the interruption");
        auto phase=QJsonDocument::fromJson(bytes).object()["phase"].toString();
        check(phase=="prepared","Previous journal phase was no longer prepared");
        int backups=0;
        for(auto& entry:fs::directory_iterator(imaging::win32FilePath(deep))){
          auto name=entry.path().filename().wstring();
          if(name.rfind(L"Saved.comp.backup-",0)!=0)continue;
          ++backups;
          auto recovered=ProjectStore(makeWicProjectCodec()).load(deep/name);
          check(recovered.document.layers[0].name=="Original"&&recovered.document.layers[0].raster->rgba()==d.layers[0].raster->rgba(),"Backup did not still hold the previous project");
        }
        check(backups==1,"Previous project backup was not accessible");
      }catch(const std::exception& error){hookFailure=error.what();throw;}
      throw std::runtime_error("interrupted journal replace");
    });
    rejects([&]{faults.save(project,changed,id(2));});
    check(interrupted,"Long-path journal replace was not interrupted");
    check(hookFailure.empty(),hookFailure.c_str());
    auto restored=actual.load(project);
    check(restored.document.layers[0].name=="Original"&&restored.document.layers[0].raster->rgba()==d.layers[0].raster->rgba(),"Interrupted journal update did not leave the previous project accessible");
    int entries=0;for(auto& entry:fs::directory_iterator(imaging::win32FilePath(deep))){++entries;check(entry.path().filename()==L"Saved.comp","Interrupted save left a recovery directory");}
    check(entries==1,"Interrupted save left recovery files");
    std::error_code ignored;fs::remove_all(imaging::win32FilePath(root),ignored);
  }
  else if(test=="metadata_temp_ownership"){
    fs::path deep=root;bool longEnough=false;
    for(int i=0;i<48;++i){if(fs::absolute(deep/L"Saved.comp.compositor-save.json").native().size()>=260){longEnough=true;break;}deep/=L"long-path-segment";}
    check(longEnough,"Could not construct a journal path of at least 260 characters");
    fs::create_directories(imaging::win32FilePath(deep));
    auto project=deep/L"Saved.comp";ProjectStore actual(makeWicProjectCodec());
    d.layers[0].name="Original";d.layers[0].raster=Raster::filled(2,2,{9,8,7,255});
    actual.save(project,d,id(2));
    auto changed=d;changed.layers[0].name="Replacement";
    const QByteArray foreign("owned-by-someone-else");
    fs::path occupied;
    ProjectStore collision(makeWicProjectCodec(),[&](SaveFaultPoint point){
      if(point!=SaveFaultPoint::BeforeMetadataTempCreate)return;
      auto prepared=metadataTempAwaitingCreate();
      check(prepared!=nullptr,"Metadata temp path was not visible before create");
      occupied=*prepared;
      QFile file(QString::fromStdWString(prepared->wstring()));
      check(file.open(QIODevice::WriteOnly|QIODevice::NewOnly),"Could not plant the colliding metadata temp");
      check(file.write(foreign)==foreign.size()&&file.flush(),"Could not write the colliding metadata temp");
    });
    rejects([&]{collision.save(project,changed,id(2));});
    check(!occupied.empty(),"Colliding metadata temp was not planted");
    QFile occupiedFile(QString::fromStdWString(occupied.wstring()));
    check(occupiedFile.open(QIODevice::ReadOnly),"Colliding metadata temp was removed or could not be read");
    check(occupiedFile.readAll()==foreign,"Colliding metadata temp was truncated or replaced");
    occupiedFile.close();
    check(actual.load(project).document.layers[0].name=="Original","Temp collision changed the previous project");
    check(QFile::remove(QString::fromStdWString(occupied.wstring())),"Could not remove the planted metadata temp");
    fs::path createdTemp;
    ProjectStore interrupted(makeWicProjectCodec(),[&](SaveFaultPoint point){
      if(point!=SaveFaultPoint::BeforeJournalReplace)return;
      int temps=0;
      for(auto& entry:fs::directory_iterator(imaging::win32FilePath(deep))){
        if(entry.path().filename().wstring().find(L".tmp-")==std::wstring::npos)continue;
        ++temps;
        createdTemp=imaging::win32FilePath(deep/entry.path().filename());
        QFile file(QString::fromStdWString(createdTemp.wstring()));
        check(file.open(QIODevice::ReadOnly),"Created metadata temp could not be read");
        check(file.read(1)=="{","Created metadata temp was not the journal written by this save");
      }
      check(temps==1,"Expected one metadata temp created by this save");
      throw std::runtime_error("fail after metadata temp creation");
    });
    rejects([&]{interrupted.save(project,changed,id(2));});
    check(!createdTemp.empty(),"Failure after creation did not observe the metadata temp");
    check(GetFileAttributesW(createdTemp.c_str())==INVALID_FILE_ATTRIBUTES,"Metadata temp created by this save was left behind");
    check(actual.load(project).document.layers[0].name=="Original"&&actual.load(project).document.layers[0].raster->rgba()==d.layers[0].raster->rgba(),"Failure after metadata temp creation changed the previous project");
    int entries=0;for(auto& entry:fs::directory_iterator(imaging::win32FilePath(deep))){++entries;check(entry.path().filename()==L"Saved.comp","Failure after metadata temp creation left a recovery file");}
    check(entries==1,"Failure after metadata temp creation left recovery files");
    std::error_code ignored;fs::remove_all(imaging::win32FilePath(root),ignored);
  }
  else if(test=="unc_share_root"){
    auto shareRoot=imaging::win32FilePath(L"\\\\server\\share");
    auto shareNative=shareRoot.native();
    check(shareNative==L"\\\\?\\UNC\\server\\share\\"||shareNative==L"\\\\?\\UNC\\server\\share","UNC share root was rejected or rewritten");
    check(imaging::win32FilePath(L"\\\\server\\share\\").native()==shareNative,"Trailing slash changed the UNC share root");
    check(imaging::win32FilePath(L"\\\\server\\share\\Saved.comp").native()==L"\\\\?\\UNC\\server\\share\\Saved.comp","UNC project path was rewritten");
    bool escaped=false;try{imaging::win32FilePath(L"\\\\server\\share\\..");}catch(const std::invalid_argument&){escaped=true;}
    check(escaped,"UNC path was allowed to leave its share");
    wchar_t configured[32768]{};
    auto configuredLength=GetEnvironmentVariableW(L"COMPOSITOR_TEST_UNC_SHARE",configured,32768);
    if(configuredLength==0)std::cout<<"SKIP live UNC share: COMPOSITOR_TEST_UNC_SHARE is not set, so the network save is not run\n";
    else{
      check(configuredLength<32768,"COMPOSITOR_TEST_UNC_SHARE is too long");
      std::wstring given(configured,configuredLength);
      while(!given.empty()&&(given.back()==L'\\'||given.back()==L'/'))given.pop_back();
      check(given.starts_with(L"\\\\")&&!given.starts_with(L"\\\\?\\")&&!given.starts_with(L"\\\\.\\"),"COMPOSITOR_TEST_UNC_SHARE must be a UNC directory");
      auto serverEnd=given.find(L'\\',2);
      check(serverEnd!=std::wstring::npos&&serverEnd>2,"COMPOSITOR_TEST_UNC_SHARE must include a server and share");
      auto shareEnd=given.find(L'\\',serverEnd+1);
      check(shareEnd!=serverEnd+1&&(shareEnd==std::wstring::npos||shareEnd>serverEnd+1),"COMPOSITOR_TEST_UNC_SHARE must include a share");
      auto shareOnly=shareEnd==std::wstring::npos?given:given.substr(0,shareEnd);
      check(imaging::win32FilePath(shareOnly).native().starts_with(L"\\\\?\\UNC\\"),"Configured UNC share root was not accepted");
      auto directory=imaging::win32FilePath(given);
      auto attributes=GetFileAttributesW(directory.c_str());
      check(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_DIRECTORY),"COMPOSITOR_TEST_UNC_SHARE is not a readable directory");
      check((attributes&FILE_ATTRIBUTE_REPARSE_POINT)==0,"COMPOSITOR_TEST_UNC_SHARE must not be a reparse point");
      auto identity=std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
      int serial=0;
      struct Work{
        fs::path configured,directory;QByteArray token;
        Work()=default;
        Work(const Work&)=delete;
        Work& operator=(const Work&)=delete;
        Work(Work&& other)noexcept:configured(std::move(other.configured)),directory(std::move(other.directory)),token(std::move(other.token)){other.directory.clear();other.token.clear();}
        ~Work(){removeOwned();}
        static std::wstring key(const fs::path& path){auto text=path.lexically_normal().wstring();while(text.size()>3&&(text.back()==L'\\'||text.back()==L'/'))text.pop_back();return text;}
        bool owned()const{
          try{
            if(directory.empty()||token.isEmpty()||configured.empty())return false;
            if(CompareStringOrdinal(key(directory).c_str(),-1,key(configured).c_str(),-1,TRUE)==CSTR_EQUAL)return false;
            if(CompareStringOrdinal(key(directory.parent_path()).c_str(),-1,key(configured).c_str(),-1,TRUE)!=CSTR_EQUAL)return false;
            auto name=directory.filename().wstring();
            if(name!=L"cmp-unc-"+QString::fromUtf8(token).toStdWString()||name.find_first_of(L"\\/:")!=std::wstring::npos)return false;
            auto extended=imaging::win32FilePath(directory);
            auto attributes=GetFileAttributesW(extended.c_str());
            if(attributes==INVALID_FILE_ATTRIBUTES||!(attributes&FILE_ATTRIBUTE_DIRECTORY)||(attributes&FILE_ATTRIBUTE_REPARSE_POINT))return false;
            QFile marker(QString::fromStdWString(imaging::win32FilePath(directory/L".compositor-unc-test-owner").wstring()));
            if(!marker.open(QIODevice::ReadOnly))return false;
            return marker.read(token.size()+1)==token;
          }catch(...){return false;}
        }
        void removeOwned(){if(!owned())return;std::error_code ignored;try{fs::remove_all(imaging::win32FilePath(directory),ignored);}catch(...){}directory.clear();token.clear();}
      };
      auto makeWork=[&](){
        Work work;work.configured=fs::path(given);work.token=QByteArray::fromStdString(std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())+"-"+std::to_string(++serial));
        auto path=work.configured/(L"cmp-unc-"+QString::fromUtf8(work.token).toStdWString());
        auto extended=imaging::win32FilePath(path);
        check(CreateDirectoryW(extended.c_str(),nullptr),"Could not create the owned UNC test directory");
        auto created=GetFileAttributesW(extended.c_str());
        if(created==INVALID_FILE_ATTRIBUTES||!(created&FILE_ATTRIBUTE_DIRECTORY)||(created&FILE_ATTRIBUTE_REPARSE_POINT)){RemoveDirectoryW(extended.c_str());check(false,"UNC test directory was not created as a real directory");}
        auto markerPath=imaging::win32FilePath(path/L".compositor-unc-test-owner");
        QFile marker(QString::fromStdWString(markerPath.wstring()));
        bool opened=marker.open(QIODevice::WriteOnly|QIODevice::NewOnly);
        bool wrote=opened&&marker.write(work.token)==work.token.size()&&marker.flush();
        marker.close();
        if(!wrote){if(opened)DeleteFileW(markerPath.c_str());RemoveDirectoryW(extended.c_str());check(false,"Could not mark ownership of the UNC test directory");}
        work.directory=path;
        check(work.owned(),"UNC test directory is not owned before use");
        return work;
      };
      struct Sentinel{fs::path path;bool created=false,directory=false,reparse=false;DWORD attributes=0;uint64_t size=0;FILETIME written{};QByteArray bytes;std::vector<std::wstring> children;};
      auto snapshot=[&](Sentinel& sentinel){
        auto extended=imaging::win32FilePath(sentinel.path);
        WIN32_FILE_ATTRIBUTE_DATA info{};
        check(GetFileAttributesExW(extended.c_str(),GetFileExInfoStandard,&info),"Could not inspect a UNC sentinel");
        sentinel.attributes=info.dwFileAttributes;sentinel.reparse=(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)!=0;sentinel.directory=(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
        sentinel.size=(uint64_t(info.nFileSizeHigh)<<32)|info.nFileSizeLow;sentinel.written=info.ftLastWriteTime;sentinel.children.clear();sentinel.bytes.clear();
        if(sentinel.reparse)return;
        if(sentinel.directory){for(auto& entry:fs::directory_iterator(extended))sentinel.children.push_back(entry.path().filename().wstring());std::sort(sentinel.children.begin(),sentinel.children.end());
          auto keep=imaging::win32FilePath(sentinel.path/L"keep.txt");WIN32_FILE_ATTRIBUTE_DATA keepInfo{};
          if(GetFileAttributesExW(keep.c_str(),GetFileExInfoStandard,&keepInfo)&&!(keepInfo.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&!(keepInfo.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)&&((uint64_t(keepInfo.nFileSizeHigh)<<32)|keepInfo.nFileSizeLow)<=65536){QFile file(QString::fromStdWString(keep.wstring()));check(file.open(QIODevice::ReadOnly),"UNC sentinel could not be read");sentinel.bytes=file.readAll();}}
        else if(sentinel.size<=65536){QFile file(QString::fromStdWString(extended.wstring()));check(file.open(QIODevice::ReadOnly),"UNC sentinel could not be read");sentinel.bytes=file.readAll();}
      };
      auto prove=[&](const Sentinel& expected){
        Sentinel actual;actual.path=expected.path;actual.created=expected.created;snapshot(actual);
        check(actual.attributes==expected.attributes&&actual.reparse==expected.reparse&&actual.directory==expected.directory,"UNC sentinel identity changed");
        check(actual.size==expected.size&&CompareFileTime(&actual.written,&expected.written)==0,"UNC sentinel bytes or timestamp changed");
        check(actual.children==expected.children&&actual.bytes==expected.bytes,"UNC sentinel contents changed");
      };
      auto entries=[&](){std::vector<std::wstring> names;for(auto& entry:fs::directory_iterator(imaging::win32FilePath(fs::path(given))))names.push_back(entry.path().filename().wstring());std::sort(names.begin(),names.end());return names;};
      auto before=entries();
      auto plant=[&](const std::wstring& name,bool asDirectory,const QByteArray& content){
        Sentinel sentinel;sentinel.path=fs::path(given)/name;auto extended=imaging::win32FilePath(sentinel.path);auto existing=GetFileAttributesW(extended.c_str());auto missing=GetLastError();
        if(existing==INVALID_FILE_ATTRIBUTES){check(missing==ERROR_FILE_NOT_FOUND||missing==ERROR_PATH_NOT_FOUND,"Could not inspect a UNC sentinel");
          if(asDirectory){check(CreateDirectoryW(extended.c_str(),nullptr),"Could not create a UNC sentinel directory");auto keep=imaging::win32FilePath(sentinel.path/L"keep.txt");QFile file(QString::fromStdWString(keep.wstring()));bool opened=file.open(QIODevice::WriteOnly|QIODevice::NewOnly);bool wrote=opened&&file.write(content)==content.size()&&file.flush();file.close();if(!wrote){if(opened)DeleteFileW(keep.c_str());RemoveDirectoryW(extended.c_str());check(false,"Could not write a UNC sentinel");}}
          else{QFile file(QString::fromStdWString(extended.wstring()));bool opened=file.open(QIODevice::WriteOnly|QIODevice::NewOnly);bool wrote=opened&&file.write(content)==content.size()&&file.flush();file.close();if(!wrote){if(opened)DeleteFileW(extended.c_str());check(false,"Could not write a UNC sentinel");}}
          sentinel.created=true;}
        snapshot(sentinel);
        if(sentinel.created)check(sentinel.bytes==content,"Created UNC sentinel did not keep its contents");
        return sentinel;
      };
      auto saved=plant(L"Saved.comp",true,"preexisting-saved");
      auto nested=plant(L"nested",true,"preexisting-nested");
      auto file=plant(L"cmp-sentinel-"+identity,false,"preexisting-sentinel");
      auto proveAll=[&](){prove(saved);prove(nested);prove(file);auto configured=GetFileAttributesW(imaging::win32FilePath(fs::path(given)).c_str());check(configured!=INVALID_FILE_ATTRIBUTES&&(configured&FILE_ATTRIBUTE_DIRECTORY),"Configured UNC directory was removed");};
      fs::path failedPath;
      try{auto failed=makeWork();failedPath=failed.directory;check(failed.owned(),"UNC test directory is not owned before cleanup");throw std::runtime_error("injected UNC cleanup");}
      catch(const std::runtime_error& error){if(std::string(error.what())!="injected UNC cleanup")throw;}
      check(GetFileAttributesW(imaging::win32FilePath(failedPath).c_str())==INVALID_FILE_ATTRIBUTES,"Failed UNC cleanup removed nothing or removed the wrong directory");
      proveAll();
      {
        auto guard=makeWork();
        std::cout<<"LIVE UNC share: "<<QString::fromStdWString(given).toStdString()<<"\n";
        ProjectStore network(makeWicProjectCodec());
        d.layers[0].raster=Raster::filled(2,2,{4,5,6,255});
        auto project=guard.directory/L"Saved.comp";
        network.save(project,d,id(2));
        check(network.load(project).document.layers[0].raster->rgba()==d.layers[0].raster->rgba(),"UNC share project pixels changed");
        auto nestedParent=guard.directory/L"nested";
        fs::create_directories(imaging::win32FilePath(nestedParent));
        d.layers[0].raster=Raster::filled(2,2,{3,2,1,255});
        auto nestedProject=nestedParent/L"Saved.comp";
        network.save(nestedProject,d,id(2));
        check(network.load(nestedProject).document.layers[0].raster->rgba()==d.layers[0].raster->rgba(),"UNC nested project pixels changed");
        check(guard.owned(),"UNC test directory lost its ownership marker");
      }
      proveAll();
      auto retire=[&](Sentinel& sentinel){if(!sentinel.created)return;prove(sentinel);auto extended=imaging::win32FilePath(sentinel.path);
        if(sentinel.directory){auto keep=imaging::win32FilePath(sentinel.path/L"keep.txt");check(DeleteFileW(keep.c_str()),"Could not remove a UNC sentinel file created by this test");check(RemoveDirectoryW(extended.c_str()),"Could not remove a UNC sentinel directory created by this test");}
        else check(DeleteFileW(extended.c_str()),"Could not remove a UNC sentinel file created by this test");};
      retire(saved);retire(nested);retire(file);
      check(entries()==before,"UNC test changed a pre-existing entry in the configured directory");
    }
  }
  else throw std::runtime_error("Unknown test");std::cout<<"PASS "<<test<<"\n";return 0;
 }catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}

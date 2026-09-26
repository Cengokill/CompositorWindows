#include "SubjectPanel.h"
#include <QDialog>
#include <QPointer>
#include <cmath>

namespace compositor {
namespace {
void maskBudget(const Document& document,const Layer& original){
    uint64_t pixels=uint64_t(original.raster->width)*original.raster->height;
    for(const auto& layer:document.layers)if(layer.id!=original.id&&layer.mask&&layer.mask->raster)pixels+=uint64_t(layer.mask->raster->width)*layer.mask->raster->height;
    if(pixels>100000000)throw std::runtime_error("The background mask exceeds the project mask budget");
    if(uint64_t(original.raster->width)*original.raster->height>imaging::ImportOptions{}.maxWorkingBytes/72)throw std::runtime_error("The background mask exceeds the refinement memory budget");
}
unsigned selectionCoverage(const std::optional<Selection>& selection,const Transform& transform,int x,int y,int width,int height){
    if(!selection)return 255;
    if(!selection->coverage)return 0;
    const auto p=transform.fromUnit({(x+.5)/width,(y+.5)/height});const auto& coverage=*selection->coverage;
    return p.x>=0&&p.y>=0&&p.x<coverage.width&&p.y<coverage.height?coverage.pixel(int(std::floor(p.x)),int(std::floor(p.y))):0;
}
class SubjectPanel final:public ui::EditPanelSession {
    Document originalDocument_;
    Layer original_;
    QPointer<QDialog> dialog_;
    bool closed_{},committing_{};
    void publish(std::shared_ptr<const imaging::RgbaImage> image){
        if(closed_)return;if(host_.valid&&!host_.valid()){cancel();return;}
        if(!image){if(host_.preview)host_.preview({});return;}
        auto shown=originalDocument_;
        auto target=std::find_if(shown.layers.begin(),shown.layers.end(),[this](const Layer& l){return l.id==original_.id;});
        if(target==shown.layers.end())throw std::runtime_error("Subject preview target is missing");
        target->raster=Raster::fromRgba(int(image->width),int(image->height),image->pixels.data(),image->stride);
        if(host_.preview)host_.preview(std::make_shared<const Document>(std::move(shown)));
    }
    std::shared_ptr<const imaging::GrayMask> existingAtApply(){
        if(host_.valid&&!host_.valid())throw std::runtime_error("The background removal target is no longer available");
        auto current=host_.currentDocument?host_.currentDocument():std::optional<Document>{originalDocument_};
        if(!current)throw std::runtime_error("The background removal document is no longer available");
        maskBudget(*current,original_);
        const auto found=std::find_if(current->layers.begin(),current->layers.end(),[this](const Layer& l){return l.id==original_.id;});
        if(found==current->layers.end()||found->raster!=original_.raster||found->transform!=original_.transform)throw std::runtime_error("The background removal source changed");
        if(!found->mask||found->mask->placement||!found->mask->raster||found->mask->raster->width!=original_.raster->width||found->mask->raster->height!=original_.raster->height)return {};
        const auto& mask=*found->mask->raster;
        return std::make_shared<const imaging::GrayMask>(imaging::GrayMask{uint32_t(mask.width),uint32_t(mask.height),size_t(mask.width),mask.pixels});
    }
    void finish(std::optional<imaging::GrayMask> mask){
        if(closed_)return;closed_=true;
        try{if(mask&&(!host_.valid||host_.valid())&&host_.commit){
            auto made=std::make_shared<GrayRaster>();made->width=int(mask->width);made->height=int(mask->height);made->pixels=std::move(mask->pixels);
            auto merge=[made](const Layer& current){auto layer=current;if(layer.mask){layer.mask->raster=made;layer.mask->enabled=true;}else layer.mask=Mask{made};return layer;};
            auto result=originalDocument_;for(auto& layer:result.layers)if(layer.id==original_.id){layer=merge(layer);break;}
            host_.commit({std::move(result),original_.id,true,std::move(merge)});
        }}catch(const std::exception& error){if(host_.error)host_.error(QString::fromUtf8(error.what()));}
        if(host_.preview)host_.preview({});if(host_.closed)host_.closed();host_={};
    }
public:
    SubjectPanel(QWidget* parent,const Document& document,const Layer& original,const std::filesystem::path& model,const imaging::SubjectDialogOptions& options,ui::EditPanelHost host)
        :EditPanelSession(parent,Kind::Filter,false,std::move(host)),originalDocument_(document),original_(original){
        maskBudget(document,original);
        imaging::RgbaImage source{uint32_t(original.raster->width),uint32_t(original.raster->height),size_t(original.raster->width)*4,original.raster->rgba()};
        QPointer<SubjectPanel> owner=this;imaging::SubjectDialogCallbacks callbacks;
        callbacks.preview=[owner](std::shared_ptr<const imaging::RgbaImage> image){if(owner&&!owner->closed_){try{owner->publish(std::move(image));}catch(const std::exception& error){if(owner->host_.error)owner->host_.error(QString::fromUtf8(error.what()));owner->cancel();}}};
        callbacks.existingAtApply=[owner]{if(!owner||owner->closed_)throw std::runtime_error("Background removal is closed");return owner->existingAtApply();};
        callbacks.committingChanged=[owner](bool value){if(owner&&!owner->closed_){owner->committing_=value;if(owner->host_.stateChanged)owner->host_.stateChanged();}};
        callbacks.finished=[owner](std::optional<imaging::GrayMask> result){if(owner)owner->finish(std::move(result));};
        const auto selection=document.selection;const auto transform=original.transform;
        callbacks.processPreview=[selection,transform](imaging::RgbaImage& image,const imaging::RgbaImage& source,const imaging::ImportOptions& limits){
            if(!selection)return;for(uint32_t y=0;y<image.height;++y){imaging::checkCancelled(limits);for(uint32_t x=0;x<image.width;++x){const unsigned coverage=selectionCoverage(selection,transform,int(x),int(y),int(image.width),int(image.height));for(size_t c=0;c<4;++c){auto& out=image.pixels[size_t(y)*image.stride+size_t(x)*4+c];const unsigned before=source.pixels[size_t(y)*source.stride+size_t(x)*4+c];out=uint8_t((out*coverage+before*(255-coverage)+127)/255);}}}
        };
        callbacks.processMask=[selection,transform](imaging::GrayMask& mask,const imaging::GrayMask* existing,const imaging::ImportOptions& limits){
            if(!selection)return;for(uint32_t y=0;y<mask.height;++y){imaging::checkCancelled(limits);for(uint32_t x=0;x<mask.width;++x){const unsigned coverage=selectionCoverage(selection,transform,int(x),int(y),int(mask.width),int(mask.height));const unsigned before=existing?existing->pixels[size_t(y)*existing->stride+x]:255;auto& out=mask.pixels[size_t(y)*mask.stride+x];out=uint8_t((out*coverage+before*(255-coverage)+127)/255);}}
        };
        dialog_=imaging::openSubjectDialog(parent,source,model,options,std::move(callbacks));
        connect(dialog_,&QObject::destroyed,this,[this]{dialog_=nullptr;deleteLater();});
    }
    ~SubjectPanel()override{closed_=true;if(dialog_){disconnect(dialog_,nullptr,this,nullptr);delete dialog_;}}
    QDialog* panel()const override{return dialog_;}
    bool committing()const override{return committing_;}
    void cancel()override{if(!closed_&&!committing_&&dialog_)dialog_->reject();}
};
}
ui::EditPanelSession* openSubjectPanel(QWidget* parent,const Document& document,const Layer& layer,const std::filesystem::path& model,const imaging::SubjectDialogOptions& options,ui::EditPanelHost host){return new SubjectPanel(parent,document,layer,model,options,std::move(host));}
}

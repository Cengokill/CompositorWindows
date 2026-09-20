#include "heif_codec.h"
#include "wic_codec.h"
#include <libheif/heif.h>
#include <fstream>
#include <memory>
#include <mutex>
namespace compositor::imaging {
namespace {
void ok(heif_error e){if(e.code!=heif_error_Ok)throw std::runtime_error(e.message?e.message:"HEIC decoding failed");}
int cancel(void* p){auto* options=static_cast<const ImportOptions*>(p);return options->cancelled&&options->cancelled()?1:0;}
}
DecodedImage HeifCodec::decode(const std::filesystem::path& path,const ImportOptions& options){
    checkCancelled(options);static std::once_flag initialized;std::call_once(initialized,[]{ok(heif_init(nullptr));});
    auto size=std::filesystem::file_size(path);if(size<12||size>options.maxWorkingBytes/3)throw std::runtime_error("HEIC file exceeds encoded input budget");
    std::ifstream stream(path,std::ios::binary);std::vector<std::uint8_t> input(static_cast<std::size_t>(size));if(!stream.read(reinterpret_cast<char*>(input.data()),static_cast<std::streamsize>(size)))throw std::runtime_error("Cannot read HEIC file");
    if(!heif_have_decoder_for_format(heif_compression_HEVC))throw std::runtime_error("Bundled HEVC decoder is unavailable");
    std::unique_ptr<heif_context,decltype(&heif_context_free)> context(heif_context_alloc(),heif_context_free);if(!context)throw std::bad_alloc();
    auto limits=*heif_context_get_security_limits(context.get());limits.max_image_size_pixels=options.remainingPixels;limits.max_memory_block_size=options.maxWorkingBytes/2;limits.max_total_memory=options.maxWorkingBytes/2;limits.max_color_profile_size=16*1024*1024;ok(heif_context_set_security_limits(context.get(),&limits));heif_context_set_maximum_image_size_limit(context.get(),int(options.maxSide));heif_context_set_max_decoding_threads(context.get(),2);
    ok(heif_context_read_from_memory_without_copy(context.get(),input.data(),input.size(),nullptr));heif_image_handle* raw=nullptr;ok(heif_context_get_primary_image_handle(context.get(),&raw));std::unique_ptr<heif_image_handle,decltype(&heif_image_handle_release)> handle(raw,heif_image_handle_release);
    const auto w=heif_image_handle_get_width(handle.get()),h=heif_image_handle_get_height(handle.get());const auto bytes=checkedBytes(w,h,4,options);if(bytes>options.maxWorkingBytes/4)throw std::runtime_error("HEIC transient pixel budget exceeded");
    std::unique_ptr<heif_decoding_options,decltype(&heif_decoding_options_free)> opt(heif_decoding_options_alloc(),heif_decoding_options_free);if(!opt)throw std::bad_alloc();opt->strict_decoding=1;opt->ignore_transformations=0;opt->convert_hdr_to_8bit=1;opt->decoder_id="libde265";opt->cancel_decoding=cancel;opt->progress_user_data=const_cast<ImportOptions*>(&options);opt->num_codec_threads=4;
    auto profileBytes=heif_image_handle_get_raw_color_profile_size(handle.get());std::vector<std::uint8_t> icc;
    if(profileBytes){if(profileBytes>16*1024*1024)throw std::runtime_error("HEIC ICC profile too large");icc.resize(profileBytes);ok(heif_image_handle_get_raw_color_profile(handle.get(),icc.data()));opt->output_image_nclx_profile_passthrough=1;}
    heif_image* decoded=nullptr;ok(heif_decode_image(handle.get(),&decoded,heif_colorspace_RGB,heif_chroma_interleaved_RGBA,opt.get()));std::unique_ptr<heif_image,decltype(&heif_image_release)> image(decoded,heif_image_release);
    const auto dw=heif_image_get_width(image.get(),heif_channel_interleaved),dh=heif_image_get_height(image.get(),heif_channel_interleaved);checkedBytes(dw,dh,4,options);int stride=0;auto* p=heif_image_get_plane_readonly(image.get(),heif_channel_interleaved,&stride);if(!p||stride<dw*4)throw std::runtime_error("Invalid decoded HEIC stride");
    DecodedImage out;out.image={std::uint32_t(dw),std::uint32_t(dh),std::size_t(dw)*4,std::vector<std::uint8_t>(std::size_t(dw)*dh*4)};out.metadata.decoder=std::string("libheif ")+heif_get_version()+" / libde265";
    for(int y=0;y<dh;++y){checkCancelled(options);std::copy_n(p+std::size_t(y)*stride,dw*4,out.image.pixels.data()+std::size_t(y)*dw*4);}
    const bool already=heif_image_is_premultiplied_alpha(image.get())!=0;
    if(already)for(std::size_t i=0;i<out.image.pixels.size();i+=4)for(int c=0;c<3;++c)out.image.pixels[i+c]=out.image.pixels[i+3]?std::uint8_t(std::min(255U,(unsigned(out.image.pixels[i+c])*255+out.image.pixels[i+3]/2)/out.image.pixels[i+3])):0;
    if(!icc.empty()){WicCodec::normalizeStraightRgba(out.image.pixels,dw,dh,icc);out.metadata.profileStatus=ProfileStatus::ConvertedEmbeddedProfile;}
    else {heif_color_profile_nclx* nclx=nullptr;auto status=heif_image_handle_get_nclx_color_profile(handle.get(),&nclx);if(status.code==heif_error_Ok&&nclx){out.metadata.profileStatus=ProfileStatus::ConvertedEmbeddedProfile;heif_nclx_color_profile_free(nclx);}else out.metadata.profileStatus=ProfileStatus::AssumedSrgbNoProfile;}
    for(std::size_t i=0;i<out.image.pixels.size();i+=4)for(int c=0;c<3;++c)out.image.pixels[i+c]=std::uint8_t((unsigned(out.image.pixels[i+c])*out.image.pixels[i+3]+127)/255);
    validate(out.image);return out;
}
}

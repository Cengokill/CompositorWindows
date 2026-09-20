#include "heif_codec.h"
#include "wic_codec.h"
#include <iostream>
#include <fstream>
using namespace compositor::imaging;
int main(int argc,char**argv){try{if(argc!=3)throw std::runtime_error("Usage: heif_checks source-heic output-png");auto decoded=HeifCodec::decode(argv[1]);WicCodec::encode(argv[2],decoded.image);std::cout<<decoded.metadata.decoder<<" "<<decoded.image.width<<"x"<<decoded.image.height<<" premultiplied RGBA8, profile status "<<int(decoded.metadata.profileStatus)<<'\n';return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}

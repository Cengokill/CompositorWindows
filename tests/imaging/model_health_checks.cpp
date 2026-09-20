#include "onnx_subject_provider.h"
#include <iostream>
using namespace compositor::imaging;
int wmain(int argc,wchar_t**argv){try{
    if(argc!=2)throw std::runtime_error("Usage: model_health_checks model.onnx");
    OnnxSubjectProvider provider(argv[1]);provider.healthCheck();
    bool rejected=false;
    try{provider.infer(RgbaImage{2,2,8,std::vector<std::uint8_t>(16,255)});}
    catch(const std::runtime_error& e){if(std::string(e.what())=="No foreground subject was detected")rejected=true;else throw;}
    if(!rejected)throw std::runtime_error("White negative input unexpectedly returned a semantic mask");
    std::cout<<"PASS real CPU health graph on white input; semantic inference still rejects absent subject\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}

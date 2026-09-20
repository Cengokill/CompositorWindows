#pragma once
#include "subject_matte.h"
#include <filesystem>
namespace compositor::imaging {
class OnnxSubjectProvider final:public ISubjectMaskProvider {
    struct Impl;std::unique_ptr<Impl> impl_;
public:
    // Only this inspected conversion is accepted. Loading and execution are entirely local.
    static constexpr const char* modelSha256="c0faf38f5504f2239f1e6481ce4ac166b17435b38ea35e480d811a47bc1aba80";
    explicit OnnxSubjectProvider(const std::filesystem::path& modelPath);
    ~OnnxSubjectProvider();
    GrayMask infer(const RgbaImage&,const ImportOptions& = {}) override;
};
}

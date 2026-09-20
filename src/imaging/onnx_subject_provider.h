#pragma once
#include "subject_matte.h"
#include <filesystem>
namespace compositor::imaging {
class OnnxSubjectProvider final:public ISubjectMaskProvider {
    struct Impl;std::unique_ptr<Impl> impl_;
    GrayMask inferImpl(const RgbaImage&,const ImportOptions&,bool requireSubject);
public:
    // Only this inspected conversion is accepted. Loading and execution are entirely local.
    static constexpr const char* modelSha256="c0faf38f5504f2239f1e6481ce4ac166b17435b38ea35e480d811a47bc1aba80";
    explicit OnnxSubjectProvider(const std::filesystem::path& modelPath);
    ~OnnxSubjectProvider();
    GrayMask infer(const RgbaImage&,const ImportOptions& = {}) override;
    // Executes the real CPU graph and validates finite output without requiring a semantic subject.
    void healthCheck(const ImportOptions& = {});
};
}

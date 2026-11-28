#pragma once
#include <filesystem>
#include <string>

namespace compositor::imaging {
// Resolves the ONNX Runtime API table once, before any Ort wrapper is used.
void initializeOnnxRuntime();
// Lowercase SHA-256 of a local model file; `missing` is the error for an unreadable file.
std::string modelFileSha256(const std::filesystem::path&,const char* missing);
}

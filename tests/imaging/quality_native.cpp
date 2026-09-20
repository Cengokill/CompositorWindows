#include "onnx_subject_provider.h"
#include "wic_codec.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>

using namespace compositor::imaging;
using Clock = std::chrono::steady_clock;
static double elapsed(Clock::time_point start) { return std::chrono::duration<double>(Clock::now() - start).count(); }

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc != 4) throw std::runtime_error("Usage: quality_native model.onnx input.png output-directory");
        const std::filesystem::path output(argv[3]);
        if (!std::filesystem::is_directory(output)) throw std::runtime_error("Output directory must already exist");
        const auto decoded = WicCodec::decode(argv[2]);
        const auto loadStart = Clock::now();
        OnnxSubjectProvider provider(argv[1]);
        const auto loadSeconds = elapsed(loadStart);
        const auto inferStart = Clock::now();
        GrayMask base;
        bool noSubject = false;
        try { base = provider.infer(decoded.image); }
        catch (const std::runtime_error& error) {
            if (std::string(error.what()) != "No foreground subject was detected") throw;
            noSubject = true;
        }
        const auto inferenceSeconds = elapsed(inferStart);
        double basicSeconds = 0, advancedSeconds = 0;
        if (!noSubject) {
            auto start = Clock::now();
            const auto basic = refineSubjectMask(base, decoded.image, MatteSettings{}, false);
            basicSeconds = elapsed(start);
            MatteSettings settings; settings.advanced = true;
            start = Clock::now();
            const auto advanced = refineSubjectMask(base, decoded.image, settings, false);
            advancedSeconds = elapsed(start);
            WicCodec::encodeProjectGrayPng(output / L"basic-mask.png", basic);
            WicCodec::encodeProjectGrayPng(output / L"advanced-mask.png", advanced);
            WicCodec::encode(output / L"basic-cutout.png", applySubjectMask(decoded.image, basic));
            WicCodec::encode(output / L"advanced-cutout.png", applySubjectMask(decoded.image, advanced));
        }
        PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
        if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)))
            throw std::runtime_error("Process memory measurement failed");
        std::ofstream report(output / L"native.json", std::ios::binary);
        if (!report) throw std::runtime_error("Cannot write native report");
        report << std::setprecision(9) << "{\n  \"status\": \"" << (noSubject ? "no_subject" : "produced_masks")
               << "\",\n  \"model_load_seconds\": " << loadSeconds << ",\n  \"inference_seconds\": " << inferenceSeconds
               << ",\n  \"basic_refine_seconds\": " << basicSeconds << ",\n  \"advanced_refine_seconds\": " << advancedSeconds
               << ",\n  \"peak_working_set_bytes\": " << memory.PeakWorkingSetSize
               << ",\n  \"private_committed_bytes_at_end\": " << memory.PrivateUsage << "\n}\n";
        report.close(); if (!report) throw std::runtime_error("Native report write failed");
        std::cout << (noSubject ? "no_subject" : "produced_masks") << "; inference seconds " << inferenceSeconds << '\n';
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

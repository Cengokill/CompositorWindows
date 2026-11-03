#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace compositor {
// Profiles are immutable and shared by document/history/export snapshots.
struct CmykProfile {
    std::string name, sha256;
    std::vector<std::uint8_t> bytes;
};
using CmykProfilePtr = std::shared_ptr<const CmykProfile>;
}

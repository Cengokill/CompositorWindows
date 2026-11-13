#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>

namespace compositor::graphics {
inline constexpr uint32_t parallelBatchMaxWorkers=16;
// Synchronous independent jobs on one private native pool shared by callers.
// 0 selects min(logical CPUs,16);1 runs on the caller. The callback and everything
// it refers to must remain valid and safe for concurrent access until return.
// First exception propagates only after all callbacks stop. Results may be
// partial on failure and must be discarded by the caller. Nested calls made by
// a worker targeting its own pool run serially, preventing a recursive wait.
void runParallelBatch(size_t count,uint32_t workerLimit,const std::function<void(size_t)>& callback);
// Viewport shading can wait on the reduction cache, which in turn submits
// work to the general graphics pool. Keep those waiters off that pool.
void runParallelRenderBatch(size_t count,const std::function<void(size_t)>& callback);
}

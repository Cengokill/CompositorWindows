#include "core/Document.h"
#include <cstdlib>
#include <cstdio>
#include <malloc.h>
#include <new>
#include <stdexcept>
#include <string_view>
#include <utility>

// Fail real allocations in the linked production History, without adding test
// hooks to it. Keep failure armed during cancel + document restoration.
namespace fault {
thread_local long remaining = -1;
thread_local size_t attempts = 0;
void allocation() {
    if (remaining < 0) return;
    ++attempts;
    if (remaining == 0) throw std::bad_alloc();
    --remaining;
}
void arm(long index) { attempts = 0; remaining = index; }
void disarm() { remaining = -1; }
}
void* operator new(size_t size) { fault::allocation(); if (auto* p = std::malloc(size ? size : 1)) return p; throw std::bad_alloc(); }
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }
void* operator new(size_t size, std::align_val_t alignment) { fault::allocation(); if (auto* p = _aligned_malloc(size ? size : 1, size_t(alignment))) return p; throw std::bad_alloc(); }
void* operator new[](size_t size, std::align_val_t alignment) { return ::operator new(size, alignment); }
void operator delete(void* p, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete(void* p, size_t, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete[](void* p, size_t, std::align_val_t) noexcept { _aligned_free(p); }

using namespace compositor;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Fixture {
    History history;
    std::optional<Document> document = Document{};
    std::string active = std::string(64, 'a');
    Fixture() {
        document->id = std::string(64, 'd');
        Layer layer; layer.id = active; layer.name = std::string(64, 'n');
        layer.raster = Raster::filled(2, 2, {25, 50, 75, 255});
        layer.transform = {0, 0, 2, 2};
        auto mask = std::make_shared<GrayRaster>(); mask->width = 2; mask->height = 2; mask->pixels = {255, 128, 64, 0};
        layer.mask = Mask{mask}; document->layers = {layer};
        edit(801); edit(802);
        auto snapshot = history.undo(); restore(std::move(*snapshot));
        history.markSaved();
    }
    void restore(Snapshot snapshot) noexcept { document = std::move(snapshot.document); active = std::move(snapshot.activeLayer); }
    void edit(int width) { history.begin(std::string(64, 'e'), document, active); document->width = width; history.end(document, active); }
};
void baseline(const Fixture& f) {
    require(f.history.undoCount() == 1 && f.history.canUndo() && f.history.canRedo(), "history stacks changed after failed operation");
    require(!f.history.modified() && f.document->width == 801, "saved revision or live document changed after failed operation");
}

void beginFailure() {
    size_t failures = 0;
    for (long index = 0; index < 4096; ++index) {
        Fixture f; bool threw = false;
        try { fault::arm(index); f.history.begin(std::string(64, 'b'), f.document, f.active); }
        catch (const std::bad_alloc&) { threw = true; }
        fault::disarm();
        if (threw) {
            ++failures; baseline(f); require(!f.history.cancel(), "failed begin left a pending transaction");
            f.edit(803); require(f.history.undoCount() == 2 && !f.history.canRedo(), "begin could not be retried");
        } else {
            auto snapshot = f.history.cancel(); require(snapshot && snapshot->document == f.document, "successful begin did not retain snapshot");
            require(failures > 0, "begin allocation injection was ineffective");
            std::printf("begin allocation_points=%zu passed\n", failures); return;
        }
    }
    throw std::runtime_error("begin allocation sweep did not terminate");
}
void endFailure(bool nested, bool trim) {
    size_t failures = 0;
    for (long index = 0; index < 4096; ++index) {
        Fixture f; const auto before = f.document;
        f.history.begin(std::string(64, 'c'), f.document, f.active);
        if (nested) { f.history.begin("Nested", f.document, f.active); f.history.end(f.document, f.active); }
        if (trim) { f.history.byteLimit = 0; f.document->layers.clear(); f.active.clear(); }
        else f.document->width = 803;
        bool threw = false;
        try { fault::arm(index); f.history.end(f.document, f.active); }
        catch (const std::bad_alloc&) { threw = true; }
        if (threw) {
            // Cancellation must work even if EVERY subsequent allocation fails.
            std::optional<Snapshot> rollback;
            try { fault::arm(0); rollback = f.history.cancel(); if (rollback) f.restore(std::move(*rollback)); }
            catch (...) { fault::disarm(); throw std::runtime_error("rollback allocated after failed end"); }
            fault::disarm(); ++failures;
            require(rollback.has_value(), "failed end lost its rollback snapshot");
            require(f.document == before, "failed end changed document metadata or immutable assets");
            baseline(f); f.history.byteLimit = 256 * 1024 * 1024;
            f.edit(804); require(f.history.undoCount() == 2 && f.history.modified(), "end retry did not commit exactly once");
            auto undo = f.history.undo(); f.restore(std::move(*undo)); require(!f.history.modified(), "failed end advanced the saved revision");
        } else {
            fault::disarm();
            require(f.history.modified() && !f.history.canRedo(), "successful end did not publish revision and clear redo");
            require(f.history.undoCount() == (trim ? 0u : 2u), "successful trim or end has wrong history length");
            require(failures > 0, "end allocation injection was ineffective");
            std::printf("end nested=%d trim=%d allocation_points=%zu passed\n", nested, trim, failures); return;
        }
    }
    fault::disarm(); throw std::runtime_error("end allocation sweep did not terminate");
}
void traversalFailure(bool redo) {
    size_t failures = 0;
    for (long index = 0; index < 4096; ++index) {
        Fixture f; bool threw = false; std::optional<Snapshot> result;
        try { fault::arm(index); result = redo ? f.history.redo() : f.history.undo(); }
        catch (const std::bad_alloc&) { threw = true; }
        fault::disarm();
        if (threw) {
            ++failures; baseline(f);
            result = redo ? f.history.redo() : f.history.undo();
            require(result && result->document->width == (redo ? 802 : 800), "history retry returned wrong snapshot");
            f.restore(std::move(*result));
            auto inverse = redo ? f.history.undo() : f.history.redo(); f.restore(std::move(*inverse)); baseline(f);
        } else {
            require(result && result->document->width == (redo ? 802 : 800), "history traversal returned wrong snapshot");
            require(f.history.modified(), "successful traversal did not update revision");
            require(failures > 0, "traversal allocation injection was ineffective");
            std::printf("%s allocation_points=%zu passed\n", redo ? "redo" : "undo", failures); return;
        }
    }
    throw std::runtime_error("traversal allocation sweep did not terminate");
}
void cancelWithoutAllocation() {
    Fixture f; const auto before = f.document;
    f.history.begin("Cancel", f.document, f.active); f.document->layers.clear(); f.active.clear();
    std::optional<Snapshot> rollback;
    try { fault::arm(0); rollback = f.history.cancel(); if (rollback) f.restore(std::move(*rollback)); }
    catch (...) { fault::disarm(); throw std::runtime_error("cancel allocated"); }
    const auto attempted = fault::attempts; fault::disarm();
    require(attempted == 0 && rollback && f.document == before, "cancel did not restore without allocation"); baseline(f);
    std::printf("cancel allocation_attempts=%zu passed\n", attempted);
}
void noopWithoutAllocation() {
    Fixture f; f.history.begin("No-op", f.document, f.active);
    try { fault::arm(0); f.history.end(f.document, f.active); }
    catch (...) { fault::disarm(); throw std::runtime_error("no-op end allocated"); }
    const auto attempted = fault::attempts; fault::disarm(); baseline(f);
    require(attempted == 0, "no-op attempted allocation"); std::puts("noop allocation_attempts=0 passed");
}
int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("expected one case name");
        const std::string_view name = argv[1];
        if (name == "begin") beginFailure();
        else if (name == "end") endFailure(false, false);
        else if (name == "nested_end") endFailure(true, false);
        else if (name == "trim_end") endFailure(false, true);
        else if (name == "undo") traversalFailure(false);
        else if (name == "redo") traversalFailure(true);
        else if (name == "cancel") cancelWithoutAllocation();
        else if (name == "noop") noopWithoutAllocation();
        else throw std::runtime_error("unknown history allocation case");
        return 0;
    } catch (const std::exception& e) { fault::disarm(); std::fprintf(stderr, "FAIL: %s\n", e.what()); return 1; }
}

#pragma once
#include "core/Document.h"
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace compositor::layers {
struct SelectionState {std::vector<std::string> ids;std::string primary;};
struct EditResult {Document document;SelectionState selection;bool changed{};std::string action;};
struct Entry {std::string id;int depth{};bool visible{};};
struct Placement {std::string parent,above;bool atBottom{};};
struct Limits {std::uint64_t maxWorkingBytes{1200000000};std::function<bool()> cancelled;};
SelectionState normalizeSelection(const Document&,SelectionState);
std::vector<Entry> entries(const Document&,bool topFirst=false,const std::unordered_set<std::string>& collapsed={});
std::unordered_set<std::string> descendants(const Document&,const std::string&);
EditResult addGroup(const Document&,SelectionState);
EditResult group(const Document&,SelectionState);
bool canPlace(const Document&,const std::string& id,const std::string& parent);
EditResult place(const Document&,SelectionState,const std::string& id,const Placement&);
EditResult moveOut(const Document&,SelectionState);
EditResult ungroup(const Document&,SelectionState);
bool canMoveSibling(const Document&,const std::string&,int offset);
EditResult moveSibling(const Document&,SelectionState,int offset);
// This legacy list operation moves flat top-first offsets, as EditorSession does.
EditResult reorderTopFirst(const Document&,SelectionState,std::vector<std::size_t> offsets,std::size_t destination);
// Active/option-drag duplication rejects folders, shares immutable rasters/masks.
EditResult duplicateLayer(const Document&,SelectionState,const std::string&,std::optional<Placement> drop={});
struct CopyResult {EditResult edit;std::unordered_map<std::string,std::string> ids;};
// Cross-project copy carries a folder subtree, remaps IDs, and bakes dependencies
// outside the copy. Point defaults to destination canvas center.
CopyResult copySubtree(const Document& source,const std::string& id,const Document& destination,std::optional<Point> point={},const Limits& = {});
bool canLink(const Document&,const std::string& source,const std::string& target);
bool canToggleClipping(const Document&,const std::string&);
EditResult link(const Document&,SelectionState,const std::string& source,const std::string& target);
EditResult releaseClipping(const Document&,SelectionState,const std::string& target);
EditResult toggleClipping(const Document&,SelectionState,const std::string& target);
// 8/16-bit conversion covers the selected layers and the contents of selected folders. Folders,
// adjustments, text, shapes and animated GIF layers are never targets.
enum class BitDepth {Eight,Sixteen};
std::vector<std::string> depthTargets(const Document&,SelectionState);
// Which directions would change at least one target.
struct DepthChoices {bool eight{},sixteen{};};
DepthChoices depthChoices(const Document&,SelectionState);
// True when at least one target would change. A layer already holding deep pixels, HDR included, is left to
// Convert HDR to 16-Bit in the 16-bit direction.
bool canConvertDepth(const Document&,SelectionState,BitDepth);
// One edit. A blank layer gets clear 16-bit pixels over the canvas; the project pixel budget is checked first.
EditResult convertDepth(const Document&,SelectionState,BitDepth);
// Menu clipping and visibility commands act on the selection plus the active layer as one edit. Creating clips each
// unclipped target to the layer below it (or to that layer's base) and is all-or-nothing; it releases when every
// target is clipped. Releasing also frees the layers clipped directly above each target to the same base.
bool releasesSelectedClipping(const Document&,SelectionState);
bool canToggleSelectedClipping(const Document&,SelectionState);
EditResult toggleSelectedClipping(const Document&,SelectionState);
EditResult releaseSelectedClipping(const Document&,SelectionState);
// The active layer decides: visible hides every target, hidden shows every target. Folders are included.
EditResult toggleSelectedVisibility(const Document&,SelectionState);
std::string selectedVisibilityTitle(const Document&,SelectionState);
struct DeletePlan {std::vector<std::string> roots,dependents;std::unordered_set<std::string> removed;};
DeletePlan deletionPlan(const Document&,SelectionState);
enum class DeleteMode {Cancel,Bake,RemoveLinks};
std::shared_ptr<const Raster> bakeLiveMask(const Document&,const std::string& target,const Limits& = {},std::shared_ptr<const DeepImage>* deep=nullptr);
EditResult erase(const Document&,SelectionState,DeleteMode,const Limits& = {});
struct MergePlan {std::vector<std::string> ids;std::unordered_set<std::string> removed;std::string name,parent,anchor,action;};
std::optional<MergePlan> mergePlan(const Document&,SelectionState);
EditResult merge(const Document&,SelectionState,const IRasterBackend&,const Limits& = {});
}

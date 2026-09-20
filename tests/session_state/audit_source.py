"""Inventory every stored EditorSession property at the pinned baseline."""
from pathlib import Path
import hashlib
import json
import re

here = Path(__file__).resolve().parent
root = here.parents[2]
source = root / "upstream/Compositor/Document/EditorSession.swift"
groups = {
    "implemented_tool_bundle": "showsSampleRing tool lastBrushPoint locksTransformRatio transformAutoSelect showsTransformControls brushSettings spotHealingMode blurMode brushMode parkedBrushTips maskPaintWhite backgroundColor gradientSettings marqueeKind shapeKind shapeCornerRadius selectionModeChoice selectionAntialiased wandSettings showsPixelGrid",
    "already_project_owned": "document projectURL viewport collapsedGroupIDs cloneSource cloneSettings cloneOffset isMaskSelected selectedLayerIDs activeLayerID history",
    "choice_not_yet_wired": "cropRatioChoice lassoKind filterSettings selectionExpandAmount selectionContractAmount",
    "operation_or_pending_edit_excluded": "adjustmentOriginal adjustmentEditingID cropRect transformEdit transformDuplicate gradientEdit lassoDraft shapeDraft heldSelectionMode selectionMoveOrigin pixelMove levels hueSaturation filterEdit hueSampleMode hueTargeting hueTargetDrag pendingOpacityDigit colorPicker brushStroke warpStroke opacityEditLayerID blendPreview renamingLayerID",
    "derived_runtime_or_cache_excluded": "canvasFocusRequest isProjectBusy showsBusy busyIndicatorTask projectWaiters fileRequestWaiters cropError distortPreviewCache snapGuides maskDistortPreviewCache shapeTransformPreviewCache hueSaturationTask hueSaturationPending brushError brushRevision showsNewDocument showsImporter isImporting importError refreshCanvasPreview pendingImports",
    "workflow_or_clipboard_outside_bundle": "skipsInitialClipboardCanvasSize pixelClipboard",
}
lookup = {name: category for category, names in groups.items() for name in names.split()}
entries = []
for line, value in enumerate(source.read_text(encoding="utf-8").splitlines(), 1):
    if line < 95:
        continue
    found = re.match(r"^    (?:@ObservationIgnored )?(?:private\(set\) )?(?:private )?(?:var|let) (\w+)(.*)", value)
    if not found:
        continue
    name, rest = found.groups()
    if "{" in rest and "=" not in rest.split("{", 1)[0] and "didSet" not in rest and name != "activeLayerID":
        continue  # computed property
    if name not in lookup:
        raise ValueError(f"Unclassified stored property {name}:{line}")
    entries.append({"name": name, "line": line, "category": lookup[name], "declaration": value.strip()})
assert set(lookup) == {entry["name"] for entry in entries}, set(lookup) - {entry["name"] for entry in entries}
report = {"baseline_sha": "a19db9011282399785dc18efcfded904627bdcc2", "source": source.relative_to(root).as_posix(),
    "sha256": hashlib.sha256(source.read_bytes()).hexdigest(), "stored_property_count": len(entries),
    "counts": {group: sum(entry["category"] == group for entry in entries) for group in groups},
    "properties": entries,
    "representation_notes": [
        "Source Brush/Eraser mode is represented by current ProjectTool Brush/Eraser values; a separate parked rail-icon mode is not added in this bounded slice.",
        "Source parkedBrushTips is represented by separate native brushSettings, cloneSettings and blurSettings tip values; foreground is a shared QColor.",
        "Clone source, aligned flag, retained offset and sample-all-layers already belong to EditorProject. They are not duplicated in ProjectToolState.",
        "Crop ratio, remembered lasso family, remembered filter settings and expand/contract defaults are explicit remaining source choices, not implemented by unused fields.",
        "ProjectWorkspace.swift canSwitch prohibits some pending edits. MainWindow tab interruption behavior is a separate root-owned integration concern.",
    ]}
(here / "source-inventory.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
print(json.dumps({"stored_property_count": len(entries), "counts": report["counts"]}))

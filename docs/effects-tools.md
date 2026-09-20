# Adjustment editing tools

The implementation uses pinned Compositor `a19db9011282399785dc18efcfded904627bdcc2`. The pure algorithms are in `src/effects_tools`; supplementary Qt controls are in `src/ui/AdjustmentAdvancedControls.*`; `AdjustmentDialog.cpp` integrates them into the actual live and destructive editors. The original upstream checkout remains intact.

## APIs and source contracts

| Header | Public behavior | Source |
|---|---|---|
| `Levels.h` | `LevelRange` normalization, `LevelsSettings`, histogram, three Auto modes, black/gray/white calibration, transformed original-pixel sampling and handle movement | `Document/Levels.swift`, `LevelsAutomatic.swift`, `UI/LevelsSheet.swift`, actual `LevelsPixels.c` |
| `Hue.h` | Seven color ranges, wrapped four-handle bands, sample replace/add/remove, inverted selected range, target drag baseline and before/after spectrum | `Document/HueSaturation.swift:5-213,507-556`, `UI/HueSaturationSheet.swift` |
| `CurveMath.h`, `Curves.h` | Shared cubic Hermite interpolation, 256-point graph, insertion, movement, interior removal and selected-channel reset | `Document/Curves.swift:3-40`, `UI/CurvesControls.swift` |
| `Color.h` | RGB/hex/HSB conversion, gray/black hue retention, visible composite sampling and sample-ring dimensions | `Document/ColorPalette.swift`, `Rendering/SampleRingOverlay.swift`, `Rendering/EditorCanvas.swift` |

`levelsFromAdjustmentJson` / `withLevelsSettings`, `hueFromAdjustmentJson` / `withHueSettings`, and `curvesFromAdjustmentJson` / `withCurvesSettings` read and update the full adjustment object. Other top-level metadata survives. HSV enum-key dictionaries use Swift's alternating key/value arrays. Missing entries resolve to source defaults; serialization writes all seven entries. This preserves meaning, not the original dictionary bytes or whitespace. HSV editing updates `hsvSettings`; once that exists, old top-level hue/saturation/lightness fields do not drive rendering.

`levelsHistogram(raster, mappedSelection)` iterates immutable tiles and calls the actual C routine. Each channel is unpremultiplied to its nearest byte bin and weighted by alpha times selection coverage. RGB is the mean of the three channel histograms. No RGBA materialization occurs. Display scaling leaves counts unchanged and caps isolated peaks at four times the 95th percentile of positive interior bins. Auto clipping uses a strict greater-than comparison against 0.1 percent of total weight; Contrast shares a composite interval, Color stretches individual channels, and Color + neutral midtones computes a gamma from each stretched channel's weighted mean.

Levels calibration samples original straight RGB, resets the composite range, and updates all three individual channels. Gray fractions outside (0,1) skip that channel. Sampler coordinates map through the inverse layer transform, floor to the source pixel and reject transparent/exterior pixels. Histogram selection must already match the source raster. The dialog supplies `editing::mappedCoverage` for destructive edits; live adjustment input ignores the document selection, as `AdjustmentEditing.swift` does.

HSV band sampling requires a non-Master range, no Colorize, and saturation greater than 0.02. Targeting chooses the most strongly weighted named range, captures its initial hue/saturation and applies total horizontal view delta divided by two. Repeated move events use that baseline rather than accumulating. Hue clamps to +/-180 and saturation to +/-100. Colorize starts at Master hue 0, saturation 25; turning it off restores ordinary defaults. The native target gesture uses Control for hue, corresponding to Command in the Mac UI.

Curves accept 2-32 points with endpoint x values 0 and 255, ordered interior x and coordinates in [0,255]. The graph and renderer share the same Hermite evaluator. Insertion requires x strictly between 1 and 254, more than one unit from existing x values, and fewer than 32 points; selecting a nearby point uses a distance below 14 in curve coordinates. Interior dragging uses the source one-unit neighbor spacing, and endpoints retain x. An imported valid curve can contain fractional x gaps narrower than the editor's one-unit rule. A drag that would violate schema validity is explicitly rejected; the Qt control keeps the previous curve and exposes the reason as its tooltip. This defensive refusal is a documented difference from the unchecked source UI.

`sampleCompositeColor` renders one canonical document pixel and converts premultiplied RGB to rounded 8-bit straight RGB. It returns no sample outside the canvas or at zero alpha. `sampleRingGeometry` describes the source 116x116 ring: origin at pointer minus 58, inset 15, dark outline width 24, color width 16, split at y 58. The ring geometry API is available; a complete native palette picker and on-canvas ring are still integration work.

## Qt controls and actual dialog

`LevelsAdvancedControls`, `HueAdvancedControls` and `CurvesAdvancedControls` are ordinary Qt Widgets without moc requirements. `setAdjustmentJson` updates state without emitting; `adjustmentJson` returns the full object; `onChanged` emits the changed object on the GUI thread. Callbacks do not mutate the document.

Levels accepts `setHistogram`, exposes `sampleMode` and `onSampleModeChanged`, and accepts original RGB through `applySample`. Hue exposes its sample mode and targeting state, accepts composite samples, and provides `beginTarget`, `dragTarget` and `endTarget`. The owner supplies canvas sampling and lifetime management. Curves handles its graph gestures internally. The dialog supplies channel/range and numeric fields, updates all controls from one settings object, and retains the original immutable document throughout its asynchronous previews.

The dialog now has an asynchronous original-input histogram worker, coalesced preview revisions, and a Preview checkbox. Apply stays disabled while a newer revision is pending. Preview-off displays the original but retains the fully adjusted candidate for Apply. Cancel returns no result and waits for in-flight workers before local state is destroyed. Original-pixel Levels sampling is stable after repeated samples. HSV samples the visible preview composite. The displayed image fits the preview's aspect ratio; mouse coordinates use that same fitted rectangle and ignore its letterbox.

Live Levels input follows `AdjustmentEditing.swift:14-21`: preserve all records, obtain the prefix before the adjustment in hierarchy order, and hide other non-group records. Group ancestry and mask-source references remain valid even when their records appear later in the flat array. A new adjustment uses the source insertion rule: inside an active folder, otherwise above the active sibling. `LayerAdjustment.swift:87-106` creates no selection mask, so this dialog does not invent one. Existing live masks and opacity/blend metadata remain intact.

## Verification and limits

Run from the workspace:

```powershell
& windows/tests/effects_tools/run.ps1
```

`tests/effects_tools/build-run-10.log` and `results.xml` record 26 successes, zero failures/skips: 18 pure algorithm/control cases and eight actual modal dialog cases. The build uses Qt 6.8.3, MSVC 14.44.35207 and SDK 10.0.26100.0, Debug x64. Qt tests use `QT_QPA_PLATFORM=offscreen` and explicitly load the installed Segoe UI font. The final build has no compiler warnings. Earlier build failures are retained in their numbered logs.

Dialog checks cover pending-revision Cancel, destructive Curves Apply, composite HSV sampling/target drag, live Levels input with a later group and live-mask source plus empty selection, insertion inside an active folder, Preview-off Apply, original-pixel Levels sampling/letterbox exclusion, and Colorize/reset synchronization. Six PNGs show the individual controls and actual Levels/HSV/Curves dialogs; they were inspected for readable text, visible controls and preview proportions. These are native offscreen renders and interactions, not Mac comparison or desktop user acceptance.

`tests/effects_tools/evidence-map.json` maps every case to commands and source evidence; `source-manifest.json` records implementation hashes; `audit/effects-tools-acceptance-additions.json` supplies 26 candidates for the shared ledger. Full upstream test methods claimed ported: zero. Bounded assertions and interactions do not cover every source session assertion.

Mac references remain `blocked_reference`. The separate `SOURCE-TEST-CONFLICT-LEVELS-ALPHA` still fails with the unchanged pinned wrapper sequence; see `docs/effects.md` and `tests/effects/levels-source-test-conflict.json`. Sharing curve math re-ran the ten effects tests successfully in `tests/effects/build-run-2.log`, with the conflict command still exiting 1. No tolerance or source expectation changed.

Remaining limits include the Mac histogram preview downscale above 8000 pixels per side, source interpolation/color-management differentials, user desktop and accessibility acceptance, complete source assertion ports, cancellation responsiveness on large renders, and large-image preview performance. The dialog currently computes a full-resolution candidate per coalesced revision before displaying a smaller image; it does not claim the source's optimized preview/cache performance. History grouping remains the caller's responsibility. Palette picker persistence and sample-ring drawing remain outside this implementation.

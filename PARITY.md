# Compositor Windows parity

Pinned upstream: `a19db9011282399785dc18efcfded904627bdcc2`.

The current expanding inventory contains **679 cases** from 94 grouped obligations, the upstream test invocations, and source control/event sites. This is not a frozen or complete denominator.

Fully implemented cases: **0/679**. Fully verified parity cases: **0/679**. Reference-blocked cases: **311**. Partial native implementations and analytic tests do not close broader parity obligations.

Machine-readable detail: [parity-ledger.json](parity-ledger.json). Native implementation verification is recorded separately in [VALIDATION.md](VALIDATION.md).

| ID | Requirement | Implementation | Result |
|---|---|---|---|
| P001.01 | Validate dimensions | partial | unverified |
| P001.02 | create intended background and initial layer | partial | unverified |
| P001.03 | cancellation preserves the current project | partial | unverified |
| P002.01 | Each tab retains document, viewport, history, active layer and busy state | partial | unverified |
| P002.02 | switches and closes safely | partial | unverified |
| P003.01 | File association, process activation and queued image imports target the correct project during modal edits | partial | unverified |
| P004.01 | Project path and saved revision change only after success | partial | unverified |
| P004.02 | reopening survives moved packages and missing original imports | partial | unverified |
| P005.01 | Save/discard/cancel work across tabs | partial | unverified |
| P005.02 | failed save prevents unintended close or replacement | partial | unverified |
| P006.01 | Transaction names, nested edits, no-op preservation, redo invalidation and saved-revision tracking agree with source | partial | unverified |
| P007.01 | Retain shared immutable images | partial | unverified |
| P007.02 | preserve the 100-entry and 256 MiB policy semantics without whole-image copies per pointer event | partial | unverified |
| P008.01 | All anchors, relative units, growth/shrink and transforms match | not_started | unverified |
| P008.02 | preserve masks and group metadata | not_started | unverified |
| P009.01 | Resampling, aspect constraints, physical units and 1–9600 PPI work | not_started | unverified |
| P009.02 | defaults and undo preserve original sources | not_started | unverified |
| P010.01 | Preview, snapping, symmetric modifier, apply/cancel and origin changes preserve layer/mask positions | not_started | unverified |
| P011.01 | Horizontal and vertical canvas flips update all relevant layers and masks as one undoable operation | not_started | unverified |
| P012.01 | Decode all four formats with a bounded pixel budget | partial | unverified |
| P012.02 | test HEIC on a machine without optional Store codecs | partial | unverified |
| P013.01 | Test all orientation values and embedded color profiles | partial | unverified |
| P013.02 | normalized output is 8-bit sRGB with correct alpha | partial | unverified |
| P014.01 | Multi-file, bitmap, screenshot and promised-file equivalents preserve image content and ordering | not_started | unverified |
| P015.01 | Read legacy defaults and current adjustments, masks, transforms and shape metadata without silent data loss | partial | unverified |
| P016.01 | Windows-to-Mac-to-Windows and reverse round trips retain semantic manifest fields and embedded assets | not_started | unverified |
| P017.01 | Reject invalid versions, graphs, paths, dimensions, numbers, duplicate IDs and assets without replacing a live document | partial | unverified |
| P018.01 | Stage a complete package | partial | unverified |
| P018.02 | fault-inject disk full, permission failure, interruption and stale backup recovery | partial | unverified |
| P018.03 | preserve the prior save | partial | unverified |
| P019.01 | Flatten through the canonical compositor | partial | unverified |
| P019.02 | preserve alpha, sRGB and resolution | partial | unverified |
| P019.03 | export does not mark edits saved | partial | unverified |
| P020.01 | Quality, chosen matte color and resolution work | partial | unverified |
| P020.02 | output is opaque and color-correct | partial | unverified |
| P021.01 | Preview represents encoded bytes | partial | unverified |
| P021.02 | size estimate and displayed result match the file ultimately saved | partial | unverified |
| P021.03 | stale jobs cannot overwrite new previews | partial | unverified |
| P022.01 | Blank/image layers, inline rename and deletion keep IDs, selection, history and mask dependencies valid | partial | unverified |
| P023.01 | Range/additive selection, primary layer and descendant selection agree | not_started | unverified |
| P023.02 | operations act on the intended roots once | not_started | unverified |
| P024.01 | Drag/drop, keyboard move and nested groups preserve sibling order | not_started | unverified |
| P024.02 | invalid parent/cycle operations are rejected | not_started | unverified |
| P025.01 | Alt-equivalent drag duplicates at the correct point without modifying the original or creating duplicates on a click | not_started | unverified |
| P026.01 | Transfer layers with independent assets, hierarchy and references | not_started | unverified |
| P026.02 | IDs and clipboard/drag lifetime are safe | not_started | unverified |
| P027.01 | Parent visibility is inherited | partial | unverified |
| P027.02 | folders retain default opacity/blend and children composite in the upstream order | partial | unverified |
| P028.01 | Result matches pre-merge appearance and bounds, including masks/blends/adjustments, and undoes in one step | not_started | unverified |
| P029.01 | Numeric keys and slider scrubbing support selected layers | partial | unverified |
| P029.02 | one scrub gives one undo transaction | partial | unverified |
| P030.01 | Test Normal, Multiply, Screen, Overlay, Darken, Lighten, Difference, Color Dodge/Burn, Hue, Saturation, Color and Luminosity with partial alpha | partial | unverified |
| P031.01 | Hover is temporary, cancel restores the model, cycling updates selected layers without focus leakage | not_started | unverified |
| P032.01 | Horizontal/vertical flips retain source resolution and correctly carry linked masks | not_started | unverified |
| P033.01 | Reveal/hide/add/delete/enable/disable and image-versus-mask target selection agree | partial | unverified |
| P033.02 | disabled masks remain saved | partial | unverified |
| P034.01 | Operations use grayscale coverage and correct exterior values, support selections and undo, and retain placement | not_started | unverified |
| P035.01 | Mask multiplies each descendant's coverage | partial | unverified |
| P035.02 | nested folders, active painting preview, crop and resize remain aligned | partial | unverified |
| P036.01 | Hidden and black bases still provide alpha | partial | unverified |
| P036.02 | source masks/opacity/transforms and upstream links contribute as in source | partial | unverified |
| P037.01 | Modifier-click creation/release, shared base, drag-out release, deletion/baking, cycles and undo preserve graph semantics | not_started | unverified |
| P038.01 | Layer-alone, mask-alone and linked transformations match | not_started | unverified |
| P038.02 | relinking, painting, save/reopen and external coverage agree | not_started | unverified |
| P039.01 | Move/scale/rotate/flip retain full source pixels | partial | unverified |
| P039.02 | drag rounding, typed fractions and transform apply/cancel match | partial | unverified |
| P040.01 | Multiple selected roots move once | not_started | unverified |
| P040.02 | selected pixels and whole layers retain their distinct transform behavior | not_started | unverified |
| P041.01 | Canvas/layer edges and centers, guides, modifiers, exact values and arrow stepping agree at multiple DPI values | not_started | unverified |
| P042.01 | Preserve convex-quad validation, flipped-source corner ordering, live preview, committed raster/mask warp and undo | not_started | unverified |
| P043.01 | Replace/add/subtract, square/circle constraints, center drag and intermediate modifier changes behave correctly | partial | unverified |
| P044.01 | Closing by click, double-click or Enter, deleting a corner, Escape and holes/winding match | partial | unverified |
| P045.01 | No selection edits unrestricted pixels | partial | unverified |
| P045.02 | an empty selection edits nothing across every tool and filter | partial | unverified |
| P046.01 | Outline movement remains distinct from moving image pixels | partial | unverified |
| P046.02 | step size and modifier state match | partial | unverified |
| P047.01 | Tolerance, point/3x3/5x5 sample, contiguous/noncontiguous and current/all-layer sampling match premultiplied pixel semantics | not_started | unverified |
| P048.01 | Coverage, canvas clipping, holes and command availability agree while text fields retain Select All | partial | unverified |
| P049.01 | Load image alpha and the source's Mask's Black Areas behavior, with correct transforms and softness | not_started | unverified |
| P050.01 | Foreground/background fills and clear/delete honor coverage, transformed sources and masks | partial | unverified |
| P050.02 | maintain one undo step | partial | unverified |
| P051.01 | Alpha, transformed bounds, layer creation, text-field focus and external clipboard formats work | partial | unverified |
| P052.01 | Lift/drop/cancel and duplicate retain unselected pixels, image/mask alignment and undo | not_started | unverified |
| P053.01 | Size 1–2000, hardness, opacity, color, erasing, mask target and selection limits work on transformed layers | partial | unverified |
| P054.01 | Distance-based deposition, opacity cap and equivalent sparse/dense events match the established algorithm | partial | unverified |
| P055.01 | Tail replacement leaves no ghosts | partial | unverified |
| P055.02 | boundary pixels agree | partial | unverified |
| P055.03 | mouse-up and the next stroke retain immutable snapshots without forced full flattening | partial | unverified |
| P056.01 | HLSL, WARP and software routes have explicit tests | partial | unverified |
| P056.02 | device loss and failed allocation preserve document state | partial | unverified |
| P057.01 | Shift-line, Escape, pointer capture loss and commit sequencing produce the intended single history transaction | partial | unverified |
| P058.01 | Content-Aware, Create Texture and Proximity Match preserve alpha, seed behavior, surrounding tone, coverage and opacity | not_started | unverified |
| P059.01 | Source setting, aligned/nonaligned and current/all-layer sampling behave across strokes and transforms | not_started | unverified |
| P060.01 | Strength, hardness, mask/image target, coverage and undo agree | not_started | unverified |
| P060.02 | retain source support boundaries | not_started | unverified |
| P061.01 | Carried color and displacement are distinct modes | not_started | unverified |
| P061.02 | transformed layers, selections and commit/cancel preserve correct pixels | not_started | unverified |
| P061.03 | add dedicated tests where upstream coverage is sparse | not_started | unverified |
| P062.01 | Linear/radial, foreground-background/transparent, reverse, opacity, endpoint editing, angular constraints and cancel agree | partial | unverified |
| P063.01 | Rectangle, rounded rectangle, ellipse, center/constraint modifiers and corner-radius rules create correct layers | not_started | unverified |
| P064.01 | Resize regenerates live shapes | not_started | unverified |
| P064.02 | pixel edits end that behavior | not_started | unverified |
| P064.03 | save/reopen preserves appropriate raster and style | not_started | unverified |
| P065.01 | Foreground/background, swap/reset, sampled canvas color and sampling ring agree on transparent and transformed content | not_started | unverified |
| P066.01 | HSB/RGB/hex, original/new preview, canvas sampling, OK/cancel and focus work without hiding the canvas | not_started | unverified |
| P067.01 | Identity, ranges, opacity/alpha, colorize defaults, live preview and cancel match | partial | unverified |
| P068.01 | Independent hue bands, falloff, wraparound, inversion, eyedropper add/subtract and targeted adjustment work | partial | unverified |
| P069.01 | RGB/individual channel ranges, input/output limits, gamma, histogram weighting and masks match | partial | unverified |
| P070.01 | Contrast, Color, Color + neutral midtones and black/gray/white eyedroppers preserve source formulas | not_started | unverified |
| P071.01 | Curve channels, point interpolation, identity, normalization, preview and persisted control points match | partial | unverified |
| P072.01 | Stops, offset and gamma use upstream linear-light formula and preserve alpha | partial | unverified |
| P073.01 | Endpoint colors, reverse, luminance lookup and alpha retention match | partial | unverified |
| P074.01 | Amount, size, roughness and seed retain a stable pattern across tiles, viewport changes and save/reopen | partial | unverified |
| P075.01 | Premultiplied color and grayscale inversion preserve alpha and selection coverage | partial | unverified |
| P076.01 | Hue/Saturation, Levels, Curves, Exposure, Gradient Map and Grain remain editable | partial | unverified |
| P076.02 | below/above, clipping, blend and mask scope match | partial | unverified |
| P077.01 | Radius and transparent edge growth, selection-limited changes, trim and transformed placement match Mac references | partial | unverified |
| P078.01 | Angle sign, distance conversion, spread, support margins and edge growth match | partial | unverified |
| P078.02 | do not substitute an uncalibrated directional blur | partial | unverified |
| P079.01 | Uniform/Gaussian, monochromatic/color, percentage, fixed preview seed and retained alpha match | partial | unverified |
| P080.01 | Barrel/pincushion sign, center normalization, bilinear sampling and transparent corners match | partial | unverified |
| P081.01 | Preserve upstream patch algorithm, selection, out-of-bounds extension, no-donor error, cancellation and undo | partial | unverified |
| P082.01 | Return an editable foreground mask using a licensed offline model | partial | unverified |
| P082.02 | quality is evaluated against real foreground subjects, not only a no-subject smoke test | partial | unverified |
| P083.01 | Refine, Contrast and Shift Edge preserve upstream order/ranges, existing-mask multiplication and full-resolution commit | partial | unverified |
| P083.02 | add dedicated tests | partial | unverified |
| P084.01 | Previews derive from originals, never accumulate | partial | unverified |
| P084.02 | cancellation/stale completions cannot mutate a closed or different tab | partial | unverified |
| P085.01 | Actual-pixel, fit, cursor-anchored zoom, wheel/pinch and temporary Space-pan stay correct across window/DPI changes | partial | unverified |
| P086.01 | Halving alignment, Lanczos quality, tile seams and premultiplied clamping match | not_started | unverified |
| P086.02 | pixel grid appears at intended zoom | not_started | unverified |
| P087.01 | Thumbnails, viewport, Copy Merged and exports share rendering semantics and invalidate stale caches | partial | unverified |
| P088.01 | Every enabled upstream action is reachable | not_started | unverified |
| P088.02 | map Cmd/Option semantically to Windows and preserve text editing/IME behavior | not_started | unverified |
| P089.01 | All hit targets and cursor states track DPI/zoom and transient modifiers | not_started | unverified |
| P089.02 | no stuck pointer capture | not_started | unverified |
| P090.01 | Sliders, numeric fields, arrow stepping, modifier steps and focus release match without duplicate undo entries | partial | unverified |
| P091.01 | Package DLLs, codecs, shaders and model assets | not_started | unverified |
| P091.02 | test standard-user clean installation, project opening, uninstall and file-association behavior for directory packages | not_started | unverified |
| P092.01 | Replace Sparkle with a Windows-specific authenticated update flow | not_started | unverified |
| P092.02 | test check, unavailable update, install, interrupted download and rollback | not_started | unverified |
| P092.03 | do not use the Mac feed | not_started | unverified |
| P093.01 | Keyboard navigation, UI Automation names, screen-reader interaction, high contrast and 100/125/150/200 percent DPI work | not_started | unverified |
| P093.02 | pen basics are usable without inventing upstream pressure features | not_started | unverified |
| P094.01 | Measure 4K brush p50/p95 and commit time, peak RAM/VRAM, large documents and concurrent tabs | partial | unverified |
| P094.02 | retain source limits and cancellable long operations | partial | unverified |
| ACC-UT-AdjustmentLayerTests-globalAdjustmentAffectsBelowButNotAboveAndRemainsLive | globalAdjustmentAffectsBelowButNotAboveAndRemainsLive | not_started | blocked_reference |
| ACC-UT-AdjustmentLayerTests-clippedCurveChangesOnlyItsBaseAndCopyMergedMatchesExport | clippedCurveChangesOnlyItsBaseAndCopyMergedMatchesExport | not_started | blocked_reference |
| ACC-UT-AdjustmentLayerTests-hueOpacityAndMaskPreserveOriginalPixels | hueOpacityAndMaskPreserveOriginalPixels | not_started | blocked_reference |
| ACC-UT-AdjustmentLayerTests-adjustmentPersistsDuplicatesAndUndoRestoresSettings | adjustmentPersistsDuplicatesAndUndoRestoresSettings | not_started | blocked_reference |
| ACC-UT-AdjustmentLayerTests-curvesIdentityAndImageCommandPreserveAlpha | curvesIdentityAndImageCommandPreserveAlpha | not_started | blocked_reference |
| ACC-UT-AdjustmentLayerTests-adjustmentBlendAndSoftMaskPreserveCoverage | adjustmentBlendAndSoftMaskPreserveCoverage | not_started | blocked_reference |
| ACC-UT-AdjustmentLayerTests-sharedEditorsKeepPixelsDynamicAndSupportCancel-arg01 | sharedEditorsKeepPixelsDynamicAndSupportCancel | not_started | blocked_reference |
| ACC-UT-AdjustmentLayerTests-sharedEditorsKeepPixelsDynamicAndSupportCancel-arg02 | sharedEditorsKeepPixelsDynamicAndSupportCancel | not_started | blocked_reference |
| ACC-UT-AdjustmentLayerTests-sharedEditorsKeepPixelsDynamicAndSupportCancel-arg03 | sharedEditorsKeepPixelsDynamicAndSupportCancel | not_started | blocked_reference |
| ACC-UT-AdjustmentLayerTests-sharedEditorsKeepPixelsDynamicAndSupportCancel-arg04 | sharedEditorsKeepPixelsDynamicAndSupportCancel | not_started | blocked_reference |
| ACC-UT-AdjustmentLayerTests-sharedEditorsKeepPixelsDynamicAndSupportCancel-arg05 | sharedEditorsKeepPixelsDynamicAndSupportCancel | not_started | blocked_reference |
| ACC-UT-AdjustmentLayerTests-sharedEditorsKeepPixelsDynamicAndSupportCancel-arg06 | sharedEditorsKeepPixelsDynamicAndSupportCancel | not_started | blocked_reference |
| ACC-UT-AdjustmentLayerTests-legacyHSVStillDecodesAndRenders | legacyHSVStillDecodesAndRenders | not_started | blocked_reference |
| ACC-UT-BlendShortcutTests-shiftPlusAndMinusStepTheActiveLayersBlendModeWhereverFocusIsExceptTextFields | shiftPlusAndMinusStepTheActiveLayersBlendModeWhereverFocusIsExceptTextFields | not_started | blocked_reference |
| ACC-UT-BrushIntersectionTests-selfCrossingsBlendInsteadOfTakingTheStrongestEdge | selfCrossingsBlendInsteadOfTakingTheStrongestEdge | not_started | blocked_reference |
| ACC-UT-BrushIntersectionTests-accumulationDependsOnDistanceNotEventCount | accumulationDependsOnDistanceNotEventCount | not_started | blocked_reference |
| ACC-UT-BrushIntersectionTests-softCrossingsRespectStrokeOpacityAndFlushIsIdempotent | softCrossingsRespectStrokeOpacityAndFlushIsIdempotent | not_started | blocked_reference |
| ACC-UT-BrushIntersectionTests-exportCrossingExample | exportCrossingExample | not_started | blocked_reference |
| ACC-UT-BrushPerformanceTests-fourKInteractiveStroke | fourKInteractiveStroke | not_started | blocked_reference |
| ACC-UT-BrushTests-bracketKeysReachTheBrushWhereverFocusIsExceptTextFields | bracketKeysReachTheBrushWhereverFocusIsExceptTextFields | not_started | blocked_reference |
| ACC-UT-BrushTests-shiftBracketsStepHardnessFromTheCanvas | shiftBracketsStepHardnessFromTheCanvas | not_started | blocked_reference |
| ACC-UT-BrushTests-continuousStrokeCrossesTilesAndCommitsOneUndo | continuousStrokeCrossesTilesAndCommitsOneUndo | not_started | blocked_reference |
| ACC-UT-BrushTests-softBrushProducesPartialAlphaAndCancelPreservesDocument | softBrushProducesPartialAlphaAndCancelPreservesDocument | not_started | blocked_reference |
| ACC-UT-BrushTests-softMaskPaintingPreviewMatchesCommitAndPersists | softMaskPaintingPreviewMatchesCommitAndPersists | not_started | blocked_reference |
| ACC-UT-BrushTests-brushStaysCircularOnNonuniformRotatedFlippedLayer | brushStaysCircularOnNonuniformRotatedFlippedLayer | not_started | blocked_reference |
| ACC-UT-BrushTests-maskedImagePaintingUsesCoverageAndOpacityOnlyOnce | maskedImagePaintingUsesCoverageAndOpacityOnlyOnce | not_started | blocked_reference |
| ACC-UT-BrushTests-importedImageLayerExpandsAcrossCanvasWithoutMovingImageOrMask | importedImageLayerExpandsAcrossCanvasWithoutMovingImageOrMask | not_started | blocked_reference |
| ACC-UT-BrushTests-paintedBoundsTrimTilePaddingAndKeepSoftEdges | paintedBoundsTrimTilePaddingAndKeepSoftEdges | not_started | blocked_reference |
| ACC-UT-BrushTests-opacityCapsTheWholeStrokeEvenWhereItOverlapsItself | opacityCapsTheWholeStrokeEvenWhereItOverlapsItself | not_started | blocked_reference |
| ACC-UT-BrushTests-softStrokeBuildsCoverageWhileKeepingItsFeatheredRim | softStrokeBuildsCoverageWhileKeepingItsFeatheredRim | not_started | blocked_reference |
| ACC-UT-BrushTests-spacedDabsLeaveNoVisibleRippleAlongTheStroke | spacedDabsLeaveNoVisibleRippleAlongTheStroke | not_started | blocked_reference |
| ACC-UT-BrushTests-sparseMouseSamplesFollowACurveInsteadOfStraightChords | sparseMouseSamplesFollowACurveInsteadOfStraightChords | not_started | blocked_reference |
| ACC-UT-BrushTests-liveStrokeReachesNewestSampleAndTailIsReplacedExactly | liveStrokeReachesNewestSampleAndTailIsReplacedExactly | not_started | blocked_reference |
| ACC-UT-BrushTests-opacityAppliesToMaskPainting | opacityAppliesToMaskPainting | not_started | blocked_reference |
| ACC-UT-BrushTests-shiftBracketsStepHardnessByQuarters | shiftBracketsStepHardnessByQuarters | not_started | blocked_reference |
| ACC-UT-BrushTests-numberKeysSetBrushAndGradientOpacity | numberKeysSetBrushAndGradientOpacity | not_started | blocked_reference |
| ACC-UT-BrushTests-largeBlankCanvasOnlyAllocatesTouchedTilesUntilCommit | largeBlankCanvasOnlyAllocatesTouchedTilesUntilCommit | not_started | blocked_reference |
| ACC-UT-BrushTests-foldersHiddenLayersAndDisabledMasksRejectPainting | foldersHiddenLayersAndDisabledMasksRejectPainting | not_started | blocked_reference |
| ACC-UT-CanvasEntryTests-eyedropperShortcutSelectsTool | eyedropperShortcutSelectsTool | not_started | blocked_reference |
| ACC-UT-CanvasEntryTests-clipboardSuggestsImagePixelsAndIgnoresText | clipboardSuggestsImagePixelsAndIgnoresText | not_started | blocked_reference |
| ACC-UT-CanvasEntryTests-mountingCanvasGivesItKeyboardFocus | mountingCanvasGivesItKeyboardFocus | not_started | blocked_reference |
| ACC-UT-CanvasSizeTests-everyAnchorPreservesSourceAndTransformForExpansionAndShrink | everyAnchorPreservesSourceAndTransformForExpansionAndShrink | not_started | blocked_reference |
| ACC-UT-CanvasSizeTests-relativeRatioAndUnitsUseFinalDimensions | relativeRatioAndUnitsUseFinalDimensions | not_started | blocked_reference |
| ACC-UT-CanvasSizeTests-coloredExtensionPreservesOldTransparencyAndRoundTripsWithUndo | coloredExtensionPreservesOldTransparencyAndRoundTripsWithUndo | not_started | blocked_reference |
| ACC-UT-CanvasSizeTests-transparentResizeIsAllocationFreeAndShrinkDoesNotAddFill | transparentResizeIsAllocationFreeAndShrinkDoesNotAddFill | not_started | blocked_reference |
| ACC-UT-CanvasThumbnailTests-thumbnailsTakeTheCanvasShape | thumbnailsTakeTheCanvasShape | not_started | blocked_reference |
| ACC-UT-CanvasThumbnailTests-layerPixelsSitWhereTheyAreOnTheCanvas | layerPixelsSitWhereTheyAreOnTheCanvas | not_started | blocked_reference |
| ACC-UT-CanvasThumbnailTests-masksFillTheCanvasWithTheirEdgeTone | masksFillTheCanvasWithTheirEdgeTone | not_started | blocked_reference |
| ACC-UT-CloneStampTests-copiesTheSourceUnderTheBrushKeepingAlignmentUntilItIsTurnedOff | copiesTheSourceUnderTheBrushKeepingAlignmentUntilItIsTurnedOff | not_started | blocked_reference |
| ACC-UT-CloneStampTests-cloneStampKeepsItsOwnSoftBrushTip | cloneStampKeepsItsOwnSoftBrushTip | not_started | blocked_reference |
| ACC-UT-ColorPickerTests-hexParsesFullShorthandAndRejectsInvalid | hexParsesFullShorthandAndRejectsInvalid | not_started | blocked_reference |
| ACC-UT-ColorPickerTests-hsbRoundTripsEightBitColors | hsbRoundTripsEightBitColors | not_started | blocked_reference |
| ACC-UT-ColorPickerTests-graysAndBlackKeepPreviousHueAndSaturation | graysAndBlackKeepPreviousHueAndSaturation | not_started | blocked_reference |
| ACC-UT-ColorPickerTests-canvasSamplingReadsCompositeAndCommitsOnlyOnOK | canvasSamplingReadsCompositeAndCommitsOnlyOnOK | not_started | blocked_reference |
| ACC-UT-ColorPickerTests-pickerReopensWhereItWasLastLeft | pickerReopensWhereItWasLastLeft | not_started | blocked_reference |
| ACC-UT-CompositorTests-dimensionValidation | dimensionValidation | not_started | blocked_reference |
| ACC-UT-CompositorTests-actualPixelsAndRoundTrip-arg01 | actualPixelsAndRoundTrip | not_started | blocked_reference |
| ACC-UT-CompositorTests-actualPixelsAndRoundTrip-arg02 | actualPixelsAndRoundTrip | not_started | blocked_reference |
| ACC-UT-CompositorTests-zoomKeepsCursorPixelFixed | zoomKeepsCursorPixelFixed | not_started | blocked_reference |
| ACC-UT-CompositorTests-fitAndResizeModes | fitAndResizeModes | not_started | blocked_reference |
| ACC-UT-CompositorTests-limitsAndNewDocumentReset | limitsAndNewDocumentReset | not_started | blocked_reference |
| ACC-UT-CropTests-dragGeometrySupportsReverseRatioMoveAndEveryHandle | dragGeometrySupportsReverseRatioMoveAndEveryHandle | not_started | blocked_reference |
| ACC-UT-CropTests-cropTranslatesWithoutResamplingAndUndoRestoresBounds | cropTranslatesWithoutResamplingAndUndoRestoresBounds | not_started | blocked_reference |
| ACC-UT-CropTests-sameSizeOffsetCropAndExpansionUseExactBounds | sameSizeOffsetCropAndExpansionUseExactBounds | not_started | blocked_reference |
| ACC-UT-CropTests-cancellationAndViewportMappingDoNotEditDocument | cancellationAndViewportMappingDoNotEditDocument | not_started | blocked_reference |
| ACC-UT-CropTests-cropEdgesSnapToNearbyEdges | cropEdgesSnapToNearbyEdges | not_started | blocked_reference |
| ACC-UT-CropTests-snapTargetsAreTheCanvasAndLayerBounds | snapTargetsAreTheCanvasAndLayerBounds | not_started | blocked_reference |
| ACC-UT-CropTests-cropDraggingRedrawsOnlyTheOverlay | cropDraggingRedrawsOnlyTheOverlay | not_started | blocked_reference |
| ACC-UT-CursorTests-leavingTheCanvasRestoresTheArrowWithEveryTool | leavingTheCanvasRestoresTheArrowWithEveryTool | not_started | blocked_reference |
| ACC-UT-CursorTests-aDragReleasedOutsideTheCanvasRestoresTheArrow | aDragReleasedOutsideTheCanvasRestoresTheArrow | not_started | blocked_reference |
| ACC-UT-CursorTests-theLayerListShowsTheArrowUnlessAModifierCursorApplies | theLayerListShowsTheArrowUnlessAModifierCursorApplies | not_started | blocked_reference |
| ACC-UT-CursorTests-duplicateAndDistortCursors | duplicateAndDistortCursors | not_started | blocked_reference |
| ACC-UT-CursorTests-hiddenTransformControlsLeaveOnlyMoving | hiddenTransformControlsLeaveOnlyMoving | not_started | blocked_reference |
| ACC-UT-CursorTests-optionOverALayerRowOffersDuplicatingExceptOverThumbnails | optionOverALayerRowOffersDuplicatingExceptOverThumbnails | not_started | blocked_reference |
| ACC-UT-DistortTests-perspectiveMappingHitsTheCornersAndTwistedShapesAreRefused | perspectiveMappingHitsTheCornersAndTwistedShapesAreRefused | not_started | blocked_reference |
| ACC-UT-DistortTests-distortingWarpsTheLayerIntoTheShapeAsOneUndoStep | distortingWarpsTheLayerIntoTheShapeAsOneUndoStep | not_started | blocked_reference |
| ACC-UT-DistortTests-distortedLayerIsTrimmedToItsVisiblePixels | distortedLayerIsTrimmedToItsVisiblePixels | not_started | blocked_reference |
| ACC-UT-DownsampleTests-halvingsAreReusedAndOnlyUsedForLargeReductions | halvingsAreReusedAndOnlyUsedForLargeReductions | not_started | blocked_reference |
| ACC-UT-DownsampleTests-aHardEdgeStaysSharpShrunkEightTimes | aHardEdgeStaysSharpShrunkEightTimes | not_started | blocked_reference |
| ACC-UT-DownsampleTests-fineStripesAverageToFlatGrayWithoutShimmer | fineStripesAverageToFlatGrayWithoutShimmer | not_started | blocked_reference |
| ACC-UT-DownsampleTests-translucentEdgesStayValidAndMasksStayGray | translucentEdgesStayValidAndMasksStayGray | not_started | blocked_reference |
| ACC-UT-ExportTests-pngPreservesDimensionsAlphaOrientationAndTransforms | pngPreservesDimensionsAlphaOrientationAndTransforms | not_started | blocked_reference |
| ACC-UT-ExportTests-orderVisibilityClippingAndAtomicOverwrite | orderVisibilityClippingAndAtomicOverwrite | not_started | blocked_reference |
| ACC-UT-ExportTests-blankCanvasAndOversizedCanvas | blankCanvasAndOversizedCanvas | not_started | blocked_reference |
| ACC-UT-FilterTests-gaussianBlurSoftensAHardEdgeWithoutFadingTheBordersAsOneUndoStep | gaussianBlurSoftensAHardEdgeWithoutFadingTheBordersAsOneUndoStep | partial | failed |
| ACC-UT-FilterTests-motionBlurStreaksAlongItsAngleCounterclockwiseFromHorizontal | motionBlurStreaksAlongItsAngleCounterclockwiseFromHorizontal | not_started | blocked_reference |
| ACC-UT-FilterTests-addNoiseChangesColorButNeverAlphaAndMonochromaticKeepsGrays | addNoiseChangesColorButNeverAlphaAndMonochromaticKeepsGrays | not_started | blocked_reference |
| ACC-UT-FilterTests-removeDistortionBendsAboutTheCenterAndOnlyPincushionCorrectionOpensTheCorners | removeDistortionBendsAboutTheCenterAndOnlyPincushionCorrectionOpensTheCorners | not_started | blocked_reference |
| ACC-UT-FloatingPanelTests-hueSaturationPanelSurvivesALayoutPass | hueSaturationPanelSurvivesALayoutPass | not_started | blocked_reference |
| ACC-UT-FloatingPanelTests-colorPickerPanelSurvivesALayoutPass | colorPickerPanelSurvivesALayoutPass | not_started | blocked_reference |
| ACC-UT-FloatingPanelTests-adjustmentEditorsUseMovableNonmodalPanels-arg01 | adjustmentEditorsUseMovableNonmodalPanels | not_started | blocked_reference |
| ACC-UT-FloatingPanelTests-adjustmentEditorsUseMovableNonmodalPanels-arg02 | adjustmentEditorsUseMovableNonmodalPanels | not_started | blocked_reference |
| ACC-UT-FloatingPanelTests-adjustmentEditorsUseMovableNonmodalPanels-arg03 | adjustmentEditorsUseMovableNonmodalPanels | not_started | blocked_reference |
| ACC-UT-FloatingPanelTests-adjustmentEditorsUseMovableNonmodalPanels-arg04 | adjustmentEditorsUseMovableNonmodalPanels | not_started | blocked_reference |
| ACC-UT-FloatingPanelTests-adjustmentEditorsUseMovableNonmodalPanels-arg05 | adjustmentEditorsUseMovableNonmodalPanels | not_started | blocked_reference |
| ACC-UT-FloatingPanelTests-adjustmentEditorsUseMovableNonmodalPanels-arg06 | adjustmentEditorsUseMovableNonmodalPanels | not_started | blocked_reference |
| ACC-UT-GradientTests-foregroundToBackgroundFillsCanvasAndCommitsOneUndo | foregroundToBackgroundFillsCanvasAndCommitsOneUndo | not_started | blocked_reference |
| ACC-UT-GradientTests-radialSpreadsFromStartToRimInEveryDirection | radialSpreadsFromStartToRimInEveryDirection | not_started | blocked_reference |
| ACC-UT-GradientTests-reverseOpacityAndDirectionFollowSettings | reverseOpacityAndDirectionFollowSettings | not_started | blocked_reference |
| ACC-UT-GradientTests-foregroundToTransparentPreservesUnderlyingPixelsAndAlpha | foregroundToTransparentPreservesUnderlyingPixelsAndAlpha | not_started | blocked_reference |
| ACC-UT-GradientTests-cancelUndoAndClicksLeaveDocumentUntouched | cancelUndoAndClicksLeaveDocumentUntouched | not_started | blocked_reference |
| ACC-UT-GradientTests-redraggingReplacesPendingLineWithoutAccumulating | redraggingReplacesPendingLineWithoutAccumulating | not_started | blocked_reference |
| ACC-UT-GradientTests-maskGradientWritesCoverageInsideLayerBounds | maskGradientWritesCoverageInsideLayerBounds | not_started | blocked_reference |
| ACC-UT-GradientTests-paletteChangesUpdatePendingPreview | paletteChangesUpdatePendingPreview | not_started | blocked_reference |
| ACC-UT-GroupingSelectionTests-singleLayerAndFolderAreWrappedRatherThanCreatingAChildFolder | singleLayerAndFolderAreWrappedRatherThanCreatingAChildFolder | not_started | blocked_reference |
| ACC-UT-GroupingSelectionTests-multipleSelectionPreservesOrderAndSelectedFolderDescendants | multipleSelectionPreservesOrderAndSelectedFolderDescendants | not_started | blocked_reference |
| ACC-UT-GroupingSelectionTests-itemsFromDifferentFoldersUseCommonParentAndEmptySelectionCreatesEmptyGroup | itemsFromDifferentFoldersUseCommonParentAndEmptySelectionCreatesEmptyGroup | not_started | blocked_reference |
| ACC-UT-GroupTests-nestedGroupsMoveOutCollapseAndDeleteUndo | nestedGroupsMoveOutCollapseAndDeleteUndo | not_started | blocked_reference |
| ACC-UT-GroupTests-hiddenParentOverridesChildrenAndExportOrderFollowsGroups | hiddenParentOverridesChildrenAndExportOrderFollowsGroups | not_started | blocked_reference |
| ACC-UT-GroupTests-groupsRoundTripAndSurviveImageAndCanvasResize | groupsRoundTripAndSurviveImageAndCanvasResize | not_started | blocked_reference |
| ACC-UT-GroupTests-malformedParentLinksAndCyclesAreRejected | malformedParentLinksAndCyclesAreRejected | not_started | blocked_reference |
| ACC-UT-HistoryTests-everyLayerEditRoundTripsWithSelection | everyLayerEditRoundTripsWithSelection | not_started | blocked_reference |
| ACC-UT-HistoryTests-navigationNoOpsAndSaveRevisionPreserveHistory | navigationNoOpsAndSaveRevisionPreserveHistory | not_started | blocked_reference |
| ACC-UT-HistoryTests-replacementCanvasAndNestedTransactionsUndoAsOne | replacementCanvasAndNestedTransactionsUndoAsOne | not_started | blocked_reference |
| ACC-UT-HistoryTests-historyBlockedDuringImportsAndDialogs | historyBlockedDuringImportsAndDialogs | not_started | blocked_reference |
| ACC-UT-HistoryTests-batchImportIsOneEntryAndFailuresDoNotAddHistory | batchImportIsOneEntryAndFailuresDoNotAddHistory | not_started | blocked_reference |
| ACC-UT-HistoryTests-queuedImportsHaveSeparateUndoEntries | queuedImportsHaveSeparateUndoEntries | not_started | blocked_reference |
| ACC-UT-HistoryTests-historyBoundsEntriesAndUniqueRetainedPixels | historyBoundsEntriesAndUniqueRetainedPixels | not_started | blocked_reference |
| ACC-UT-HueSaturationTests-defaultsAreAnExactNoOp | defaultsAreAnExactNoOp | not_started | blocked_reference |
| ACC-UT-HueSaturationTests-hueRotatesSaturationAndLightnessFollowPhotoshopRanges | hueRotatesSaturationAndLightnessFollowPhotoshopRanges | not_started | blocked_reference |
| ACC-UT-HueSaturationTests-colorizeGivesEverythingOneHueAndKeepsAlpha | colorizeGivesEverythingOneHueAndKeepsAlpha | not_started | blocked_reference |
| ACC-UT-HueSaturationTests-adjustmentStaysInsideTheSelectionAndIsOneUndoStep | adjustmentStaysInsideTheSelectionAndIsOneUndoStep | not_started | blocked_reference |
| ACC-UT-HueSaturationTests-previewIsLiveDoesNotTouchTheDocumentAndNeverAccumulates | previewIsLiveDoesNotTouchTheDocumentAndNeverAccumulates | not_started | blocked_reference |
| ACC-UT-HueSaturationTests-bandWeightsRampThroughFalloffAndWrapAround | bandWeightsRampThroughFalloffAndWrapAround | not_started | blocked_reference |
| ACC-UT-HueSaturationTests-colorRangesAdjustIndependently | colorRangesAdjustIndependently | not_started | blocked_reference |
| ACC-UT-HueSaturationTests-rangesLeaveOtherHuesAloneAndInvertFlipsTheBand | rangesLeaveOtherHuesAloneAndInvertFlipsTheBand | not_started | blocked_reference |
| ACC-UT-HueSaturationTests-slidersEditTheSelectedRangeAndTheAfterBarFollowsHueShifts | slidersEditTheSelectedRangeAndTheAfterBarFollowsHueShifts | not_started | blocked_reference |
| ACC-UT-HueSaturationTests-eyedroppersRecenterWidenAndNarrowTheBand | eyedroppersRecenterWidenAndNarrowTheBand | not_started | blocked_reference |
| ACC-UT-HueSaturationTests-targetedAdjustmentPicksTheRangeUnderTheCursor | targetedAdjustmentPicksTheRangeUnderTheCursor | not_started | blocked_reference |
| ACC-UT-HueSaturationTests-samplingNeedsAColorRangeAndAColorfulPixel | samplingNeedsAColorRangeAndAColorfulPixel | not_started | blocked_reference |
| ACC-UT-ImageAdjustmentTests-exposureWorksInLinearLightWithOffsetAndGamma | exposureWorksInLinearLightWithOffsetAndGamma | not_started | blocked_reference |
| ACC-UT-ImageAdjustmentTests-gradientMapColorsByBrightnessAndReverses | gradientMapColorsByBrightnessAndReverses | not_started | blocked_reference |
| ACC-UT-ImageAdjustmentTests-grainIsFixedInDocumentSpaceAndLeavesTransparencyAlone | grainIsFixedInDocumentSpaceAndLeavesTransparencyAlone | not_started | blocked_reference |
| ACC-UT-ImageAdjustmentTests-settingsSaveAndOlderAdjustmentsStillOpen | settingsSaveAndOlderAdjustmentsStillOpen | not_started | blocked_reference |
| ACC-UT-ImageAdjustmentTests-newAdjustmentLayersStartFromThePaletteRenderAndEditInThePanel | newAdjustmentLayersStartFromThePaletteRenderAndEditInThePanel | not_started | blocked_reference |
| ACC-UT-ImageAdjustmentTests-imageMenuExposureChangesTheLayerInOneStep | imageMenuExposureChangesTheLayerInOneStep | not_started | blocked_reference |
| ACC-UT-ImageAdjustmentTests-gradientMapColorsUseTheAppColorPicker | gradientMapColorsUseTheAppColorPicker | not_started | blocked_reference |
| ACC-UT-ImageImportTests-supportedFormats-arg01 | supportedFormats | not_started | blocked_reference |
| ACC-UT-ImageImportTests-supportedFormats-arg02 | supportedFormats | not_started | blocked_reference |
| ACC-UT-ImageImportTests-supportedFormats-arg03 | supportedFormats | not_started | blocked_reference |
| ACC-UT-ImageImportTests-supportedFormats-arg04 | supportedFormats | not_started | blocked_reference |
| ACC-UT-ImageImportTests-orientationAndColorConversion | orientationAndColorConversion | not_started | blocked_reference |
| ACC-UT-ImageImportTests-pngPreservesTransparency | pngPreservesTransparency | not_started | blocked_reference |
| ACC-UT-ImageImportTests-limitsAndInvalidFiles | limitsAndInvalidFiles | not_started | blocked_reference |
| ACC-UT-ImageImportTests-importPlacementAndPartialFailure | importPlacementAndPartialFailure | not_started | blocked_reference |
| ACC-UT-ImageImportTests-dropPositionUsesDocumentCoordinates | dropPositionUsesDocumentCoordinates | not_started | blocked_reference |
| ACC-UT-ImageImportTests-queuedImportsAreNotLost | queuedImportsAreNotLost | not_started | blocked_reference |
| ACC-UT-ImageImportTests-fileDropProvidersReachImporterInOrder | fileDropProvidersReachImporterInOrder | not_started | blocked_reference |
| ACC-UT-ImageSizeTests-resizePreservesLayerIdentityAndUndoRestoresSource | resizePreservesLayerIdentityAndUndoRestoresSource | not_started | blocked_reference |
| ACC-UT-ImageSizeTests-resolutionOnlyRetainsPixelsAndSurvivesSaveAndExport | resolutionOnlyRetainsPixelsAndSurvivesSaveAndExport | not_started | blocked_reference |
| ACC-UT-ImageSizeTests-rotatedHiddenLayerScalesInDocumentAxesAndInvalidSizeIsRejected | rotatedHiddenLayerScalesInDocumentAxesAndInvalidSizeIsRejected | not_started | blocked_reference |
| ACC-UT-JPEGExportTests-transparencyUsesChosenMatteAndProducesOpaqueSRGB | transparencyUsesChosenMatteAndProducesOpaqueSRGB | not_started | blocked_reference |
| ACC-UT-JPEGExportTests-qualityChangesBytesAndDecodedPixels | qualityChangesBytesAndDecodedPixels | not_started | blocked_reference |
| ACC-UT-LayerAppearanceTests-hoverPreviewIsTemporaryAndNeverChangesSavedState | hoverPreviewIsTemporaryAndNeverChangesSavedState | not_started | blocked_reference |
| ACC-UT-LayerAppearanceTests-moveToolNumberKeysSetSelectedLayersOpacityAsOneUndo | moveToolNumberKeysSetSelectedLayersOpacityAsOneUndo | not_started | blocked_reference |
| ACC-UT-LayerAppearanceTests-blendModesAndOpacityMatchKnownPixels | blendModesAndOpacityMatchKnownPixels | not_started | blocked_reference |
| ACC-UT-LayerAppearanceTests-opacityDragIsOneUndoAndKeepsSources | opacityDragIsOneUndoAndKeepsSources | not_started | blocked_reference |
| ACC-UT-LayerAppearanceTests-appearancePersistsThroughSaveResizeAndTransparentExport | appearancePersistsThroughSaveResizeAndTransparentExport | not_started | blocked_reference |
| ACC-UT-LayerMaskTests-addDisableDeleteUndoAndTargetSelection | addDisableDeleteUndoAndTargetSelection | not_started | blocked_reference |
| ACC-UT-LayerMaskTests-coverageOpacityAndDisabledMasksRenderCorrectly | coverageOpacityAndDisabledMasksRenderCorrectly | not_started | blocked_reference |
| ACC-UT-LayerMaskTests-transformedMaskResizeAndCanvasChangesStayAligned | transformedMaskResizeAndCanvasChangesStayAligned | not_started | blocked_reference |
| ACC-UT-LayerMaskTests-projectAndPNGPreserveCoverageAndDisabledState | projectAndPNGPreserveCoverageAndDisabledState | not_started | blocked_reference |
| ACC-UT-LayerMaskTests-masksRejectOlderSchemaAndUnsafePaths | masksRejectOlderSchemaAndUnsafePaths | not_started | blocked_reference |
| ACC-UT-LayerMaskTests-folderMasksClipEveryLayerInsideAndMultiplyWithTheirOwnMasks | folderMasksClipEveryLayerInsideAndMultiplyWithTheirOwnMasks | not_started | blocked_reference |
| ACC-UT-LayerMaskTests-folderMaskCanBePaintedInvertedAndLoadedAsASelection | folderMaskCanBePaintedInvertedAndLoadedAsASelection | not_started | blocked_reference |
| ACC-UT-LayerMaskTests-canvasShowsAFolderMaskWhileItIsBeingPainted | canvasShowsAFolderMaskWhileItIsBeingPainted | not_started | blocked_reference |
| ACC-UT-LayerMaskTests-folderMasksSaveResizeAndNeedTheNewFormat | folderMasksSaveResizeAndNeedTheNewFormat | not_started | blocked_reference |
| ACC-UT-LayerTests-blankLayersAreTransparentAndInsertedAboveSelection | blankLayersAreTransparentAndInsertedAboveSelection | not_started | blocked_reference |
| ACC-UT-LayerTests-deletionPreservesCanvasAndChoosesNeighbor | deletionPreservesCanvasAndChoosesNeighbor | not_started | blocked_reference |
| ACC-UT-LayerTests-renameAndVisibilityKeepIdentity | renameAndVisibilityKeepIdentity | not_started | blocked_reference |
| ACC-UT-LayerTests-reorderTranslatesVisibleOrderAndKeepsSelection | reorderTranslatesVisibleOrderAndKeepsSelection | not_started | blocked_reference |
| ACC-UT-LayerTests-unavailableActionsDoNotChangeDocument | unavailableActionsDoNotChangeDocument | not_started | blocked_reference |
| ACC-UT-LayerTests-nativeSelectionDoesNotReloadRowsAndReorderKeepsIdentity | nativeSelectionDoesNotReloadRowsAndReorderKeepsIdentity | not_started | blocked_reference |
| ACC-UT-LayerTests-selectionAndRenameDoNotInvalidateCanvasButPixelChangesDo | selectionAndRenameDoNotInvalidateCanvasButPixelChangesDo | not_started | blocked_reference |
| ACC-UT-LayerTests-compositingHonorsVisibilityOrderAndBlankLayers | compositingHonorsVisibilityOrderAndBlankLayers | not_started | blocked_reference |
| ACC-UT-LayerTests-newCanvasStartsWithOneSelectedEmptyLayer | newCanvasStartsWithOneSelectedEmptyLayer | not_started | blocked_reference |
| ACC-UT-LayerTests-deletingAMultiSelectionRemovesEveryLayerInOneStep | deletingAMultiSelectionRemovesEveryLayerInOneStep | not_started | blocked_reference |
| ACC-UT-LayerTests-duplicatingALayerByDraggingPlacesTheCopyAsOneStep | duplicatingALayerByDraggingPlacesTheCopyAsOneStep | not_started | blocked_reference |
| ACC-UT-LevelsTests-identityAndChannelSelectionAreExactNoOps | identityAndChannelSelectionAreExactNoOps | not_started | blocked_reference |
| ACC-UT-LevelsTests-inputClippingGammaOutputInversionAndAlpha | inputClippingGammaOutputInversionAndAlpha | partial | failed |
| ACC-UT-LevelsTests-channelsCoexistAndUseDocumentedOrder | channelsCoexistAndUseDocumentedOrder | not_started | blocked_reference |
| ACC-UT-LevelsTests-histogramExcludesTransparencyAndWeightsSelection | histogramExcludesTransparencyAndWeightsSelection | not_started | blocked_reference |
| ACC-UT-LevelsTests-selectionPreviewCancelCommitUndoAndPersistence | selectionPreviewCancelCommitUndoAndPersistence | not_started | blocked_reference |
| ACC-UT-LevelsTests-stalePreviewCannotReturnAfterOffOrReopen | stalePreviewCannotReturnAfterOffOrReopen | not_started | blocked_reference |
| ACC-UT-LevelsTests-histogramDisplayKeepsDistributionVisibleBesideClippingSpikes | histogramDisplayKeepsDistributionVisibleBesideClippingSpikes | not_started | blocked_reference |
| ACC-UT-LevelsTests-autoAlgorithmsAndEyedropperCalibration | autoAlgorithmsAndEyedropperCalibration | not_started | blocked_reference |
| ACC-UT-LevelsTests-eyedropperSamplesOriginalAndRejectsTransparentPixels | eyedropperSamplesOriginalAndRejectsTransparentPixels | not_started | blocked_reference |
| ACC-UT-LevelsTests-panelPreview | panelPreview | not_started | blocked_reference |
| ACC-UT-LiveMaskTests-clippingColorPreservesSoftBaseAlphaWithoutBlackFringe | clippingColorPreservesSoftBaseAlphaWithoutBlackFringe | not_started | blocked_reference |
| ACC-UT-LiveMaskTests-optionClickCreatesSharedStackAndDragOutReleases | optionClickCreatesSharedStackAndDragOutReleases | not_started | blocked_reference |
| ACC-UT-LiveMaskTests-hiddenBlackSourceSuppliesAlphaAndRasterMasksMultiply | hiddenBlackSourceSuppliesAlphaAndRasterMasksMultiply | not_started | blocked_reference |
| ACC-UT-LiveMaskTests-cyclesUndoPersistenceBakeAndDelete | cyclesUndoPersistenceBakeAndDelete | not_started | blocked_reference |
| ACC-UT-LiveMaskTests-movingSourceChangesCoverageAndChainsMultiply | movingSourceChangesCoverageAndChainsMultiply | not_started | blocked_reference |
| ACC-UT-MagicWandTests-contiguousStopsAtOtherColorsWhileNonContiguousFindsEveryMatch | contiguousStopsAtOtherColorsWhileNonContiguousFindsEveryMatch | not_started | blocked_reference |
| ACC-UT-MagicWandTests-toleranceAppliesToEveryChannelIncludingAlpha | toleranceAppliesToEveryChannelIncludingAlpha | not_started | blocked_reference |
| ACC-UT-MagicWandTests-sampleSizeAveragesThePixelsAroundTheClick | sampleSizeAveragesThePixelsAroundTheClick | not_started | blocked_reference |
| ACC-UT-MagicWandTests-outlinesReproduceTheirPixelsWithHolesAndCornerTouches | outlinesReproduceTheirPixelsWithHolesAndCornerTouches | not_started | blocked_reference |
| ACC-UT-MagicWandTests-theWandReadsTheActiveLayerOrEveryVisibleLayerAndCombinesModes | theWandReadsTheActiveLayerOrEveryVisibleLayerAndCombinesModes | not_started | blocked_reference |
| ACC-UT-MagicWandTests-clickingInsideASelectionMakesANewWandSelectionRatherThanDeselecting | clickingInsideASelectionMakesANewWandSelectionRatherThanDeselecting | not_started | blocked_reference |
| ACC-UT-MaskTransformTests-aLinkedMaskMovesWithItsLayerWhicheverThumbnailIsSelected | aLinkedMaskMovesWithItsLayerWhicheverThumbnailIsSelected | not_started | blocked_reference |
| ACC-UT-MaskTransformTests-unlinkedTheLayerMovesAloneAndItsMaskStaysOnTheCanvas | unlinkedTheLayerMovesAloneAndItsMaskStaysOnTheCanvas | not_started | blocked_reference |
| ACC-UT-MaskTransformTests-unlinkedTheMaskMovesAloneAndRelinkedTheyMoveTogether | unlinkedTheMaskMovesAloneAndRelinkedTheyMoveTogether | not_started | blocked_reference |
| ACC-UT-MaskTransformTests-aMovedHideAllMaskKeepsHidingPastItsPixels | aMovedHideAllMaskKeepsHidingPastItsPixels | not_started | blocked_reference |
| ACC-UT-MaskTransformTests-placementAndLinkAreSavedAndPaintingFollowsThePlacement | placementAndLinkAreSavedAndPaintingFollowsThePlacement | not_started | blocked_reference |
| ACC-UT-ProjectTests-projectRoundTripSurvivesSourceRemovalAndPackageMove | projectRoundTripSurvivesSourceRemovalAndPackageMove | not_started | blocked_reference |
| ACC-UT-ProjectTests-overwriteReplacesPackageAndDropsRemovedAssets | overwriteReplacesPackageAndDropsRemovedAssets | not_started | blocked_reference |
| ACC-UT-ProjectTests-failedSavePreservesPreviouslySavedPackage | failedSavePreservesPreviouslySavedPackage | not_started | blocked_reference |
| ACC-UT-ProjectTests-unsupportedCorruptAndUnsafeMetadataAreRejected | unsupportedCorruptAndUnsafeMetadataAreRejected | not_started | blocked_reference |
| ACC-UT-ProjectTests-missingEmbeddedImageIsRejected | missingEmbeddedImageIsRejected | not_started | blocked_reference |
| ACC-UT-ProjectTests-projectOperationsBlockEditsAndQueueImageImports | projectOperationsBlockEditsAndQueueImageImports | not_started | blocked_reference |
| ACC-UT-ProjectWorkspaceTests-newCanvasOpensAnEmptyTabWithoutAModal | newCanvasOpensAnEmptyTabWithoutAModal | not_started | blocked_reference |
| ACC-UT-ProjectWorkspaceTests-layerDropProviderCopiesIntoANewProject | layerDropProviderCopiesIntoANewProject | not_started | blocked_reference |
| ACC-UT-ProjectWorkspaceTests-tabsKeepIndependentDocumentsAndUndo | tabsKeepIndependentDocumentsAndUndo | not_started | blocked_reference |
| ACC-UT-ProjectWorkspaceTests-crossProjectCopyRemapsIdentityAndHasIndependentUndo | crossProjectCopyRemapsIdentityAndHasIndependentUndo | not_started | blocked_reference |
| ACC-UT-ProjectWorkspaceTests-dockCreatesTabsAndTargetedImportUsesExistingTab | dockCreatesTabsAndTargetedImportUsesExistingTab | not_started | blocked_reference |
| ACC-UT-ProjectWorkspaceTests-quitAsksAboutTheActiveTabFirst | quitAsksAboutTheActiveTabFirst | not_started | blocked_reference |
| ACC-UT-RasterSnapshotTests-mouseUpAndNextStrokeNeverFlattenTheDocument | mouseUpAndNextStrokeNeverFlattenTheDocument | not_started | blocked_reference |
| ACC-UT-RasterSnapshotTests-snapshotsStayImmutableAndDisplayMatchesExportAcrossSuccessiveStrokes | snapshotsStayImmutableAndDisplayMatchesExportAcrossSuccessiveStrokes | not_started | blocked_reference |
| ACC-UT-RasterSnapshotTests-maskMouseUpIsImmediateAndPreservesTheImage | maskMouseUpIsImmediateAndPreservesTheImage | not_started | blocked_reference |
| ACC-UT-RasterSnapshotTests-softwareFallbackKeepsTheSoftStrokeContinuous | softwareFallbackKeepsTheSoftStrokeContinuous | not_started | blocked_reference |
| ACC-UT-RasterSnapshotTests-eightHundredPixelSoftStrokeHasNoPeriodicRidges | eightHundredPixelSoftStrokeHasNoPeriodicRidges | not_started | blocked_reference |
| ACC-UT-SelectionClipboardTests-copyAndPastePutsPixelsOnANewLayerInPlace | copyAndPastePutsPixelsOnANewLayerInPlace | not_started | blocked_reference |
| ACC-UT-SelectionClipboardTests-cutLeavesAHoleAndPasteRestoresThePixels | cutLeavesAHoleAndPasteRestoresThePixels | not_started | blocked_reference |
| ACC-UT-SelectionClipboardTests-layerViaCopyCopiesTheSelectionOrDuplicatesTheLayer | layerViaCopyCopiesTheSelectionOrDuplicatesTheLayer | not_started | blocked_reference |
| ACC-UT-SelectionClipboardTests-transformSelectionMovesPixelsAndOutlineAsOneUndo | transformSelectionMovesPixelsAndOutlineAsOneUndo | not_started | blocked_reference |
| ACC-UT-SelectionClipboardTests-escapeRestoresExactlyWithoutAnUndoStep | escapeRestoresExactlyWithoutAnUndoStep | not_started | blocked_reference |
| ACC-UT-SelectionClipboardTests-applyingAnUnchangedTransformLeavesSoftEdgesUntouched | applyingAnUnchangedTransformLeavesSoftEdgesUntouched | not_started | blocked_reference |
| ACC-UT-SelectionClipboardTests-scalingAndMovingPastTheLayerEdgeGrowsTheLayer | scalingAndMovingPastTheLayerEdgeGrowsTheLayer | not_started | blocked_reference |
| ACC-UT-SelectionClipboardTests-lassoShapedSelectionCopiesAndPastes | lassoShapedSelectionCopiesAndPastes | not_started | blocked_reference |
| ACC-UT-SelectionClipboardTests-copyMergedTakesEveryVisibleLayerNotJustTheActiveOne | copyMergedTakesEveryVisibleLayerNotJustTheActiveOne | not_started | blocked_reference |
| ACC-UT-SelectionEditTests-brushPaintsOnlyInsideTheSelection | brushPaintsOnlyInsideTheSelection | not_started | blocked_reference |
| ACC-UT-SelectionEditTests-emptySelectionEditsNothing | emptySelectionEditsNothing | not_started | blocked_reference |
| ACC-UT-SelectionEditTests-gradientStaysInsideTheSelection | gradientStaysInsideTheSelection | not_started | blocked_reference |
| ACC-UT-SelectionEditTests-fillUsesPaletteInsideSelectionOrWholeLayerWithoutOne | fillUsesPaletteInsideSelectionOrWholeLayerWithoutOne | not_started | blocked_reference |
| ACC-UT-SelectionEditTests-deleteClearsSelectedPixelsOrDeletesTheLayerWithoutASelection | deleteClearsSelectedPixelsOrDeletesTheLayerWithoutASelection | not_started | blocked_reference |
| ACC-UT-SelectionEditTests-maskFillHidesOnlyTheSelectedArea | maskFillHidesOnlyTheSelectedArea | not_started | blocked_reference |
| ACC-UT-SelectionEditTests-clipFollowsScaledLayersAndSoftensEdges | clipFollowsScaledLayersAndSoftensEdges | not_started | blocked_reference |
| ACC-UT-SelectionEditTests-maskButtonAddsWhiteMaskOrHidesTheSelection | maskButtonAddsWhiteMaskOrHidesTheSelection | not_started | blocked_reference |
| ACC-UT-SelectionEditTests-layerMenuMasksUseTheSelection | layerMenuMasksUseTheSelection | not_started | blocked_reference |
| ACC-UT-SelectionEditTests-maskFromSelectionLinesUpOnScaledLayers | maskFromSelectionLinesUpOnScaledLayers | not_started | blocked_reference |
| ACC-UT-SelectionEditTests-deletingWithTheMaskTargetedRemovesOnlyTheMask | deletingWithTheMaskTargetedRemovesOnlyTheMask | not_started | blocked_reference |
| ACC-UT-SelectionEditTests-cmdDragMovesSelectedPixelsAndOutlineAsOneUndo | cmdDragMovesSelectedPixelsAndOutlineAsOneUndo | not_started | blocked_reference |
| ACC-UT-SelectionEditTests-duplicatePixelDragPreservesSourceAndUndoesTogether | duplicatePixelDragPreservesSourceAndUndoesTogether | not_started | blocked_reference |
| ACC-UT-SelectionEditTests-cmdArrowNudgesPixelsAndMasksRefuse | cmdArrowNudgesPixelsAndMasksRefuse | not_started | blocked_reference |
| ACC-UT-SelectionEditTests-invertKeepsTransparencyStaysInSelectionAndWorksOnMasks | invertKeepsTransparencyStaysInSelectionAndWorksOnMasks | not_started | blocked_reference |
| ACC-UT-SelectionEditTests-pixelMoveNeverShowsTheOutlineAtItsOldSpot | pixelMoveNeverShowsTheOutlineAtItsOldSpot | not_started | blocked_reference |
| ACC-UT-SelectionEditTests-invertIsFastOnLargeImagesAndHandlesUniformMasksWithASelection | invertIsFastOnLargeImagesAndHandlesUniformMasksWithASelection | not_started | blocked_reference |
| ACC-UT-SelectionEditTests-quickOperationsNeverDimTheInterface | quickOperationsNeverDimTheInterface | not_started | blocked_reference |
| ACC-UT-SelectionEditTests-invertWorksInEveryTool | invertWorksInEveryTool | not_started | blocked_reference |
| ACC-UT-SelectionTests-replaceAddAndSubtractCombineOutlines | replaceAddAndSubtractCombineOutlines | not_started | blocked_reference |
| ACC-UT-SelectionTests-modifiersPickModeAndSelectionIsClippedToCanvas | modifiersPickModeAndSelectionIsClippedToCanvas | not_started | blocked_reference |
| ACC-UT-SelectionTests-emptySelectionIsDistinctFromNoSelection | emptySelectionIsDistinctFromNoSelection | not_started | blocked_reference |
| ACC-UT-SelectionTests-clickDeselectsAndSelectionStepsUndo | clickDeselectsAndSelectionStepsUndo | not_started | blocked_reference |
| ACC-UT-SelectionTests-polygonalCornersCanBeRemovedAndClosed | polygonalCornersCanBeRemovedAndClosed | not_started | blocked_reference |
| ACC-UT-SelectionTests-antialiasingControlsEdgeCoverage | antialiasingControlsEdgeCoverage | not_started | blocked_reference |
| ACC-UT-SelectionTests-selectAllInverseAndToolSwitchCancelsDraft | selectAllInverseAndToolSwitchCancelsDraft | not_started | blocked_reference |
| ACC-UT-SelectionTests-cursorBadgeFollowsModifiersButKeepsAnOutlinesStartingMode | cursorBadgeFollowsModifiersButKeepsAnOutlinesStartingMode | not_started | blocked_reference |
| ACC-UT-SelectionTests-draggingMovesTheOutlineInWholePixelsAsOneUndo | draggingMovesTheOutlineInWholePixelsAsOneUndo | not_started | blocked_reference |
| ACC-UT-SelectionTests-movingOffCanvasAndBackKeepsTheWholeShape | movingOffCanvasAndBackKeepsTheWholeShape | not_started | blocked_reference |
| ACC-UT-SelectionTests-arrowNudgesAndMoveIsOnlyForNewModeOnARealSelection | arrowNudgesAndMoveIsOnlyForNewModeOnARealSelection | not_started | blocked_reference |
| ACC-UT-SelectionTests-expandAndContractGrowAndShrinkTheOutline | expandAndContractGrowAndShrinkTheOutline | not_started | blocked_reference |
| ACC-UT-SelectionTests-expandStaysOnCanvasAndContractCanEmptyTheSelection | expandStaysOnCanvasAndContractCanEmptyTheSelection | not_started | blocked_reference |
| ACC-UT-SelectionTests-cmdClickingAMaskSelectsItsBlackAreas | cmdClickingAMaskSelectsItsBlackAreas | not_started | blocked_reference |
| ACC-UT-SelectionTests-maskSelectionFollowsTheLayerTransformAndIgnoresAllWhiteMasks | maskSelectionFollowsTheLayerTransformAndIgnoresAllWhiteMasks | not_started | blocked_reference |
| ACC-UT-SelectionTests-cmdClickingALayerSelectsItsOpaquePixels | cmdClickingALayerSelectsItsOpaquePixels | not_started | blocked_reference |
| ACC-UT-SelectionTests-marqueeDrawsWholePixelRectanglesInAnyDirection | marqueeDrawsWholePixelRectanglesInAnyDirection | not_started | blocked_reference |
| ACC-UT-SelectionTests-marqueeShiftMakesSquaresAndCenteredDragsGrowFromTheAnchor | marqueeShiftMakesSquaresAndCenteredDragsGrowFromTheAnchor | not_started | blocked_reference |
| ACC-UT-SelectionTests-marqueeEllipseSelectsAnOvalInItsBoxAndShiftMakesACircle | marqueeEllipseSelectsAnOvalInItsBoxAndShiftMakesACircle | not_started | blocked_reference |
| ACC-UT-SelectionTests-optionDraggingTheMarqueeSubtractsWithoutDrawingFromTheCenter | optionDraggingTheMarqueeSubtractsWithoutDrawingFromTheCenter | not_started | blocked_reference |
| ACC-UT-SelectionTests-mKeyChoosesTheMarqueeThenSwitchesItsShape | mKeyChoosesTheMarqueeThenSwitchesItsShape | not_started | blocked_reference |
| ACC-UT-SelectionTests-shiftStartsAnAddAndOnlyAFreshShiftSquaresTheMarquee | shiftStartsAnAddAndOnlyAFreshShiftSquaresTheMarquee | not_started | blocked_reference |
| ACC-UT-SelectionTests-lKeyChoosesTheLassoThenSwitchesItsMode | lKeyChoosesTheLassoThenSwitchesItsMode | not_started | blocked_reference |
| ACC-UT-ShapeToolTests-rectangleFillsANewLayerWithTheForegroundColorAsOneUndoStep | rectangleFillsANewLayerWithTheForegroundColorAsOneUndoStep | not_started | blocked_reference |
| ACC-UT-ShapeToolTests-ellipseLeavesItsCornersClearWithShiftCircleAndOptionFromCenter | ellipseLeavesItsCornersClearWithShiftCircleAndOptionFromCenter | not_started | blocked_reference |
| ACC-UT-ShapeToolTests-aClickEscapeOrToolSwitchMakesNoLayer | aClickEscapeOrToolSwitchMakesNoLayer | not_started | blocked_reference |
| ACC-UT-ShapeToolTests-roundedRectanglesFollowTheRadiusAndClampToAPill | roundedRectanglesFollowTheRadiusAndClampToAPill | not_started | blocked_reference |
| ACC-UT-SliderSnapTests-clickingTheTrackSnapsTheKnobAndStillEditsTheValue | clickingTheTrackSnapsTheKnobAndStillEditsTheValue | not_started | blocked_reference |
| ACC-UT-SmartEditTests-fillContinuesRepeatingTexture | fillContinuesRepeatingTexture | not_started | blocked_reference |
| ACC-UT-SmartEditTests-fillReconstructsBackgroundInsideSelectionAndUndoes | fillReconstructsBackgroundInsideSelectionAndUndoes | not_started | blocked_reference |
| ACC-UT-SmartEditTests-cancelAndNoSourceLeaveOriginalUntouched | cancelAndNoSourceLeaveOriginalUntouched | not_started | blocked_reference |
| ACC-UT-SmartEditTests-visionRequestRunsOnAnImage | visionRequestRunsOnAnImage | not_started | blocked_reference |
| ACC-UT-SpotHealingTests-healsTheBlemishUnderTheBrushAndNothingElse-arg01 | healsTheBlemishUnderTheBrushAndNothingElse | not_started | blocked_reference |
| ACC-UT-SpotHealingTests-healsTheBlemishUnderTheBrushAndNothingElse-arg02 | healsTheBlemishUnderTheBrushAndNothingElse | not_started | blocked_reference |
| ACC-UT-SpotHealingTests-healsTheBlemishUnderTheBrushAndNothingElse-arg03 | healsTheBlemishUnderTheBrushAndNothingElse | not_started | blocked_reference |
| ACC-UT-TiledLayerTests-tiledLayersDrawLikeOneImage-arg01 | tiledLayersDrawLikeOneImage | not_started | blocked_reference |
| ACC-UT-TiledLayerTests-tiledLayersDrawLikeOneImage-arg02 | tiledLayersDrawLikeOneImage | not_started | blocked_reference |
| ACC-UT-TiledLayerTests-tiledLayersDrawLikeOneImage-arg03 | tiledLayersDrawLikeOneImage | not_started | blocked_reference |
| ACC-UT-TiledLayerTests-maskStrokesDrawLikeOneMask-arg01 | maskStrokesDrawLikeOneMask | not_started | blocked_reference |
| ACC-UT-TiledLayerTests-maskStrokesDrawLikeOneMask-arg02 | maskStrokesDrawLikeOneMask | not_started | blocked_reference |
| ACC-UT-TiledLayerTests-maskStrokesDrawLikeOneMask-arg03 | maskStrokesDrawLikeOneMask | not_started | blocked_reference |
| ACC-UT-TiledLayerTests-paintingAtTheLayersEdgeDoesNotChangeIt-arg01 | paintingAtTheLayersEdgeDoesNotChangeIt | not_started | blocked_reference |
| ACC-UT-TiledLayerTests-paintingAtTheLayersEdgeDoesNotChangeIt-arg02 | paintingAtTheLayersEdgeDoesNotChangeIt | not_started | blocked_reference |
| ACC-UT-TiledLayerTests-translucentStrokesDrawLikeOneImage-arg01 | translucentStrokesDrawLikeOneImage | not_started | blocked_reference |
| ACC-UT-TiledLayerTests-translucentStrokesDrawLikeOneImage-arg02 | translucentStrokesDrawLikeOneImage | not_started | blocked_reference |
| ACC-UT-TiledLayerTests-paintingAScaledDownLayerDoesNotShiftItsPixels | paintingAScaledDownLayerDoesNotShiftItsPixels | not_started | blocked_reference |
| ACC-UT-TiledLayerTests-paintingAScaledDownLayersMaskDoesNotShiftItsPixels | paintingAScaledDownLayersMaskDoesNotShiftItsPixels | not_started | blocked_reference |
| ACC-UT-TransformPressTests-draggingOutsideTheLayerMovesIt | draggingOutsideTheLayerMovesIt | not_started | blocked_reference |
| ACC-UT-TransformPressTests-optionDraggingOutsideTheLayerDuplicatesIt | optionDraggingOutsideTheLayerDuplicatesIt | not_started | blocked_reference |
| ACC-UT-TransformTests-duplicateTransformPreservesOriginalAndSupportsUndoAndCancel | duplicateTransformPreservesOriginalAndSupportsUndoAndCancel | not_started | blocked_reference |
| ACC-UT-TransformTests-autoSelectCanBeDisabledAndCommandClickOverridesIt | autoSelectCanBeDisabledAndCommandClickOverridesIt | not_started | blocked_reference |
| ACC-UT-TransformTests-hoverRegionsMatchRotatedEdgesCornersAndRotationHandle | hoverRegionsMatchRotatedEdgesCornersAndRotationHandle | not_started | blocked_reference |
| ACC-UT-TransformTests-blankLayersHaveNoTransformHandlesOrEditing | blankLayersHaveNoTransformHandlesOrEditing | not_started | blocked_reference |
| ACC-UT-TransformTests-rotatedResizeKeepsOppositeAnchorAtEveryHandle | rotatedResizeKeepsOppositeAnchorAtEveryHandle | not_started | blocked_reference |
| ACC-UT-TransformTests-moveRotateAndShiftConstraints | moveRotateAndShiftConstraints | not_started | blocked_reference |
| ACC-UT-TransformTests-documentMappingAndRotatedHitTesting | documentMappingAndRotatedHitTesting | not_started | blocked_reference |
| ACC-UT-TransformTests-previewCommitCancelAndUndoPreserveSources | previewCommitCancelAndUndoPreserveSources | not_started | blocked_reference |
| ACC-UT-TransformTests-scalePercentSetsBothSidesAboutTheCenter | scalePercentSetsBothSidesAboutTheCenter | not_started | blocked_reference |
| ACC-UT-TransformTests-noOpInvalidValuesAndSwitchingTools | noOpInvalidValuesAndSwitchingTools | not_started | blocked_reference |
| ACC-UT-TransformTests-renderingRotatesScalesAndFlipsWithoutReplacingPixels | renderingRotatesScalesAndFlipsWithoutReplacingPixels | not_started | blocked_reference |
| ACC-UT-TransformTests-layerListToolKeysAndTransformNudge | layerListToolKeysAndTransformNudge | not_started | blocked_reference |
| ACC-UI-CompositorUITests-testCreateCanvasAndNavigation | testCreateCanvasAndNavigation | not_started | blocked_reference |
| ACC-UI-CompositorUITests-testLaunchPerformance | testLaunchPerformance | not_started | blocked_reference |
| ACC-UI-CompositorUITestsLaunchTests-testLaunch | testLaunch | not_started | blocked_reference |
| CMD-28edbed046c05dee | CommandGroup(replacing: .undoRedo) | not_started | unverified |
| CMD-ea98323fd360002e | Button("Undo") | not_started | unverified |
| CMD-38c13fac0d77cb27 | Button("Redo") | not_started | unverified |
| CMD-3e862047cde2f714 | Button(session.history.canUndo ? "Undo \(session.history.undoName)" : "Undo") | not_started | unverified |
| CMD-492a35ea51ef8170 | Button(session.history.canRedo ? "Redo \(session.history.redoName)" : "Redo") | not_started | unverified |
| CMD-80d54decc7bca3c8 | CommandGroup(replacing: .newItem) | not_started | unverified |
| CMD-c5804048d2b2c946 | Button("New Canvas…") | not_started | unverified |
| CMD-43dfd36f5d122053 | Button("Open Project…") | not_started | unverified |
| CMD-1bc012ca26e8cf23 | Button("Import Images…") | not_started | unverified |
| CMD-8eb1ed0b4fc1a5b1 | CommandGroup(replacing: .saveItem) | not_started | unverified |
| CMD-153900bd8deb0bd0 | Button("Save") | not_started | unverified |
| CMD-859fc9da71e989e1 | Button("Save As…") | not_started | unverified |
| CMD-e591ee64e25ce0a2 | Button("Export PNG…") | not_started | unverified |
| CMD-c6e5913ec0c85778 | Button("Export JPEG…") | not_started | unverified |
| CMD-903434af5a230f66 | Button("Close Project") | not_started | unverified |
| CMD-0e3bdba7c2676e51 | CommandGroup(after: .appInfo) | not_started | unverified |
| CMD-fbbf30febefdd4fe | Button("Check for Updates…") | not_started | unverified |
| CMD-ebbf1b178ddd39c8 | CommandGroup(after: .toolbar) | not_started | unverified |
| CMD-a4c7e7e73262b237 | Button("Fit Canvas") | not_started | unverified |
| CMD-e4ef7527090dc209 | Button("Actual Pixels") | not_started | unverified |
| CMD-b2753a216d1a974e | Button("Zoom In") | not_started | unverified |
| CMD-64b0dd8b298a1b04 | Button("Zoom Out") | not_started | unverified |
| CMD-008e48f106ace5c5 | Toggle("Pixel Grid (800% and above)", isOn: Binding(get: { session.showsPixelGrid },                                                                               set: { session.showsPixelGrid = $0 })) | not_started | unverified |
| CMD-699522819763b297 | Toggle("Show Transform Controls", isOn: Binding(get: { session.showsTransformControls },                                                                           set: { session.showsTransformControls = $0 })) | not_started | unverified |
| CMD-911af7570de070f4 | CommandGroup(replacing: .appVisibility) | not_started | unverified |
| CMD-75a2b164a4c31bfc | Button("Hide Compositor") | not_started | unverified |
| CMD-f75fca0946740487 | Button("Hide Others") | not_started | unverified |
| CMD-2ee514e3b49bb0e3 | Button("Show All") | not_started | unverified |
| CMD-5f7c387b8944dd04 | CommandGroup(replacing: .pasteboard) | not_started | unverified |
| CMD-4225a1eb49b2e871 | Button("Cut") | not_started | unverified |
| CMD-1a027b6885502979 | Button("Copy") | not_started | unverified |
| CMD-c4f257245f146573 | Button("Copy Merged") | not_started | unverified |
| CMD-a36e21c4592b0343 | Button("Paste") | not_started | unverified |
| CMD-a5d1a45bf6249a37 | CommandGroup(after: .pasteboard) | not_started | unverified |
| CMD-3e3be9bb9d4b0f3e | Button("Fill with Foreground Color") | not_started | unverified |
| CMD-dc1e528d30b93efe | Button("Fill with Background Color") | not_started | unverified |
| CMD-97875337939bb702 | Button("Clear Selection Pixels") | not_started | unverified |
| CMD-f9372f4941348704 | Button("Content-Aware Fill…") | not_started | unverified |
| CMD-4af1e779c147debd | CommandMenu("Select") | not_started | unverified |
| CMD-5c63c9e06a48842a | Button("All") | not_started | unverified |
| CMD-16e60fdf7c560633 | Button("Deselect") | not_started | unverified |
| CMD-6bf814bbe2f86ab5 | Button("Inverse") | not_started | unverified |
| CMD-db34c0bfe2d53493 | Button("Layer's Pixels") | not_started | unverified |
| CMD-da70c526de8847ee | Button("Mask's Black Areas") | not_started | unverified |
| CMD-d1110fc073b627b8 | Button("Expand by \(session.selectionExpandAmount) px") | not_started | unverified |
| CMD-e7c6e1f220100459 | Button("Contract by \(session.selectionContractAmount) px") | not_started | unverified |
| CMD-0f49f05e8d9e4554 | CommandMenu("Image") | not_started | unverified |
| CMD-0c29ea7caa4b7609 | Button("Curves…") | not_started | unverified |
| CMD-10bce0392413f5ec | Button("Levels…") | not_started | unverified |
| CMD-292ec55fa413e71d | Button("Hue/Saturation…") | not_started | unverified |
| CMD-ea7f37accb016938 | Button("\(kind.rawValue)…") | not_started | unverified |
| CMD-115d04645bbe188b | Button(session.isMaskSelected ? "Invert Mask" : "Invert") | not_started | unverified |
| CMD-b21bc135541e756b | Button("Canvas Size…") | not_started | unverified |
| CMD-fba7c68fbf0109d1 | Button("Image Size…") | not_started | unverified |
| CMD-e085bb2135683a28 | Button("Flip Canvas Horizontal") | not_started | unverified |
| CMD-c499c0751502534f | Button("Flip Canvas Vertical") | not_started | unverified |
| CMD-a6a4187006021caa | CommandMenu("Filter") | not_started | unverified |
| CMD-45aef824212a3128 | Button("\(kind.rawValue)…") | not_started | unverified |
| CMD-3991a93671d902ce | CommandMenu("Layer") | not_started | unverified |
| CMD-0844e349f9f82d52 | Menu("New Adjustment Layer") | not_started | unverified |
| CMD-51dbb39427004f32 | Button(kind.rawValue + "…") | not_started | unverified |
| CMD-79c50936893e9c33 | Button("Edit Adjustment…") | not_started | unverified |
| CMD-a2502d6c04252482 | Button(session.canTransformSelection ? "Transform Selection" : "Transform Layer") | not_started | unverified |
| CMD-74892387fd356b6d | Button(session.selection == nil ? "Duplicate Layer" : "Layer via Copy") | not_started | unverified |
| CMD-4e3f39b729c697be | Button(session.activeLayer?.maskSourceID == nil ? "Create Clipping Mask" : "Release Clipping Mask") | not_started | unverified |
| CMD-5727ff11e8780ef9 | Button("Group Selected Layers") | not_started | unverified |
| CMD-c582d7f77f9ded39 | Button("Move Out of Folder") | not_started | unverified |
| CMD-19da482f82b38081 | Button("New Blank Layer") | not_started | unverified |
| CMD-c772daf3979be340 | Button("Rename Layer…") | not_started | unverified |
| CMD-333649b8151a6408 | Button(session.activeLayer?.isVisible == false ? "Show Layer" : "Hide Layer") | not_started | unverified |
| CMD-bedf9d85e008a437 | Button("Move Layer Up") | not_started | unverified |
| CMD-b9874a90c9d8a002 | Button("Move Layer Down") | not_started | unverified |
| CMD-4f7f51f0cc432ae0 | Button(session.mergeTitle) | not_started | unverified |
| CMD-42bc1453e552a2e3 | Button("Flip Layer Horizontal") | not_started | unverified |
| CMD-4e135e45a75310f3 | Button("Flip Layer Vertical") | not_started | unverified |
| CMD-2acff442a36f3dd6 | Button(session.isMaskSelected && session.activeLayer?.mask != nil ? "Delete Layer Mask" : session.selectedLayerIDs.count > 1 ? "Delete Layers" : "Delete Layer") | not_started | unverified |
| CMD-6f71ebc61eba312b | Toggle("Sample Ring", isOn: $session.showsSampleRing) | not_started | unverified |
| CMD-05393b3efba1e713 | Button("Fit") | not_started | unverified |
| CMD-6ae5d410afa725db | Button("100%") | not_started | unverified |
| CMD-c5cb673fc83e736a | Button("OK", role: .cancel) | not_started | unverified |
| CMD-40d13ec44784e5f8 | Button("OK") | not_started | unverified |
| CMD-e8a6d17d70f78208 | Button("OK") | not_started | unverified |
| CMD-3cb5736975d88983 | Button { action } label: { view } | not_started | unverified |
| CMD-c10ee40aad0fe019 | Button { action } label: { view } | not_started | unverified |
| CMD-1e353d922c5ac544 | Button { action } label: { view } | not_started | unverified |
| CMD-63de03a91f20ba68 | Button { action } label: { view } | not_started | unverified |
| CMD-85db78e140c81720 | Picker("Mode", selection: $session.brushMode) | not_started | unverified |
| CMD-c16592072ec8981a | Picker("Mode", selection: $session.blurMode) | not_started | unverified |
| CMD-7342a0deaf4d99f0 | Picker("Type", selection: $session.spotHealingMode) | not_started | unverified |
| CMD-6dcc9cc7cbd577a8 | Toggle("Aligned", isOn: $session.cloneSettings.aligned) | not_started | unverified |
| CMD-07f3852b98bcd13c | Picker("Sample", selection: $session.cloneSettings.sampleAllLayers) | not_started | unverified |
| CMD-ff88f640a65a06b8 | TextField("Size", value: Binding<Double>(get: { Double(session.brushSettings.diameter) },                 set: { session.brushSettings.diameter = $0.isFinite ? CGFloat(min(2000, max(1, $0))) : 40 }),                 format: .number.precision(.fractionLength(0))) | not_started | unverified |
| CMD-e1b606eaecec73a6 | Slider(value: $session.brushSettings.hardness, in: 0...1) | not_started | unverified |
| CMD-3f191db3f42eaf5a | TextField("Hardness", value: Binding<Double>(get: { Double(session.brushSettings.hardness * 100) },                 set: { session.brushSettings.hardness = $0.isFinite ? CGFloat(min(1, max(0, $0 / 100))) : 1 }),                 format: .number.precision(.fractionLength(0))) | not_started | unverified |
| CMD-303fbfc9dfce85fd | Slider(value: $session.brushSettings.opacity, in: 0.01...1) | not_started | unverified |
| CMD-7654b705f029a2f3 | TextField("Opacity", value: Binding<Double>(get: { Double(session.brushSettings.opacity * 100) },                 set: { session.brushSettings.opacity = $0.isFinite ? CGFloat(min(100, max(1, $0)) / 100) : 1 }),                 format: .number.precision(.fractionLength(0))) | not_started | unverified |
| CMD-1475b604b3308320 | Picker("Paint", selection: $session.maskPaintWhite) | not_started | unverified |
| CMD-a89d2fbeb4d46a56 | Button { action } label: { view } | not_started | unverified |
| CMD-5b42442d79daae30 | Picker("Units", selection: $draft.unit) | not_started | unverified |
| CMD-f9f5b8c810ecccc9 | TextField("Width", value: dimension(true), format: .number.precision(.fractionLength(0...3))) | not_started | unverified |
| CMD-d2ca2fc92f55cf3b | TextField("Height", value: dimension(false), format: .number.precision(.fractionLength(0...3))) | not_started | unverified |
| CMD-d4d059281ea8795b | Toggle("Relative to current dimensions", isOn: $draft.relative) | not_started | unverified |
| CMD-caf4ae8efa53d3b2 | Toggle("Lock original aspect ratio", isOn: $draft.locked) | not_started | unverified |
| CMD-40cc56828745538f | Picker("Canvas extension", selection: $extensionChoice) | not_started | unverified |
| CMD-e8ea0d99b4472a45 | Button("Cancel") | not_started | unverified |
| CMD-4f92a9b0920e2b66 | Button("OK") | not_started | unverified |
| CMD-17ee086c037766d8 | Button { action } label: { view } | not_started | unverified |
| CMD-86f7f6187f84ce7b | Button("Black · Hide") | not_started | unverified |
| CMD-bee29da4a801acee | Button("White · Reveal") | not_started | unverified |
| CMD-e30931a77ef8a191 | Button { action } label: { view } | not_started | unverified |
| CMD-a0812f5e4f325416 | Button { action } label: { view } | not_started | unverified |
| CMD-19ab3c2926b8ec72 | Button { action } label: { view } | not_started | unverified |
| CMD-67bbd76c36045a54 | TextField("Hex", text: $hexDraft) | not_started | unverified |
| CMD-efea4a0a95914b9a | TextField(label, value: Binding(                 get: { Int((color[keyPath: channel] * 255).rounded()) },                 set: { newValue in                     var rgb = color                     rgb[keyPath: channel] = CGFloat(min(255, max(0, newValue))) / 255                     hsb.setRGB(rgb)                 }), format: .number) | not_started | unverified |
| CMD-0365968dfc8a1357 | Button { action } label: { view } | not_started | unverified |
| CMD-02b3dcc9afa5a9ed | Button { action } label: { view } | not_started | unverified |
| CMD-52238429304de27b | Picker("Ratio", selection: $session.cropRatioChoice) | not_started | unverified |
| CMD-c0ea298b4090c0da | Button("Cancel") | not_started | unverified |
| CMD-6c4db9fee73b9f08 | Button("Apply Crop") | not_started | unverified |
| CMD-ce828f294f2c5203 | Picker("Channel", selection: $settings.channel) | not_started | unverified |
| CMD-90d228459ea2d1f0 | Button("Remove point") | not_started | unverified |
| CMD-7df6f7c83e4bb0b2 | Button("Reset curve") | not_started | unverified |
| CMD-052823d63e611158 | Picker("Quality", selection: Binding(get: { settings.backgroundQuality },                                                      set: { new in update { $0.backgroundQuality = new } })) | not_started | unverified |
| CMD-50df408b7d288221 | Picker("Distribution", selection: flag(\.gaussian)) | not_started | unverified |
| CMD-65f9ef8ac72ca8c8 | Toggle("Monochromatic", isOn: flag(\.monochromatic)) | not_started | unverified |
| CMD-b0dfc45f008f9901 | Toggle("Preview", isOn: Binding(get: { edit?.preview ?? true },                                             set: { session.updateFilter(settings, preview: $0) })) | not_started | unverified |
| CMD-87a3c8b6c54b7455 | Button("Cancel") | not_started | unverified |
| CMD-c368dc38ebff5e4b | Button("OK") | not_started | unverified |
| CMD-ce761d22812a1e46 | Slider(value: Binding(get: { logarithmic ? log(settings[keyPath: key]) : settings[keyPath: key] },                                   set: { value in update { $0[keyPath: key] = ((logarithmic ? exp(value) : value) * step).rounded() / step } }),                    in: logarithmic ? log(range.lowerBound)...log(range.upperBound) : range) | not_started | unverified |
| CMD-016ec443d18b2707 | TextField(title, value: Binding(get: { settings[keyPath: key] }, set: { value in update { $0[keyPath: key] = value } }),                       format: .number.precision(.fractionLength(0...decimals))) | not_started | unverified |
| CMD-4379482f2431bf0c | Toggle("Reverse", isOn: $settings.reversed) | not_started | unverified |
| CMD-709f1df3db133f81 | Button(action: action) | not_started | unverified |
| CMD-40731baf1f51e417 | Picker("Shape", selection: $session.gradientSettings.shape) | not_started | unverified |
| CMD-f406472ef64f44b7 | Picker("Colors", selection: $session.gradientSettings.style) | not_started | unverified |
| CMD-763ce72d42ce96b3 | Toggle("Reverse", isOn: $session.gradientSettings.reversed) | not_started | unverified |
| CMD-b301934bb708146e | Slider(value: $session.gradientSettings.opacity, in: 0.01...1) | not_started | unverified |
| CMD-05940931fca2295c | TextField("Opacity", value: Binding<Double>(get: { Double(session.gradientSettings.opacity * 100) },                 set: { session.gradientSettings.opacity = $0.isFinite ? CGFloat(min(100, max(1, $0)) / 100) : 1 }),                 format: .number.precision(.fractionLength(0))) | not_started | unverified |
| CMD-e0543001027e1036 | Button("Cancel") | not_started | unverified |
| CMD-d7eccf0e2798e513 | Button("Apply") | not_started | unverified |
| CMD-0c141b6f0f55809a | Picker("Range", selection: settings.range) | not_started | unverified |
| CMD-55214819831493cf | Toggle("Apply outside this range instead", isOn: settings.invertRange) | not_started | unverified |
| CMD-1efac172128ab880 | Toggle("Colorize", isOn: Binding(get: { current.colorize }, set: { colorize in                     // Photoshop starts colorizing at hue 0, saturation 25.                     settings.wrappedValue = colorize ? .colorizeStart : HueSaturationSettings()                 })) | not_started | unverified |
| CMD-471fe82054340eec | Toggle("Preview", isOn: preview) | not_started | unverified |
| CMD-20ce956dbb5db513 | Button("Reset") | not_started | unverified |
| CMD-898dc82d3c68b909 | Button("Cancel") | not_started | unverified |
| CMD-ed82794a6b29757e | Button("OK") | not_started | unverified |
| CMD-56d342a5e0987a4f | Slider(value: value, in: range) | not_started | unverified |
| CMD-aa50d264fea5c350 | TextField(title, value: value, format: .number.precision(.fractionLength(0))) | not_started | unverified |
| CMD-99f006cb7c495707 | Button { action } label: { view } | not_started | unverified |
| CMD-5a03ab1aa4f92aa8 | Button { action } label: { view } | not_started | unverified |
| CMD-6f3cbbe05fb8cc44 | Picker("Units", selection: $unit) | not_started | unverified |
| CMD-34fd9cfe7b4363cd | TextField("Width", value: dimension(isWidth: true), format: .number.precision(.fractionLength(0...3))) | not_started | unverified |
| CMD-224e8c5873f8c558 | TextField("Height", value: dimension(isWidth: false), format: .number.precision(.fractionLength(0...3))) | not_started | unverified |
| CMD-bfb62fcd6a244f65 | Toggle("Lock aspect ratio", isOn: $locked) | not_started | unverified |
| CMD-b13b222a5283c6b1 | TextField("Resolution", value: $resolution, format: .number.precision(.fractionLength(0...3))) | not_started | unverified |
| CMD-f6063ffbc56a1416 | Toggle("Resample", isOn: $resample) | not_started | unverified |
| CMD-f14426944afc1956 | Picker("Sampling", selection: $sampling) | not_started | unverified |
| CMD-6b0505ac58b8b9f8 | Button("Cancel") | not_started | unverified |
| CMD-477634f0f399f7d4 | Button("Resize") | not_started | unverified |
| CMD-b21fbc9c28564588 | Slider(value: $options.quality, in: 0...1, step: 0.01) | not_started | unverified |
| CMD-1865cb609b96c38f | Button("Cancel") | not_started | unverified |
| CMD-e9e30545165c0b29 | Button("Export…") | not_started | unverified |
| CMD-967c028dded39d49 | Picker("Shape", selection: Binding(get: { session.marqueeKind }, set: { kind in                     session.cancelLasso()                     session.marqueeKind = kind                 })) | not_started | unverified |
| CMD-ead60bdefd739ed2 | Picker("Lasso", selection: Binding(get: { session.lassoKind }, set: { kind in                     session.cancelLasso()                     session.lassoKind = kind                 })) | not_started | unverified |
| CMD-9ece280c27c20649 | Picker("Mode", selection: Binding(get: { session.displayedSelectionMode },                                               set: { session.selectionModeChoice = $0 })) | not_started | unverified |
| CMD-f827ba973f9a6545 | Toggle("Anti-alias", isOn: $session.selectionAntialiased) | not_started | unverified |
| CMD-39ac32fa8ad48703 | Button("Deselect") | not_started | unverified |
| CMD-3e6cfd1281d4a074 | TextField("Tolerance", value: Binding(get: { session.wandSettings.tolerance },                                                       set: { session.wandSettings.tolerance = min(255, max(0, $0)) }),                           format: .number) | not_started | unverified |
| CMD-b79cec9cf79dc77a | Picker("Sample Size", selection: $session.wandSettings.sampleSize) | not_started | unverified |
| CMD-1e9560d460d161d6 | Picker("Sample", selection: $session.wandSettings.sampleAllLayers) | not_started | unverified |
| CMD-0b7f687f91f79f63 | Toggle("Contiguous", isOn: $session.wandSettings.contiguous) | not_started | unverified |
| CMD-8cf6cf0f7fdd78ce | Button(title, action: action) | not_started | unverified |
| CMD-54c4208d0c226ec6 | TextField(title, value: Binding(get: { amount.wrappedValue },                                             set: { amount.wrappedValue = min(500, max(1, $0)) }),                       format: .number) | not_started | unverified |
| CMD-7dbfea58321ac73f | Slider(value: Binding(get: { session.activeLayer?.opacity ?? 1 },                                       set: { session.setLayerOpacity($0) }), in: 0...1,                        onEditingChanged: { if $0 { session.beginOpacityEdit() } else { session.finishOpacityEdit() } }) | not_started | unverified |
| CMD-b9dc85a11ec1e720 | TextField("Opacity percent", text: $percentage) | not_started | unverified |
| CMD-a6fd441b5f714ec7 | Button { action } label: { view } | not_started | unverified |
| CMD-77a237886f51b87b | Button(kind.rawValue) | not_started | unverified |
| CMD-66d63e0e510604fe | Button { action } label: { view } | not_started | unverified |
| CMD-7907faaee06c705b | Button { action } label: { view } | not_started | unverified |
| CMD-105338ddf4f1ba16 | Button { action } label: { view } | not_started | unverified |
| CMD-c84c8c36d5322be8 | Picker("Channel", selection: Binding(get: { settings.channel }, set: { channel in update { $0.channel = channel } })) | not_started | unverified |
| CMD-ca40e45e63fadb0d | Button(mode.rawValue) | not_started | unverified |
| CMD-e892211b4c03840e | Toggle("Preview", isOn: Binding(get: { edit?.preview ?? true }, set: {                     session.updateLevels(settings, preview: $0)                 })) | not_started | unverified |
| CMD-6464f90946b172c8 | Button("Reset") | not_started | unverified |
| CMD-d30c028efb3e54bb | Button("Cancel") | not_started | unverified |
| CMD-4850b7aee1bc3848 | Button("OK") | not_started | unverified |
| CMD-a7037126a1e1853e | TextField(name, value: binding, format: .number.precision(.fractionLength(decimals))) | not_started | unverified |
| CMD-a0aa6b34c1109a53 | Button { action } label: { view } | not_started | unverified |
| CMD-1e99b0188b18ff65 | TextField("Zoom", text: $zoomText) | not_started | unverified |
| CMD-4aaee7d0c98faf38 | Button("Open project") | not_started | unverified |
| CMD-41e203d077f10306 | Button("Import image") | not_started | unverified |
| CMD-1f777db8159ed961 | Button("Create canvas") | not_started | unverified |
| CMD-db458f1793cfd8e3 | TextField(title, text: text) | not_started | unverified |
| CMD-4b419450ab834a97 | Button { action } label: { view } | not_started | unverified |
| CMD-a72f1aafa46f2183 | Button { action } label: { view } | not_started | unverified |
| CMD-dc308560ae4805bc | Picker("Shape", selection: Binding(get: { session.shapeKind }, set: { kind in                 session.cancelShape()                 session.shapeKind = kind             })) | not_started | unverified |
| CMD-9fe8b6b06c261b47 | Slider(value: Binding(get: { min(200, session.shapeCornerRadius) },                                           set: { session.shapeCornerRadius = $0.rounded() }), in: 0...200) | not_started | unverified |
| CMD-a3629650ca2f9b2e | TextField("Radius", value: Binding(get: { session.shapeCornerRadius },                                                        set: { session.shapeCornerRadius = $0.isFinite ? min(5000, max(0, $0)) : 0 }),                               format: .number.precision(.fractionLength(0))) | not_started | unverified |
| CMD-0bafa59769fabe1a | Button { action } label: { view } | not_started | unverified |
| CMD-cb3c15b2c23f878f | Toggle("Auto Select", isOn: $session.transformAutoSelect) | not_started | unverified |
| CMD-294c10f1f163b172 | Toggle("Show Controls", isOn: $session.showsTransformControls) | not_started | unverified |
| CMD-b22a3ec5f7d5f3f2 | Toggle(isOn: $session.locksTransformRatio) | not_started | unverified |
| CMD-78772d943c224ccc | Picker("Sampling", selection: Binding(get: { value.sampling }, set: { sampling in                     change { $0.sampling = sampling }                 })) | not_started | unverified |
| CMD-2dbe65048ffe8bb0 | Button("Flip H") | not_started | unverified |
| CMD-fc096dcfce1adfb7 | Button("Flip V") | not_started | unverified |
| CMD-86f2b37cc49356fe | Button("Cancel") | not_started | unverified |
| CMD-677b1be06e08ddd3 | Button("Apply") | not_started | unverified |
| CMD-64c0c804c253d830 | TextField(label, text: $text) | not_started | unverified |
| EVT-33dd31054e2c7428 | rightMouseDown | not_started | unverified |
| EVT-7348e35caa17ec01 | mouseDown | not_started | unverified |
| EVT-4a009ca7d11585e3 | mouseDragged | not_started | unverified |
| EVT-a8b3e699ab628b08 | mouseUp | not_started | unverified |
| EVT-2dcae203fea6710d | scrollWheel | not_started | unverified |
| EVT-754a3f3b1bbb2a69 | magnify | not_started | unverified |
| EVT-54b5f9d50114e857 | keyDown | not_started | unverified |
| EVT-dcf4bbd1065d8bce | keyUp | not_started | unverified |
| EVT-84befd690914924b | mouseDown | not_started | unverified |
| EVT-eb985b1d0de90dd2 | keyDown | not_started | unverified |
| EVT-2699cee1c26804d4 | mouseDown | not_started | unverified |
| EVT-69619c614e1ce082 | mouseDown | not_started | unverified |

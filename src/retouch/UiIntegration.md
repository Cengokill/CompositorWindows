# Native retouch integration

`ui/RetouchActions.cpp` supplies `setupRetouchActions`, `refreshRetouchControls`, `beginRetouch`, `updateRetouch`, `endRetouch`, `cancelRetouch`, `publishRetouch`, and `constrainRetouch`. Link `RetouchSession.cpp` through `compositor_retouch` to `compositor_graphics`/`compositor_core`, and the UI source to `compositor_ui`. It compiles against Qt6.8.3 and supported VS2022 with `/W4 /WX /permissive- /Zc:__cplusplus`; the initial isolated invocation missing Qt's required `/Zc:__cplusplus` is retained in `evidence/graphics/retouch-ui-01`, with the successful invocation in `retouch-ui-02`.

The root owns `MainWindow.h`, construction, refresh and pointer routing. The shared additions are:

```cpp
// MainWindow.h includes retouch/RetouchSession.h, QToolBar, QCheckBox.
// EditorProject, so sources and offsets cannot leak between open documents:
retouch::CloneAlignment cloneAlignment;
bool cloneSampleAllLayers{};
// Tool enum: CloneStamp, SpotHealing, Blur.
// MainWindow fields:
retouch::Settings cloneSettings_{}, blurSettings_{retouch::Mode::Liquify};
retouch::Mode healingMode_{retouch::Mode::HealContentAware};
std::unique_ptr<retouch::RetouchSession> retouch_;
EditorProject* retouchOwner_{};
std::optional<Layer> retouchOriginal_;
retouch::Mode retouchMode_{};
bool retouchMask_{};
std::optional<Point> retouchAxisAnchor_;
std::optional<bool> retouchAxisHorizontal_;
std::array<QDoubleSpinBox*,3> retouchTip_{};
QComboBox *healingModes_{}, *blurModes_{};
QToolBar* retouchBar_{};
QCheckBox *cloneAligned_{}, *cloneAllLayers_{};
```

Constructor calls setup; refresh calls refreshRetouchControls. Pointer begin/update/end call the corresponding retouch method before other tools. Cancellation calls cancelRetouch before general gesture cleanup. The toolbar invokes the root's `selectTool`, and is visible only for retouch tools. Canvas-scoped S/J/R shortcuts work with the toolbar hidden and do not intercept letters typed in text or numeric fields. Root owns wider keyboard conventions and tool organization.

The integration retains source tip families: Clone and Smear have separate settings, while Healing uses Brush settings. Alt-click sets a project-local Clone source without changing history, including on an ineligible paint target. Aligned offsets persist between strokes; nonaligned strokes return to the sampled point. Sample All Layers freezes the canonical composite at the start. Shift-click extends the preceding same-target stroke; holding Shift during a stroke fixes the dominant axis after three document pixels, and releasing Shift returns to freehand.

Every stroke retains the original layer and selection, checks the visible ancestor chain, and uses one history transaction. Clone, Healing, Smudge and Liquify require an image target; Blur also supports enabled masks, including group masks. Empty selections prevent edits. Uniform mask blur expands to the source grid for calculation, then keeps the original uniform pointer when unchanged. Cancel restores the immutable document snapshot. Smudge/Liquify display their document working image with the mask's original placement; commit restores source-grid placement and applies selection once. Healing seeds are fresh per real stroke, and the corrected copy-blend C result is used on commit.

`tests/retouch/RetouchUiTests.cpp` drives real toolbar controls and public native-canvas pointer callbacks, with WARP coverage. All six cases pass in `evidence/integration/ctest-release-04.log`. They check history, immutable previews, cancellation, alignment, all-layer sampling, selection, eligibility, mode settings and live warp behavior. They do not prove mouse hit-testing, presented frames, human acceptance or Mac differential equality. The first integrated run exposed two test assumptions: quantized 50% red over green is `(128,127,0,255)`, and a one-pixel-radius stroke endpoint need not have full coverage. The corrected tests check canonical premultiplication and constrained placement; shader tolerances remain unchanged.

Remaining limits: the Clone source crosshair/hover sample overlay is not wired into NativeCanvas; right-drag tip resizing and full source cursor variants remain outside this integration. Healing and the CPU Gaussian run synchronously here, so UI cancellation cannot interrupt the C call. Raster growth and large blank-canvas retouch still need the growing brush session. CoreImage Gaussian and Mac display sampling comparisons remain outstanding.

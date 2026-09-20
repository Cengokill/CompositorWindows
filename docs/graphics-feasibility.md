# Graphics feasibility and C portability

Baseline: `a19db9011282399785dc18efcfded904627bdcc2` (verified locally). This work owns `src/graphics`, `shaders`, `tests/graphics`, and `evidence/graphics`. The original `upstream` checkout remains unchanged. Original MIT copyright and permission text are retained in `src/graphics/upstream/LICENSE`; individual copied-file hashes are recorded in `evidence/graphics/source-provenance.json`.

## Executed result

The current standalone result is **20 native C algorithm cases, 7 CPU coverage cases, 5 HLSL/WARP coverage cases, 17 brush-session cases and 12 stack-renderer cases passing**, built with Visual Studio 2022 Build Tools 17.14.21, MSVC 14.44.35207, Windows SDK 10.0.26100.0, x64. The standalone runners compile C/C++ with `/W4 /WX`; no warnings are disabled. The two WARP session p95 performance gates also pass at the fixed 16.7 ms threshold. These are native case instances, not the upstream test denominator. Full command and evidence:

```powershell
& H:/CompositorWindows/windows/tests/graphics/run-native.ps1 -EvidenceDirectory H:/CompositorWindows/windows/evidence/graphics/native-08-readback-pool
```

Latest raw coverage logs: `evidence/graphics/native-08-readback-pool/{build-settings.json,pixels.test.log,brush.test.log,results.json}`. Session and graph evidence are listed below. The WARP device reports `Microsoft Basic Render Driver`. `warp-crossing.pgm` is the actual 256×256 compute output. Shader compilation uses `cs_5_0`, strict syntax, warnings as errors and optimization level 3, followed by D3D11 dispatch and staging-buffer readback.

The CPU/HLSL comparison threshold was declared before the first WARP execution: at most **1 gray8 value** and **0.0003 absolute permanent-density difference**. Observed maxima are **1** and **0.0000724792**. Float `exp`, `log`, normalization and contraction vary across native CPU/HLSL implementations; final 8-bit quantization can differ by one. Analytic hard-tip values, tile partitions, unaffected pixels, identity operations and state transitions have exact checks. This tolerance is a bounded adapter gate, not a negotiated Mac differential contract.

These results contain **no Mac execution or differential evidence**. They do not certify the source's 288 Swift test functions or state/UI assertions associated with the tests cited below.

## Integration contract

`src/graphics/PixelAlgorithms.h` exposes top-down 8-bit sRGB premultiplied RGBA views and unprofiled gray8 coverage views, each with span extent, width, height and row stride. Views borrow memory; their caller owns its lifetime and supplies channels in the declared order. WIC/Direct2D BGRA must be converted before calling. Padding is ignored and preserved. Extent validation rejects empty dimensions, dimensions above 30,000, arithmetic overflow and short buffers. The higher editor/import/filter layers still enforce upstream operation-specific area and memory limits (for example `Filters.swift:248` uses 100,000,000 pixels).

Use this checked facade for alpha extraction/restoration, alpha bounds, premultiplied clamping, Levels, histograms, Gradient Map, Grain, Noise, Lens Distortion, Wand matching/outlines, Content Fill and Healing. `UpstreamAlgorithms.h` supplies proper `extern "C"` linkage for raw C calls and is intended for the facade. The raw entry points retain caller-validated allocation/parameter preconditions. In-place mutations must operate on a new working image owned by the editor transaction; these helpers do not manage history or cancellation.

`BrushCoverage.h` supplies:

- `BrushTile(width,height)`, bounded to 256×256, with separately owned `permanent` float state and `preview` gray8 snapshot.
- `BrushUniforms`: four 16-byte fields matching the shader. Mapping is `(a,b,c,d)`, geometry is `(tileDocumentOriginX,tileDocumentOriginY,radius,hardness)`, canvas is `(documentWidth,documentHeight,antialiasWidth,depositionSpacing)`. Rendering fills the `counts` field itself.
- `renderBrushCpu(tile,uniforms,newlySettled,tail)` and `D3D11BrushCoverage(shaderPath,preferWarp).render(...)`. Pass **only newly settled segments** per call. Submit the entire replacement tail every call. Empty settled/tail removes the tail. An empty call after settling is idempotent.
- `D3D11BrushCoverage(...,true)` selects WARP. `false` tries hardware then WARP. Construction/dispatch failures throw; the caller can rerun the operation through `renderBrushCpu`. One adapter instance is used serially on its owning thread.

A tile belongs to one stroke with fixed hardness/settings. Soft permanent state is optical density, hard permanent state is coverage. Radius is diameter/2; upstream deposition spacing is `max(0.25, diameter*(hardness>=1 ? 0.015 : 0.025))`. Antialias width is `max(0.001,min(length(mapping.xy),length(mapping.zw)))`. Tile origin is the origin after mapping into document coordinates. Segments are document coordinates. Pixels outside the document return zero preview while retaining existing permanent state, matching the source shader.

Brush output is **coverage only**. The editor applies the stroke's color/opacity/erase mode against the original immutable raster once per preview, along with selection coverage. Feeding previews repeatedly into already painted pixels would break the upstream opacity cap. Snapshot copying is tested for this adapter; the complete document snapshot/history boundary remains the lead's integration responsibility.

CMake source requirements (build files are owned by the lead):

```text
C11: src/graphics/upstream/{AdjustPixels,BrushPixels,ContentFill,HealPixels,LensPixels,LevelsPixels,NoisePixels,WandPixels}.c
C define: _USE_MATH_DEFINES
C++20: src/graphics/{PixelAlgorithms,BrushCoverage,D3D11BrushCoverage}.cpp
Include root: windows/src
Windows link libraries: d3d11 d3dcompiler dxgi
Test main 1: tests/graphics/PixelAlgorithmsTests.cpp
Test main 2: tests/graphics/BrushCoverageTests.cpp
Test 2 arguments: absolute BrushCoverage.hlsl path, optional evidence directory
```

The shader is a runtime asset and must be deployed with the executable. The standalone runner uses `/MD`, matching Qt's shared MSVC runtime. `BrushCoverageTests` without its shader argument runs only CPU cases and explicitly skips WARP; use the argument for the executed 11-case suite.

## C audit

| Module | Finding and port action | Evidence/remaining boundary |
|---|---|---|
| AdjustPixels | Byte-identical C/header. `uint32_t` hash overflow is intentional. Grain floors coordinates into `int64_t`; facade rejects nonfinite/unbounded settings before conversion. Gradient tables cannot alias output pixels. | Exact tile-global grain and alpha-preserving Gradient Map tests. |
| BrushPixels | Three C4244 sites now cast to `uint8_t` after bounded integer arithmetic: unpremultiply clamps at 255, restore multiplies two bytes and divides by 255, alpha originates from a byte. | All 256 alpha values round-trip exactly; stride/bounds/clamping cases. |
| ContentFill | Byte-identical C/header. Signed `int` linear indices and `x*4` remain safe under 30,000-per-axis facade limit; allocations multiply in 64-bit `size_t`. Failure returns are surfaced. | Constant fill, no donor, unchanged unselected pixels; source repeating-stripe criterion >=114/120 matches. Large memory/time and cancellation remain editor concerns. |
| HealPixels | Upstream `long` is 64-bit on macOS and 32-bit on Windows. All signed working indices and bounds are now `int64_t`, including header bounds output; `labs` becomes `llabs`. This prevents latent `p*4`, working-box and recursive-grid overflow if a raw caller supplies very large inputs. | Three modes satisfy upstream blemish-color criterion and exact unchanged-outside-coverage assertion. Huge working-box allocation is untested; normal upstream filter limits are lower than the overflow boundary. |
| LensPixels | Byte-identical C/header. Facade bounds dimensions and finite `k` to [-1,1], keeping floored source positions and adjacent samples inside Windows `long`. Source/destination overlap is rejected; equal strides are required by C ABI. | Exact identity at 30,000×1 and signs/corner alpha tests. |
| LevelsPixels | Byte-identical C/header. Facade validates finite normalized tables; runs C per row for padding and per-row mask coverage. | Exact identity, weighted histogram sums; table/output overlap rejected. |
| NoisePixels | Byte-identical C/header. Index hash explicitly converts to uint32_t; native width×height facade maximum is below 2^32. Distribution flags are booleans, amount is finite and bounded. | Seed repeatability, both distributions, mono channels and alpha. Subimage calls have local coordinates; callers needing full-image noise must retain source indexing rather than treat it as tile-global Grain. |
| WandPixels | `unsigned long sums[4],samples` changed to `uint64_t`: 32-bit sums overflow for >16,843,009 white samples. Result `long` is bounded by <=900,000,000 pixels. Dimensions/radius prevent size_t seed-radius overflow; trace coordinates stay in int32_t. | Regression uses 4,105² white pixels (16,851,025): exact all-selected result. UI upstream sample radii are only 0/1/2, so this is raw API robustness. Independent winding reconstruction verifies holes and corner contacts. |

Alpha extraction and restoration reject overlapping image/mask views; lens rejects any input/output overlap. Gray coverage is packed for Healing/Wand because their C APIs require contiguous rows. Allocation results are owned by RAII and freed. Most functions support caller-owned row padding explicitly. No byte-order reinterpretation is performed.

## Shader fidelity and lifecycle

`shaders/BrushCoverage.hlsl` translates the actual embedded `MetalBrushCoverage.swift` implementation: four affine coefficients, pixel centers, document clipping, hard-tip segment distance and antialiasing, the truncated Gaussian tip, optical-density accumulation, **eight-point Gauss-Legendre quadrature**, separately accumulated permanent/tail data, density clamp 20, and gray8 quantization. HLSL stores uint preview values then copies the low byte into the gray8 vector, avoiding racing packed-byte UAV stores. HLSL `round` uses ties-to-even; `floor(nonnegativeValue+0.5)` implements the source Metal halfway-away rule. The hard-boundary value 127.5→128 is checked exactly.

The CPU route is a software implementation of the same continuous integral. It is independently executable without D3D11. It is **not a translation of the upstream CoreGraphics dab fallback**, whose gradient rasterization remains a separate Mac-reference obligation. The tests preserve the observable soft-intersection and sparse/dense criteria from `BrushIntersectionTests.swift`.

The final WARP adapter shares a segment buffer across a batch and reuses 16 GPU/staging slots (approximately 16 MiB). It dispatches a chunk before mapping any result. Up to 64 CPU readback slots are cached (at most 32 MiB); larger batches use temporary CPU slots. All reads complete and the device status is checked before any caller-owned tile state is committed. Duplicate tile pointers are rejected before execution. CPU state remains authoritative, so retained GPU resources are staging/work resources rather than unique document ownership. Asynchronous submission, explicit cancellation and device-loss injection remain unverified.

## Performance and missing acceptance

Latest microbenchmark: one 256×256 tile in a 4,000×4,000 document, radius 260, one new segment, WARP, 5 warmups + 20 measured iterations, including remaining allocation/upload/dispatch/readback: **p50 0.5025 ms, p95 0.6069 ms**. These are host-local feasibility timings, not 4K pointer-to-present latency. Full session/commit measurements are below; UI presentation, process-wide RAM/VRAM peaks and concurrent tabs remain unmeasured here.

| Case family | Native coverage | Remaining acceptance |
|---|---|---|
| P047 Wand | Matching, sample averages, per-channel tolerance/alpha, connected selection, loops/winding | Session selection composition, current/all-layer sampling, UI, Mac references |
| P054/P055/P056 brush | Integral, soft crossings, event density, tail replacement, 256 tile partition, CPU/WARP, 30k document coordinates | Actual event splines, color/opacity/erase, complete snapshots/commit, GPU loss, hardware GPU, CoreGraphics fallback comparison |
| P058 Healing | Three C modes, seed-driven fixture, coverage and unaffected pixels | Tool transaction, placement, preview/cancel/undo, Mac reference |
| P069/P073/P074/P079/P080/P081 | Analytic C behavior for Levels/Gradient Map/Grain/Noise/Lens/Content Fill | Full UI/adjustment stacks, operation color pipeline and Mac output |
| P094 | One tile microbenchmark plus actual 4K two-stroke session/commit timing and materialization counter | UI pointer-to-present latency, process RAM/VRAM and concurrency |

Earlier failed runs are retained. `native` stopped on a missing test `<string>` include; `native-02` stopped on a test integer-to-byte `std::fill` warning; `native-03` exposed legacy HLSL loop-variable scope and an invalid 9×9 donor fixture (every candidate 5×5 donor patch touched the selected center). The shader loop names were made unique and the positive donor fixture expanded to 13×13; the no-donor assertion remains. `native-04` passed the initial suite; `native-05` includes alias guards, the explicit LLP64 sample regression and the upstream repeating-texture assertion. No output threshold was loosened.

## Brush session integration

`BrushSession.h/.cpp` now supplies `BrushSession(original,settings,optionalD3D11Adapter,optionalSelection,geometry)`, `begin(Point)`, `append(Point)`, `preview()`, `commit()` and `cancel()`. It depends on the lead-owned `core/Document.h` Raster. Settings contain radius, hardness, opacity, straight sRGB color bytes and erase. `BrushSessionGeometry::forLayer(transform,sourceWidth,sourceHeight,canvasWidth,canvasHeight)` computes the affine mapping for rotated/nonuniform/flipped sources. Selection coverage is a document-grid `GrayRaster`; absent means unrestricted and all-zero means no painting. Selection sampling is nearest in document pixels and remains uncalibrated for transformed fractional mask interpolation.

The source's last-four-event retention, initial click, centripetal Catmull-Rom knots, adaptive 0.2-document-pixel chord criterion/depth 10, provisional tail and flush sequencing are translated directly. Each changed tile is reconstructed from the original stroke raster and `coverage × opacity × selection`, preserving stroke opacity and eraser premultiplication. Each preview has independent immutable tile pointers; unchanged tiles share the original storage. All pending coverage/pixel allocations complete before publishing a new snapshot. An accelerator failure falls back to CPU against the unmodified pending state. Commit is immediate, idempotent and leaves no asynchronous edit against an older document. The caller installs its returned raster in one history transaction, after checking that layer/source/settings still match.

The session operates within its existing raster extent. Upstream imported-layer expansion, soft-edge crop metadata and independently placed grayscale mask targets remain to integrate. Calling `preview()` itself only returns a shared pointer. The session never flattens its source.

`tests/graphics/BrushSessionTests.cpp` passes 17 cases: eight each on CPU/WARP, plus CPU/WARP final-image comparison. Tests cover immutable previews/next stroke, source sharing, opacity cap, erase, cancel/empty selection, selection multiplication once, sparse curved samples, exact tail ghost removal, transformed circular brush, and the source 800px ridge assertion. Latest logs are `evidence/graphics/session-06-readback-pool`; the initial CPU/WARP baseline is retained in `session-02-benchmark`.

Reproduction:

```powershell
& H:/CompositorWindows/windows/tests/graphics/run-session.ps1 -EvidenceDirectory H:/CompositorWindows/windows/evidence/graphics/session-02-benchmark -Benchmark
```

The benchmark executes the exact `BrushPerformanceTests.fourKInteractiveStroke` path at diameter 800: 4,000×4,000, start `(700,3200)`, 120 samples up to `(700,800)` then right to `(3100,800)`, immediate commit, then an immediate second stroke. Both transparent and opaque originals run on CPU and WARP. Unlike the upstream benchmark, this measures the session and published snapshot only; it excludes UI compositing/presentation. All four scenarios record zero **actual core `Raster::rgba` calls** using the lead's atomic instrumentation.

| Backend | Original | Append+preview p50 / p95 | Commit pass 1 / 2 |
|---|---|---|---|
| CPU | transparent | 139.357 / 147.103 ms | 85.9853 / 84.1597 ms |
| WARP | transparent | 24.6969 / 29.614 ms | 20.9843 / 20.2826 ms |
| CPU | opaque | 138.202 / 145.191 ms | 83.6268 / 83.4716 ms |
| WARP | opaque | 24.3585 / 28.3227 ms | 20.8905 / 19.1482 ms |

Each stroke touches 92 coverage tiles (30,146,560 bytes live coverage); 166 then 183 of the 256 raster tile entries remain shared from the prior input. Across two passes, 4,352 coverage tile copies and 2,637 output tile copies were allocated. These figures establish why the initial per-tile dispatch/readback path needs optimization. The lead fixed a **16.7 ms p95** session target after receiving these baseline figures; this target remains unmet in the retained baseline.

The final implementation batches through reusable buffers, converts finite nonnegative paint values with equivalent half-up byte rounding instead of per-channel `lround` calls, and skips pixel recomposition when coverage is byte-identical to the preceding preview. That skip follows directly from immutable stroke settings, selection and input pixels. It also reuses CPU readback vectors rather than copying input density into memory immediately overwritten by GPU output.

`session-03-batch`, `session-04-rounding` and `session-05-coverage-delta` retain the intermediate performance misses. Latest `session-06-readback-pool` executes the same workload and records explicit passing performance gates:

| WARP original | p50 | p95 | Maximum event | Commit pass 1 / 2 |
|---|---|---|---|---|
| transparent | 11.8492 ms | **14.946 ms** | 18.9109 ms | 7.7145 / 6.4207 ms |
| opaque | 11.0098 ms | **13.4924 ms** | 15.1951 ms | 6.9002 / 6.9662 ms |

Both are below the fixed 16.7 ms p95 target; the transparent maximum is above one 60 Hz interval. This is one measured local run per scenario, excluding UI Present. Tile allocation/sharing counts and zero instrumented `Raster::rgba` materializations remain unchanged. The stage profiler separately records preparation, compute/readback and composition times in the executable log. The software-only 147/145 ms baseline remains a disclosed performance limitation; it was not rerun after composition optimization, and no new CPU speed claim is made.

```powershell
& H:/CompositorWindows/windows/tests/graphics/run-session.ps1 -EvidenceDirectory H:/CompositorWindows/windows/evidence/graphics/session-06-readback-pool -WarpBenchmarkOnly
```

`native-08-readback-pool` verifies the final batching/readback code, including 19 mixed odd/full-size tiles across the 16-slot boundary, another tail pass with cache reuse, and duplicate-tile rejection. The frozen pixel/density limits still pass unchanged. `native-06-batch` retains a build failure from unnecessary host `alignas(16)` padding; the 64-byte uniform field layout is now explicit without imposing CPU address alignment that D3D11's byte-copy API does not require.

## Canonical clipping-stack adapter

`StackRenderer.h/.cpp` implements `IRasterBackend` with `render(Document,x,y,width,height)`. The lead owns routing `SoftwareRenderer` and build configuration to this implementation. The renderer traverses source-style hierarchy order, discovers contiguous visible sibling stacks, blends children against the opaque base colors, and restores the saved base alpha exactly once before compositing the stack in its base blend mode. Independent links and hidden source alpha retain the source's separate dependency behavior. Folder masks apply per visible descendant; the same parent mask clips the final stack once. A source's ancestor folder masks/visibility do not enter the separate dependency-alpha context, matching `LiveMaskRenderer.coverage` and `ImageExporter.drawOwn`.

An optional `AdjustmentCallback(layer, currentRegionRaster, RenderRegion)` supplies the raw effect result. The renderer handles its scope, mask, opacity and blend mode; non-Normal adjustment blending happens on unpremultiplied opaque colors before restoring original alpha. Clipped adjustments operate inside the stack. A visible adjustment without a callback throws rather than silently disappearing. Specific adjustment formulas and the callback's halo/region behavior are owned by the effect provider.

`tests/graphics/StackRendererTests.cpp` passes 13 cases with `/W4 /WX`; latest logs are `evidence/graphics/stack-03-shared-sampling` (earlier runs retained). These include the source soft-alpha sequence `[255,128,32,0]`, translucent child alpha preservation, white-background fringe prevention, hidden source chains, shifted source masks, noncontiguous links, hierarchy versus raw storage order, folder masking, effect scope, tile-region equality and explicit placed-mask background.

`RasterSampling.h` exposes the canonical `sampleRaster`/`sampleGray` functions to resize, retouch and layer-bake clients. Nearest sampling is explicit; Smooth/High currently share bilinear sampling. `MaskSampling.h` provides placed-mask exterior from the source edge-majority threshold, with a bounded weak-reference cache. Folder/adjustment clips retain layer-placement/exterior-zero behavior from source. An optional supplied thumbnail makes the majority rule exact. The core does not retain source thumbnails, so masks over96 pixels use area-downsampled edge values; CoreGraphics high-quality thumbnail reductions can differ near the black/white majority threshold. CoreGraphics Lanczos reductions, geometric antialiasing, High interpolation and separately placed mask resampling require Mac comparison and are not certified by these graph tests.

Independent lead-core review evidence is preserved in `evidence/graphics/core-review-01`, including the reviewed source snapshot and executable probe. The visible clipping regression returned `(128,0,64,192)` for a half-alpha blue base plus opaque red clipped child; StackRenderer now returns `(128,0,0,128)`. Lead-owned findings also covered stride rejection (already resolved at capture), malformed tile storage, `filled` premultiplication and selection memory omitted from history accounting. Their fixes remain the lead's responsibility and must be rerun against current core before closure.

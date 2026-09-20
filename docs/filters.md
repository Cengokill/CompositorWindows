# Source filters and current verification

`src/filters/PixelFilters.h/.cpp` exposes the immutable `Raster`/`Transform` filter boundary. Link `compositor_core` and `compositor_graphics`; no Qt or image decoder is required. The supported kinds are Gaussian Blur, Motion Blur, Add Noise, Lens Correction and Content-Aware Fill. Unsupported kinds, absent fill selection, invalid coverage, excessive dimensions, excessive estimated working storage and cancellation produce exceptions. A caller commits a successful result once through document history; this module never mutates a document or retains a worker result.

The behavioral baseline is Compositor `a19db9011282399785dc18efcfded904627bdcc2`. Upstream source and tests remain untouched.

| Boundary | Source evidence | Windows behavior |
|---|---|---|
| Settings and ranges | `Compositor/Document/Filters.swift:30–79` | Radius 1, range 0.1–250; angle 0, range −90–90; distance 10, range 1–2000; amount 10, range 0.1–400; distortion 0, range −100–100. Nonfinite values use source defaults. |
| Direct raster job | `Filters.swift:81–89,121–176` | `runPixels` receives an already prepared source, preview scale and stable noise seed. Transparent exterior; premultiplied RGBA8. |
| Gaussian and motion parameters | `Filters.swift:114–147` | Gaussian sigma = radius × scale. Motion sigma = distance × scale / √12; angle is counterclockwise from horizontal, so +45° extends up-right/down-left on a top-down raster. |
| Noise and lens | `Filters.swift:149–168` | Existing pinned C `noise_add` and `lens_distort` wrappers are called directly. Lens coefficient = distortion / 100 × 0.35. |
| Selection blend | `PixelAdjust.swift:22–45` | Coverage × changed + (1−coverage) × original, no color transform. Integer rounding to nearest; endpoint pixels exact. Absent selection unrestricted, present all-zero selection a no-op. |
| Growth and placement | `Filters.swift:226–265` | Gaussian margin = ceil(radius × 3 + 2); motion margin = ceil(distance / 2 + 2). `retainedBlurMargin` preserves a panel's largest previous margin. Content fill extends to selected source coverage bounds. Result preserves rotation, flips, sampling and fractional source-pixel placement. |
| Preview and full commit | `Filters.swift:269–282,377–434` | Gaussian/Motion/Lens previews have longest side ≤2048. Noise and fill use full resolution. Full blur commit trims alpha-zero bounds after selection blending; an entirely clear image retains its grid. |
| Fill | `Document/ContentFill.swift`, `Rendering/ContentFill.c` | Exact existing C patch synthesis. Nonzero coverage marks synthesis targets, and coverage blends the synthesized result afterward. A selection with no unselected opaque donors fails. |

`SourceSelection` is a gray raster already mapped into original source pixels, with an origin that can extend beyond the source. The caller clips document selection to the canvas before mapping it. Preview coverage is sampled from this supplied grid. The Mac implementation rerasterizes the selection path at preview resolution; antialiased selection-edge equivalence remains unmeasured.

The software Gaussian kernel is separable with normalized samples through three standard deviations and a float intermediate. Motion uses a directional Gaussian, bilinear premultiplied samples and the same three-sigma truncation. These are explicit CPU implementations. Core Image's kernel, support, resampling and rounding have not been captured from a Mac. P077 and P078 raster parity remain open; the analytic kernels are not accepted substitutes for a completed differential comparison.

Working storage is estimated before allocation, including full edge tiles for thin images and simultaneously retained grids. The default limit is 1.2 GB; source limits remain 30,000 pixels per side and 100 million pixels. A legal document can exceed a particular operation's working budget and produce a clear failure. Blur polls cancellation within rows. The upstream C noise/lens/fill routines cannot interrupt their inner loops; cancellation before/after execution discards the result, so cancel latency includes the remaining C call. This matches source result-discard behavior but is an explicit responsiveness limitation.

## Reproducible checks

Run `windows/tests/filters/run_checks.ps1` after the Debug core/graphics libraries have been built. An alternate library directory can be passed with `-BuildDirectory`. It compiles `filter_checks.cpp`, runs functional checks and a benchmark, then executes the independent `--source-parity` case. `tests/filters/results/results.json` records functional success separately from the actual nonzero parity exit.

Verified functional checks include source setting normalization; all three upstream motion direction assertions; impulse spread within 0.3 pixels of distance/√12; stable/different seeds; uniform/Gaussian and monochrome/color noise; exact C noise/lens byte comparison; opaque/transparent lens corners and center; solid-background fill; all 120 of 120 repeating-texture pixels (source threshold ≥114); no-donor and no-selection failures; fill beyond the original grid; coverage endpoints and half coverage; premultiplied output; rotated/flipped/fractional growth and trim placement; retained preview margin; 2048 preview dimensions and full-size noise preview; neutral/empty no-op identity; budgets; malformed selection; unknown kind; pre-cancel and in-flight Gaussian cancellation; immutable inputs.

The upstream Gaussian border assertions in `CompositorTests/FilterTests.swift:29–31` are retained as an actual failing parity case. With its 40×20 half-opaque fixture and radius 3, Windows full growth/filter/trim produces a 36×36 image with alpha(0)=1, alpha(20)=203 and alpha(38)=0 at row 10. The first upstream assertion expects 255, despite `Filters.swift:126–128` explicitly choosing unclamped exterior and later growing/trimming the raster. This is a source assertion conflict, not a successful parity result. Mac execution is required to resolve it.

## Measured CPU cost

On Intel Core Ultra 9 285K, Windows x64, MSVC 19.44, filter code `/O2`, Debug core/C wrappers, 512×512 opaque source, no worker concurrency, measurement surrounds `apply` including growth, filter and final trim. Four sequential operations were measured, first cold and three warm. Warm elapsed times: Gaussian radius 3, 68.6–74.1 ms; Motion distance 16, 278.9–283.5 ms; Noise amount 10, 24.0–24.2 ms; Lens distortion −50, 22.3–22.5 ms. Raw observations are in `tests/filters/results/benchmark.log`.

This small fixture does not establish responsiveness on large documents or maximum slider values. Motion's sampled kernel grows linearly with distance and Gaussian with radius; these fallbacks require asynchronous preview/cancellation in the UI. GPU acceleration, peak-memory measurement, repeated large-image behavior, Mac raster differences and UI transaction validation remain open gates.

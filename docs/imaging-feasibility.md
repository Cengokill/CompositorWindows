# Imaging feasibility and acceptance evidence

Baseline: Compositor `a19db9011282399785dc18efcfded904627bdcc2`, verified with a command-scoped Git safe-directory exception. The upstream checkout was read only. Native checks ran on Windows x64 with MSVC 19.44.35221, Windows SDK 10.0.26100.0 and an Intel Core Ultra 9 285K (24 logical processors). These results establish working local implementations and bounded experiments. They do not establish complete Mac pixel parity or acceptance of the foreground replacement.

The implementation is in `src/imaging`. `image_types.h` owns top-left premultiplied RGBA8 sRGB buffers with explicit dimensions, stride and storage. Gray masks contain coverage bytes only. Invalid premultiplication, short storage, dimensions beyond 30,000, and more than 100 million pixels are rejected. `ImportOptions` carries remaining document pixels, a transient buffer budget and a thread-safe non-throwing cancellation callback. The inference runtime's internal allocations are currently measured separately; they are not covered by the buffer budget.

| Interface | Sources and links |
|---|---|
| `WicCodec::decode`, `encode` | `wic_codec.cpp`; `windowscodecs`, `ole32`, `oleaut32`, `propsys` |
| Strict project PNG color/mask read and gray write | `project_png.cpp`, same links |
| `HeifCodec::decode` | `heif_codec.cpp`, pinned `heif.lib` plus dynamic `heif.dll` and `libde265.dll` |
| `boxMean`, `guidedFilter`, `refineSubjectMask`, `applySubjectMask` | `subject_matte.cpp`, standard C++20 |
| `ISubjectMaskProvider`, `OnnxSubjectProvider` | `onnx_subject_provider.cpp`; ONNX Runtime 1.30.0, `bcrypt` |
| `showSubjectDialog` | `SubjectDialog.cpp`; Qt 6 Widgets and Concurrent; cached base mask, background workers, Basic/Advanced controls, cancellable preview and full-size Apply |

The core imaging interfaces do not require Qt. The separate dialog returns an optional committed mask; history and document mutation remain with its caller. Workers return value results and never touch widgets. Cancel waits for the current worker after requesting cancellation. The model load itself is synchronous inside that worker and can briefly delay final dialog destruction.

## Codecs

WIC accepts PNG/JPEG/TIFF by container content, decodes the first frame, normalizes an embedded profile through its color transformer, applies EXIF orientation once, and explicitly premultiplies straight RGBA output. No-profile inputs report `AssumedSrgbNoProfile`; the decoder does not report a nonexistent embedded profile as converted. Unprofiled CMYK is rejected. PNG export writes an explicit sRGB rendering-intent chunk. JPEG/TIFF export embeds the system sRGB profile and fails if it is unavailable. JPEG flattens against the chosen RGB matte before encoding; quality is clamped to [0,1]. Exports use a temporary sibling and replace the destination only after encoding succeeds.

Strict project PNG paths require a PNG signature, an IHDR bit depth of at most eight, and no APNG animation control chunk. Masks require grayscale without an alpha channel or tRNS transparency; coverage bypasses color management. Project assets bypass import orientation. The persistence worker also verified exact RGBA and mask exchange through these adapters.

HEIC is decoded directly through built libheif 1.23.4 / libde265 1.1.3. External plugin loading, x265 and all unrelated encoders/decoders are disabled. The code applies libheif container transformations, checks dimensions before full decode, enables strict decoding and per-context limits, and connects cancellation. NCLX conversion uses libheif's sRGB output contract; an ICC profile is normalized with WIC. Associated alpha is made straight before ICC conversion and explicitly premultiplied afterward. A clean Windows installation has not been exercised, but this native execution does not call a WIC HEIC decoder or Store codec. The executable's import table is retained in `evidence/imaging/heif-dependencies.log`.

Four native HEIC cases succeeded: the real example photograph (1280×854), alpha fixture (512×512), odd-size color fixture (451×461), and cropped fixture (64×64). These are from the pinned libheif tree. Their output PNGs and per-case logs are in `evidence/imaging`. Broader HEIC HDR/ICC/EXIF combinations and a clean-machine run remain unverified.

## Foreground provider and source mapping

The selected checkpoint is the author's [`ZhengPeng7/BiRefNet_lite`](https://huggingface.co/ZhengPeng7/BiRefNet_lite/tree/aa62cd87eafb9cc43056d08ef3615a14628b831d), pinned at `aa62cd87eafb9cc43056d08ef3615a14628b831d`. The model card declares MIT and explicitly says the repository contains official model weights. The declaration and the author's full MIT notice are retained in `dependencies/imaging/notices`. This checkpoint is a smaller Swin-T variant from the official BiRefNet family, not Apple's Vision model.

The downloaded `birefnet.py`, configuration, and active inference path were inspected before execution. The active constructor disables backbone pretraining and thus external checkpoint retrieval. Dynamic class construction uses fixed strings from the pinned Config class. Training-only `torch.load` paths are not used. `convert_birefnet.py` loads the audited local module and safetensors directly; it does not invoke `trust_remote_code` or execute a downloaded notebook. It asserts the checkpoint and implementation SHA256 values before loading. The conversion script maps the inspected torchvision DeformConv argument order to the standard ONNX operator and exports fixed `[1,3,1024,1024]` input / `[1,1,1024,1024]` sigmoid output at opset 20.

| Upstream source | Implemented behavior |
|---|---|
| `GuidedMatte.swift:9–63` | Float box means with replicated edges, covariance/variance filter, epsilon 1e-4 |
| `GuidedMatte.swift:68–118` | Gray guide, quantization and resize stages; preview longest-side limit 1400; full-size Apply |
| `SubjectRemoval.swift:44–81` | Actual execution order **Refine → Shift Edge → Contrast**, despite a different order in the introductory prose |
| `SubjectRemoval.swift:86–95` | Existing mask multiplied after refinement |
| `SubjectRemoval.swift:100–110` | Preview applies coverage to all premultiplied channels |
| `Filters.swift:52–71`, `FilterSheet.swift:33–44` | Basic default; Advanced defaults Refine 12, Contrast 25, Shift 0; ranges 0–40, 0–100, −10–10 |

The guide currently uses explicit gamma-domain Rec.709 coefficients, resizing uses bilinear interpolation, and Shift Edge uses a separable Gaussian truncated at three sigma. Apple's DeviceGray conversion, high-quality resampler and Core Image Gaussian need Mac fixtures before these stages can be called numerically equivalent. The box/filter arithmetic and control ordering are translated; the surrounding native raster operations remain an explicit comparison gate.

Native preprocessing unpremultiplies source RGB, resamples to 1024 square and applies ImageNet mean/std. Transparent pixels provide zero RGB. Its bilinear implementation differs from Pillow's antialiasing for larger source images; the small photographs establish native operation, not preprocessing equivalence at every input size. A mask whose maximum probability is below 0.5 produces a no-subject error. That threshold is a replacement behavior, not a claim about Vision's instance detector.

## Results and limitations

`tests/imaging/run_checks.ps1` passed PNG/TIFF exact premultiplied round trips, JPEG opacity/DPI/matte/quality, odd padded strides, a 30,000×1 image, allocation/cancellation rejection, export preservation on cancellation, all eight TIFF orientation mappings, JPEG orientation 6, embedded ICC normalization, strict PNG rejection cases, exact gray mask persistence, box-reference arithmetic, guided constant masks, control order and existing-mask multiplication. Required fixtures have a SHA256 manifest. Native tests fail when fixtures are absent; a deliberate missing-fixture run returned failure. Color normalization and JPEG matte tolerances were fixed at two byte levels before running the corresponding comparisons.

`tests/imaging/run_native_checks.ps1` passed four HEIC decodes plus C++ CPU foreground inference and full-size Advanced refinement on two photographs. The NASA astronaut is public domain; Chelsea the cat is CC0 by Stefan van der Walt. Source declarations from scikit-image v0.25.2 are saved as `evidence/imaging/fixture-source.py` (astronaut lines 382–401; Chelsea lines 890–905). The ICC fixture uses the CC0 Compact-ICC-Profiles Adobe-compatible profile, with its exact revision, hash and license retained.

`model-results.json` records Python ONNX CPU inference and the conversion comparison. On the astronaut tensor, maximum absolute probability difference versus PyTorch was 0.00019205 and mean difference was 0.0000000813; both passed the frozen 0.002 / 0.00002 limits. A second export reproduced the exact ONNX SHA256 `c0faf38f5504f2239f1e6481ce4ac166b17435b38ea35e480d811a47bc1aba80`. The native constructor rejects a different model hash.

With the native CPU arena disabled, measured inference was about 2.18–2.20 seconds for the 512×512 astronaut and 451×300 cat (both use a 1024 model input). Peak process working set was about 1.47 GB and retained private commit after inference/cancellation was 274–288 MB. Actual ONNX in-flight cancellation returned in about 0.22–0.23 seconds. Earlier arena-enabled measurements were faster (about 1.7 seconds) but retained approximately 4.7 GB of private commit; those logs remain saved. These are single-machine experiment timings, not latency percentiles or low-memory acceptance. Hard inference memory limits and full-resolution refinement beyond its explicit allocation budget remain open.

Visual review of the astronaut output shows a useful subject silhouette and a pale rim around parts of the hair and suit. The cat crop contains little surrounding scene and does not establish difficult fur or whisker separation. There is no annotated alpha ground truth or Vision output for either image. The two-photo experiment does not measure general segmentation quality, failures on ambiguous subjects, transparent objects, disconnected instances, or production suitability. Full foreground replacement acceptance remains external.

The initial ONNX Runtime 1.23.2 attempt failed because its CPU build lacked DeformConv. `model-conversion-opset20-failed.log` retains that result. The selected 1.30.0 official release supports the required CPU kernel. Initial WIC export and one test's incorrect half-rounding expectation were corrected without weakening pixel gates; the surviving successful scripts rerun the final implementation.

## Reproduction and distribution

Run `scripts/bootstrap-imaging.ps1` from the Windows source tree. It verifies `dependencies/imaging/lock.json`, acquires pinned sources/assets, builds dynamic codec libraries with MSVC 2022, extracts official ONNX Runtime, installs the version-locked Python 3.12 conversion environment locally, generates required synthetic fixtures, and creates/verifies the model. The bootstrap was rerun successfully in this workspace; the model was then independently re-exported with an identical hash. A fresh machine reconstruction remains a separate packaging gate. Python and downloaded source modules are build-time dependencies; the application uses native ONNX Runtime and the bundled graph without a first-use network request.

Verification commands are `tests/imaging/run_checks.ps1`, `tests/imaging/run_native_checks.ps1`, and `tests/imaging/run_dialog_checks.ps1`. The dialog check uses Qt's offscreen platform to exercise Cancel and Advanced Apply, required slider ranges, source preservation, and a full-resolution committed mask combined with an existing mask. Offscreen tests do not establish desktop focus/DPI/accessibility acceptance.

The binary bundle needs `heif.dll`, `libde265.dll`, ONNX Runtime's two runtime DLLs, and `model/birefnet-lite.onnx`, alongside the UI dependencies. Retain the full notices directory and corresponding `libheif-1.23.4-source.tar` and `libde265-1.1.3-source.tar` generated from the pinned commits. The codec libraries are LGPL-3.0-or-later; keep them replaceable and include their corresponding source/build materials. BiRefNet weights/code and ONNX Runtime carry MIT declarations; ONNX Runtime's complete third-party notices are included. Packaging must verify these artifacts independently. No release was published.

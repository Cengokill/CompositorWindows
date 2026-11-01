# Building the packaged source

The preview is a Windows port of [Compositor by Robbie Tilton](https://github.com/robbietilton/Compositor), pinned to `a19db9011282399785dc18efcfded904627bdcc2` (1.0.4). Upstream copyright and MIT notices are preserved. The port does not imply upstream endorsement or verified full Mac parity.

The source ZIP accompanying each download is identical to `sources/CompositorWindows-source.zip` inside its runtime payload. It contains production code, native test sources and frozen reference code, build/package scripts, dependency locks/notices and user/build instructions. Generated results, dumps, compiled binaries, development signing keys and historical coordination reports are excluded. The demo images, reusable project, export and generator are supplied in a separate demo ZIP; recording files are separate release assets. The package manifest records the source ZIP's SHA256 and source revision; the public release identifies the corresponding commit/tag.

Use Windows 11 x64, PowerShell 7, Visual Studio 2022 C++ tools with MSVC 14.44, Windows SDK 10.0.26100.0, CMake 3.31.6 or later, and Python 3.12 for the offline-model conversion. Extract the source ZIP into an empty folder. Retain the matching portable payload, because the four recorded codec/ONNX runtime DLLs include build timestamps and do not promise byte-identical rebuilding.

```powershell
. ./scripts/bootstrap.ps1 -Python C:\Python312\python.exe -RuntimeDirectory C:\Extracted\CompositorWindows
cmake --preset windows-x64-release
cmake --build --preset windows-x64-release --target Compositor --parallel 4
```

Substitute actual paths. Bootstrap acquires the locked Qt/dependency inputs, builds headers/import libraries and converts the foreground model, then restores the four verified DLLs from `RuntimeDirectory`. The directory must contain `heif.dll`, `libde265.dll`, `onnxruntime.dll` and `onnxruntime_providers_shared.dll`. A prepared dependency cache supports `-Offline`. Initial setup needs network access and the pinned Python conversion dependencies. No Python is needed to run the packaged editor.

`restore-imaging-runtime.ps1` validates all four source hashes before changing any destination. It preserves differing previous files beside their original paths and leaves verified files unchanged. The recorded locks and system settings are retained. Corresponding Qt/libheif/libde265 sources are included beside the application-source archive. Users may build modified libraries, replace the application-local DLLs, and run `Compositor.exe` directly.

For MSI/ZIP reproduction, follow [packaging.md](packaging.md). The build recipe and pinned inputs are reproducible; executable timestamps, archive timestamps and MSI identifiers can differ. Rebuilding from the source archive is a separate check from reproducing the full historical parity report.

## Run and test

The executable is `build/release/Release/Compositor.exe`. In the bootstrapped shell, deploy Qt with `dependencies/qt/bin/windeployqt.exe --release build/release/Release/Compositor.exe`. Place `models/birefnet-lite.onnx` and `shaders/BrushCoverage.hlsl` beside the executable in those relative directories; the matching portable payload contains them. The package script assembles these assets automatically.

Build the test targets with `cmake --build --preset windows-x64-release --parallel 4`, then run `ctest --preset windows-x64-release --output-on-failure`. Use `-R <pattern>` for the tests affected by your change. Interactive tests that require an active desktop session are tagged with `LABELS "interactive"`. Exclude them with `ctest --preset windows-x64-release-noninteractive --output-on-failure`, or with `ctest --preset windows-x64-release -LE interactive --output-on-failure`. Run only those tests with `-L interactive`. `focus_stack.performance` (label `performance`) stacks three 24-megapixel frames, about 10 seconds and 0.9 GB; it prints the time and peak working set and fails when the peak exceeds the focus stack budget estimate. Run it alone with `ctest --preset windows-x64-release -L performance --output-on-failure`, or skip it with `-LE performance`. See [known issues](../KNOWN-ISSUES.md) for the six existing full-suite discrepancies. The ASAN preset enables MSVC AddressSanitizer; its intermittent exit stall is also disclosed.

For a full Release build followed by one CTest run and the separate native viewport checks, run `./scripts/validate-release.ps1` from a bootstrapped PowerShell 7 session on an interactive Windows desktop with display scaling set to 100%. It stops before testing if configuration or compilation fails. After CTest finishes, it runs `native_viewport_tests` with `QT_SCALE_FACTOR=2` and restores the previous environment. Either test stage failing makes validation fail; existing discrepancies are not waived. Logs and native captures go under `build/release/validation/`. Manual application verification is still separate.

Do not run validation alongside another build or test process using the same build directory. Do not enumerate and build every `.vcxproj`: the `Experimental`, `Nightly`, and memory-check projects launch CTest dashboards and can start tests while other executables are still being linked. The normal build command above does not select those dashboard targets.

Most native tests generate their own inputs. Some image-quality and diagnostic tests need external corpora, including `evidence/imaging/astronaut.png` for the subject-dialog test. Historical audit scripts additionally expect the pinned upstream checkout and the old research/evidence workspace. Those inputs and generated reports are not shipped in Git or the application-source ZIP; these optional tools are not a prerequisite for building the editor. Mac comparison tooling is documented in [reference/README.md](../reference/README.md).


## HDR codec sources

`dependencies.lock.json` pins OpenEXR 3.3.5, Imath 3.1.12 and libdeflate 1.18 by commit and source-archive SHA256. Bootstrap verifies their archives in `dependencies/downloads/hdr`; the first online configure can also acquire them. Keep these archives for offline configuration. `scripts/HdrDependencies.cmake` builds them statically, without their tools, examples or tests. No additional codec DLL is needed. Packaging includes the verified archives and licenses beside the existing dependency sources/notices. The Radiance decoder is the pinned, HDR-only `src/imaging/third_party/stb_image.h`, with its license retained.

`migration.hdr` generates small independent linear EXR and RGBE fixtures and exercises the production HDR codecs, renderer, project store, export and history. The UI cases cover import/drop, preflight failure/Cancel, adjustment Apply/Cancel/undo, conversion confirmation/worker cancellation/undo, command gating and EXR export. Keep manual Windows verification separate from host-side numeric checks.

CTest runs existing executables; it does not rebuild them. Building only `Compositor` leaves test executables untouched. Before the HDR checks, explicitly build every selected test target and stop on a build failure:

```powershell
cmake --preset windows-x64-release
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed; tests were not started.' }
cmake --build --preset windows-x64-release --target migration_tests core_tests stack_renderer_tests command_ui_tests import_ui_tests export_ui_tests nonmodal_edit_tests --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Test build failed; tests were not started.' }
ctest --preset windows-x64-release -R 'migration.hdr|core[.]|graphics.stack_renderer|command_ui|imports.ui|export.ui|nonmodal_edit.hdr_' --timeout 120 --output-on-failure
```

If either HDR nonmodal case reports `provide named nonmodal case`, check that the executable is from the current checkout: both names are registered in `tests/nonmodal_edit/NonmodalEditTests.cpp`. The current import executable includes four `hdr_` cases and 45 named cases in total; output containing only the 41 older cases indicates an outdated executable. Do not count an unknown case or a missing executable as an exercised HDR path.

The complete `imports.ui` test also requires the existing EXIF/profile fixtures in `evidence/imaging`. A missing `orientation-6.tif` or `adobe-rgb.png` is a failed prerequisite, not a reason to remove the assertions. The normal imaging bootstrap prepares these inputs. To restore only these fixtures in an already prepared checkout, run the existing generator with a Python environment containing Pillow (for example, the bootstrapped `dependencies/imaging/model-venv/Scripts/python.exe`). Restore its ICC input from the existing dependency lock first:

```powershell
$taskImagingLock = Get-Content dependencies/imaging/lock.json -Raw | ConvertFrom-Json
$taskProfile = $taskImagingLock.assets | Where-Object { $_.path -eq 'evidence/imaging/AdobeCompat-v2.icc' }
if (-not $taskProfile) { throw 'Locked fixture ICC profile is missing from the dependency lock.' }
New-Item -ItemType Directory -Force evidence/imaging | Out-Null
if (-not (Test-Path -LiteralPath $taskProfile.path)) {
    Invoke-WebRequest -Uri $taskProfile.url -OutFile $taskProfile.path -ErrorAction Stop
}
if ((Get-FileHash -LiteralPath $taskProfile.path -Algorithm SHA256).Hash -ine $taskProfile.sha256) {
    throw 'Fixture ICC profile checksum mismatch.'
}
python tests/imaging/make_fixtures.py
if ($LASTEXITCODE -ne 0) { throw 'Imaging fixture generation failed; do not treat imports.ui as validated.' }
```

Prepare the fixtures before the test run above. Their generation does not establish that WIC orientation/profile handling or the HDR UI tests passed on Windows.

# Building the packaged source

The community preview is a Windows port of [Compositor by Robbie Tilton](https://github.com/robbietilton/Compositor), pinned to `a19db9011282399785dc18efcfded904627bdcc2` (1.0.4). Upstream copyright and MIT notices are preserved. The port does not imply upstream endorsement or verified full Mac parity.

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

The full historical parity verifier needs additional reference inputs outside this application-source delivery: the pinned upstream checkout, sibling research inventory, Windows audit/evidence corpus, and applicable reference fixtures. Generated test-result JSON files are intentionally absent. Native source targets are included, but some diagnostic/quality tests require those external fixtures. Run appropriate checks on the destination machine; copied historical evidence does not certify a new build.

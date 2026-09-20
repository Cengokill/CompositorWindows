# Compositor Windows

A native Windows 11 port under active development, based on Compositor commit `a19db9011282399785dc18efcfded904627bdcc2`. Full parity is not yet verified. See [PROGRESS.md](PROGRESS.md), [KNOWN-ISSUES.md](KNOWN-ISSUES.md), [VALIDATION.md](VALIDATION.md) and the source-attributed [PARITY.md](PARITY.md).

The application uses Qt Widgets, a Direct3D 11/Direct2D canvas, immutable document rasters, original portable C image algorithms, WIC and bundled HEIC codecs. Foreground removal runs entirely locally with a licensed BiRefNet-lite model; its quality requires separate acceptance and is not claimed identical to Apple Vision.

The [user guide](docs/user-guide.md) covers editing, Windows shortcuts, saving, export and project exchange.

## Build

Use Windows 11 x64, Visual Studio 2022 C++ tools and Windows SDK 10.0.26100.0. In PowerShell:

```powershell
. .\scripts\bootstrap.ps1 -Offline
cmake --preset windows-x64-release
cmake --build --preset windows-x64-release
ctest --preset windows-x64-release --output-on-failure
```

Omit `-Offline` on a fresh dependency setup. Model conversion requires Python 3.12 at build time; pass `-Python <absolute executable>` if the bundled development interpreter is unavailable. Dependencies and hashes are pinned. The test suite retains three unresolved source/test parity failures. A nonzero parity gate must not be represented as a release pass.

For the packaged source snapshot, also pass `-RuntimeDirectory <extracted application payload>` to bootstrap; for example, `-RuntimeDirectory C:\Extracted\CompositorWindows\versions\0.1.0`. A fresh codec build produces new DLL bytes, while this checkpoint's CMake build requires the original runtime hashes. Bootstrap builds the required headers/import libraries, then restores the four verified DLLs from the payload. Differing previous DLLs receive unique checksum-named backups. This does not claim a byte-identical codec rebuild. See [source-package instructions](docs/source-package.md) for the exact restore contract and the additional inputs required by the full parity verifier.

## Run and package

In the bootstrapped development shell, run `build/release/Release/Compositor.exe`. File > Import Image creates or adds to a canvas. The left toolbar selects tools; the upper controls change the current tool. Projects are `.comp` directories. Save and export are separate operations. `Ctrl+Z`/`Ctrl+Shift+Z` undo/redo. Escape cancels a live gesture; crop also requires Apply Crop or Enter.

Run `scripts/package.ps1` to create a portable ZIP and a standard-user installer. In a portable package, run `CompositorLauncher.exe`; it needs no SDK, Qt installation, Python, model download or HEIC Store extension. See [docs/packaging.md](docs/packaging.md) for layout and deployment checks.

The upstream checkout is preserved. The application source is MIT-licensed with original notices; packaged dependency licenses and corresponding sources accompany the binaries. No release has been published.

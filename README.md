# Compositor Windows community preview

A native Windows 11 x64 community port of **Compositor 1.0.4** by
[Robbie Tilton](https://github.com/robbietilton/Compositor), pinned to upstream
`a19db9011282399785dc18efcfded904627bdcc2`. This independent port preserves the
original MIT notices and does not imply upstream endorsement. Full Mac equivalence
and Photoshop compatibility are unverified.

Use the MSI or extract the portable ZIP and run `Compositor.exe`. Codecs, Qt/MSVC
runtimes and the offline foreground model are included. No account or first-run
model download is required. The preview is unsigned and uses manual updates.
Public download links are pending release acceptance and repository selection.

Read the [user guide](docs/user-guide.md), [release notes](docs/release-notes.md)
and [installation instructions](docs/packaging.md). Projects are `.comp`
directories; back up the complete directory. The [demo](demo/README.md) provides
redistributable inputs for a complete editing task.

## Build

Use Windows 11 x64, PowerShell 7, Visual Studio 2022 C++ tools, Windows SDK
10.0.26100.0 and Python 3.12 for the initial model conversion. Dependencies and
hashes are pinned.

```powershell
. ./scripts/bootstrap.ps1 -Python C:\Python312\python.exe
cmake --preset windows-x64-release
cmake --build --preset windows-x64-release --parallel 4
```

Use `-Offline` after dependencies have been prepared. In the bootstrapped shell,
run `build/release/Release/Compositor.exe`.

A fresh source-package build also needs `-RuntimeDirectory` pointing to the
extracted portable directory containing `Compositor.exe` and the four locked
imaging DLLs. Bootstrap builds the headers/import libraries and restores those
verified DLLs. A codec rebuild is not claimed byte-identical; see
[source-package instructions](docs/source-package.md).

Run `scripts/bootstrap-packaging.ps1`, then `scripts/package.ps1` for the MSI,
portable ZIP and matching source. Packaging requires the pinned WiX tools and
.NET on the build host only. Native application source is MIT; packaged notices
and corresponding dependency sources describe the other licenses.

## Contributing and verification

[KNOWN-ISSUES.md](KNOWN-ISSUES.md) records retained test discrepancies and missing
acceptance. In the complete development workspace, run
`ctest --preset windows-x64-release --output-on-failure`; original source/test
conflicts remain visible as failing tests. The standalone source ZIP supports
an application rebuild. The historical parity workspace and all runtime
fixtures are separate from that download; see the source-package instructions.

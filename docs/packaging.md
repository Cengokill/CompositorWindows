# Windows packages

Use the branded setup EXE or the x64 MSI on Windows 11 to install for the current user. The setup EXE contains the same MSI. Installation needs no administrator account, Python, .NET or developer tools. The default location is `%LOCALAPPDATA%\Programs\CompositorWindows`; the Start menu entry is **Compositor Windows**. The portable ZIP contains the same files: extract it into a new folder and run `Compositor.exe`.

The packages are unsigned. Windows can show an unknown-publisher warning. Keep Windows security enabled and obtain the download and published SHA256 from the release page. Save projects in your own document folders. MSI repair, upgrade and uninstall operate on installer-owned files; unlisted project folders are preserved. `.comp` projects are directories, with an **Open in Compositor** folder action; the default Explorer directory action is unchanged.

Packages use the stable channel. Compositor checks GitHub for new stable releases and links to their page, but does not install them: install a newer MSI or extract a new portable ZIP. The MSI preserves an existing custom installation location, rejects older versions, and treats the original same-version MSI as a maintenance package. A different MSI of the same version is rejected. Close the application before maintenance. The package omits development update helpers, receipts, trust configuration and signing keys.

To build a package, use PowerShell 7 in the Windows repository:

```powershell
./scripts/bootstrap-packaging.ps1
./scripts/package.ps1
```

Before each release, add a `# Compositor Windows X.Y.Z` heading to `docs/release-notes.md`, matching `project(VERSION)` in `CMakeLists.txt`. Under it, write the release's user-facing Markdown in any of the exact headings `## New features`, `## Improvements` and `## Fixes`; omit empty sections. Use paragraphs, lists, emphasis and links within these sections. The in-app What's New window displays those sections for the installed version only. The existing older entries can retain their historical format. This file is the editorial source for both the shipped notes and the resource embedded in the executable.

After building a candidate, run `./Compositor.exe --check-release-notes` from the extracted portable package and check `$LASTEXITCODE`: 0 means the executable contains nonempty notes for its own version; 2 means they are absent or empty. `tests/packaging/run_checks.ps1` performs this check against the portable executable. Also test an update from a previously recorded lower version with networking disabled and inspect the notes and scrolling in the application. Missing notes do not block startup, but they are a release preparation error.

Bootstrap downloads and verifies the pinned Qt corresponding source and WiX 5.0.2 compiler, UI and bootstrapper archives. Building installers requires a .NET runtime (6 or later); this host uses .NET 8. Only the packaging host needs WiX/.NET. `scripts/msi/toolchain.json` records tool URLs and hashes. `-Offline` verifies cached inputs; `package.ps1` uses this mode automatically. `-PortableOnly` omits installer generation. `-SkipBuild` packages the existing Release executable and must be used only after validating that binary against the current source.

Each run creates a new `dist/CompositorWindows-<version>-<timestamp>` directory with the setup EXE, MSI, portable ZIP, matching application-source ZIP, separate demo ZIP, SHA256 sums, and package manifest. The manifest records the Git revision, whether any packaged source files differ from that commit, source archive hash, upstream pin, and every runtime file. Unrelated working-tree evidence does not affect the source flag. A public release must identify its frozen commit/tag. Existing output directories are preserved.

The demo ZIP contains redistributable images, project folders, exported images, instructions and the backdrop generator from `demo/`. Video recordings are separate release assets. Demo media is excluded from the application-source ZIP and installed runtime. A provisional portable package can supply the validated executable for recording; the final MSI/ZIP must retain its exact executable, DLL, model and shader hashes when only release documents or demo assets change.

Both downloads include application-local Qt/MSVC/codec/ONNX runtimes, the offline foreground model, shader, user guide, limitations, notices and corresponding dependency sources. The MSI is validated with the Windows Installer ICE checks; only ICE91 is suppressed because installation is intentionally per-user. WiX [package scope](https://docs.firegiant.com/wix/schema/wxs/packagescopetype/) and [UI documentation](https://docs.firegiant.com/wix/tools/wixext/wixui/) describe the installer facilities used.

Run the existing deployment checks after freezing the candidate:

```powershell
./tests/packaging/run_checks.ps1 -PackageDirectory <candidate-directory> -Install
```

The checks remove developer paths from child-process environments, verify payload/source hashes, run runtime and native editing/save/export checks, install/repair the MSI, and verify project preservation on uninstall. To exercise upgrade and downgrade rejection, pass `-UpgradeInstaller <newer-version-msi>` built from the same payload using `scripts/msi/build-msi.ps1`. The test refuses to replace an existing registered preview. Results are stored under `evidence/packaging/`.

On a disposable Windows installation without an existing Compositor installation or installation folder, pass `-BundleInstall` to exercise setup EXE install, repair and uninstall. Add `-BundleUpgradeInstaller <newer-version-setup.exe>` to also exercise bundle upgrade, downgrade rejection and an upgrade from the direct MSI. The bundle checks use the default per-user installation folder and refuse to run if an installation, folder or related bundle already exists. Check the installer windows and the downgrade message interactively as well; quiet checks cannot establish their appearance or focus.

Deployment results describe the machine where they were measured. See [validation](../VALIDATION.md) for the checks performed and the remaining coverage limits.

# Windows community-preview packages

Use the x64 MSI on Windows 11 to install for the current user. It needs no administrator account, Python, .NET or developer tools. The default location is `%LOCALAPPDATA%\Programs\CompositorWindows`; the Start menu entry is **Compositor Windows Community Preview**. The portable ZIP contains the same files: extract it into a new folder and run `Compositor.exe`.

The preview is unsigned. Windows can show an unknown-publisher warning. Keep Windows security enabled and obtain the download and published SHA256 from the release page. Save projects in your own document folders. MSI repair, upgrade and uninstall operate on installer-owned files; unlisted project folders are preserved. `.comp` projects are directories, with an **Open in Compositor** folder action; the default Explorer directory action is unchanged.

Automatic updates are unavailable for this preview. Install a newer MSI or extract a new portable ZIP. The MSI preserves an existing custom installation location, rejects older versions, and treats the original same-version MSI as a maintenance package. A different MSI of the same version is rejected. Close the application before maintenance. The package omits development update helpers, receipts, trust configuration and signing keys.

To build a package, use PowerShell 7 in the Windows repository:

```powershell
./scripts/bootstrap-packaging.ps1
./scripts/package.ps1 -Version 0.1.0
```

Bootstrap downloads and verifies the pinned Qt corresponding source and WiX 5.0.2 compiler/UI archives. Building MSI files requires a .NET runtime (6 or later); this host uses .NET 8. Only the packaging host needs WiX/.NET. `scripts/msi/toolchain.json` records tool URLs and hashes. `-Offline` verifies cached inputs; `package.ps1` uses this mode automatically. `-PortableOnly` omits MSI generation. `-SkipBuild` packages the existing Release executable and must be used only after validating that binary against the current source.

Each run creates a new `dist/CompositorWindows-<version>-preview-<timestamp>` directory with the MSI, portable ZIP, matching application-source ZIP, separate demo ZIP, SHA256 sums, and package manifest. The manifest records the Git revision, whether any packaged source files differ from that commit, source archive hash, upstream pin, and every runtime file. Unrelated working-tree evidence does not affect the source flag. A public release must identify its frozen commit/tag. Existing output directories are preserved.

The demo ZIP contains redistributable images, project folders, exported images, instructions and the backdrop generator from `demo/`. Video recordings are separate release assets. Demo media is excluded from the application-source ZIP and installed runtime. A provisional portable package can supply the validated executable for recording; the final MSI/ZIP must retain its exact executable, DLL, model and shader hashes when only release documents or demo assets change.

Both downloads include application-local Qt/MSVC/codec/ONNX runtimes, the offline foreground model, shader, user guide, limitations, notices and corresponding dependency sources. The MSI is validated with the Windows Installer ICE checks; only ICE91 is suppressed because installation is intentionally per-user. WiX [package scope](https://docs.firegiant.com/wix/schema/wxs/packagescopetype/) and [UI documentation](https://docs.firegiant.com/wix/tools/wixext/wixui/) describe the installer facilities used.

Run the existing deployment checks after freezing the candidate:

```powershell
./tests/packaging/run_checks.ps1 -PackageDirectory <candidate-directory> -Install
```

The checks remove developer paths from child-process environments, verify payload/source hashes, run runtime and native editing/save/export checks, install/repair the MSI, and verify project preservation on uninstall. To exercise upgrade and downgrade rejection, pass `-UpgradeInstaller <newer-version-msi>` built from the same payload using `scripts/msi/build-msi.ps1`. The test refuses to replace an existing registered preview. Results are stored under `evidence/packaging/`.

A developer-host run does not establish clean-machine operation. Repeat installation, offline first launch, editing/save/export, manual-update behavior and uninstall on a clean supported Windows 11 machine. Windows Sandbox and Hyper-V were disabled on the available host when packaging was implemented; enabling them and provisioning a guest has not been performed. See `VALIDATION.md` for actual candidate results.

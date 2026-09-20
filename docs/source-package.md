# Source package and verification inputs

The development package includes application source, native test sources, build scripts, dependency locks and corresponding dependency-source archives. The application build uses the pinned Qt/MSVC kit described in `VALIDATION.md`; bootstrap prepares the locked dependencies and model before CMake runs. The installed application uses bundled runtime DLLs and model data.

The HEIC dependency build does not promise byte-identical DLLs: the recorded libheif/libde265 binaries contain ordinary build timestamps. The application CMake configuration and deployment steps deliberately verify the original four runtime hashes. When building this source snapshot in a fresh location, keep the extracted application payload and provide it to bootstrap:

```powershell
. .\scripts\bootstrap.ps1 -Python C:\Python312\python.exe -RuntimeDirectory C:\Extracted\CompositorWindows\versions\0.1.0
cmake --preset windows-x64-release
cmake --build --preset windows-x64-release
```

Replace the example Python and payload paths with real paths. `RuntimeDirectory` is the directory containing `heif.dll`, `libde265.dll`, `onnxruntime.dll` and `onnxruntime_providers_shared.dll`. Bootstrap still builds/prepares the codec and ORT headers and import libraries needed to link the application; it then restores the four verified checkpoint runtime DLLs. On a workspace with complete dependencies, `-Offline -RuntimeDirectory <payload>` performs the same verification without downloading dependencies. A fresh setup requires the locked downloads and Python conversion dependencies unless already cached.

The standalone `scripts/restore-imaging-runtime.ps1 -RuntimeDirectory <payload>` validates all four input hashes before staging or copying any file. Its destinations come only from the existing imaging lock. Already verified files are left in place. A differing previous file is preserved beside its original path as `<file>.before-runtime-<old SHA256>-<unique id>.bak`; failed installation attempts preserve staged/installed bytes and attempt to restore the prior files. No lock hashes or system settings are changed. The helper restores runtime files only; it does not supply missing headers, import libraries or the model.

The isolated verification at `tests/runtime_restore/run-20260920-052849-b5855d/result.json` confirms four installed hashes, previous-file preservation, an unchanged repeat run, and rejection of a corrupt fourth input before any destination copy. It also verifies that the live workspace's original DLLs remain unchanged. This is a tested restoration path, not evidence of a fresh-machine build or deterministic codec/model rebuild. For experiments with modified runtime libraries, retain the verified build and backups, replace the desired application-local DLLs afterward, and run `Compositor.exe` directly as described in the packaged notices; its update receipt will no longer validate those modified bytes.

The full source-attributable parity verifier also needs the working reference packet. It is retained in the development workspace and is not embedded in the application source ZIP:

- A sibling `upstream` checkout at `a19db9011282399785dc18efcfded904627bdcc2`.
- The sibling `research` inventory and reference-harness inputs.
- The Windows workspace's `audit`, `evidence`, `parity-ledger.json` and current native build outputs.

Preserve these inputs when transferring the verification workspace. Rebuild and recapture checks on the destination machine; copied historical reports do not certify its runtime. The source ZIP alone cannot reproduce the complete parity report. Mac-generated fixtures and foreground-quality acceptance remain separate prerequisites.

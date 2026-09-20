# Persistence verification

Run from the workspace root:

```powershell
& windows/tests/persistence/run.ps1
```

The isolated CMake target uses the shared document model, Qt 6.8.3 Core, MSVC 19.44, Windows SDK 10.0.26100.0, and the actual WIC project adapter. `build-run-6.log` and `results.xml` record **11 passed, zero failed, zero skipped**. SDK discovery required ordinary host access because the sandbox blocked reading `AppData/Local/Microsoft SDKs`; no SDK or security setting was changed. Earlier configure and missing-codec-source link failures remain in logs 1–4. Run 3 had a skipped symbolic-link test; later runs exercise a test-owned directory junction when symlink creation is unavailable and do not count that earlier skip as a pass.

| Case | Contract exercised |
|---|---|
| `blank_roundtrip` | v7 schema, UUIDs, Unicode, typed fractional transforms, omission of selection |
| `raster_mask_roundtrip` | Immutable raster/coverage, disabled and unlinked mask placement, opacity/blend, removal of old assets |
| `legacy_version_gates` | Reader 1–7, absent resolution/appearance defaults, invalid old-version appearance/groups |
| `malformed_metadata` | Required field omissions, required transform defaults, wrong Boolean type, unsafe names/filenames |
| `graph_boundaries` | 64-ancestor leaf, group depth rejection, 256-node live chain, cycles and group sources |
| `rollback_faults` | Restore existing package after five injected save exceptions |
| `crash_recovery` | Reconstructed journal state after original rename and after replacement install |
| `allocation_preflight` | Reject oversized PNG IHDR before any codec allocation |
| `hostile_journal` | Reject unrelated stage paths and preserve existing project |
| `reparse_rejection` | Reject a project path through a symbolic link or directory junction |
| `real_png_exchange` | Actual WIC premultiplied RGBA8 and no-alpha gray8 PNG package round trip |

Most package logic tests use a deliberately identified mock codec with an IHDR-shaped test header and raw bytes. These test files are not PNG fixtures and are never represented as Mac output. `real_png_exchange` uses real PNG encoding/decoding. The codec agent's additional strict-format tests cover unsupported PNG forms. None of these tests establishes Mac exchange or ports all assertions in any upstream test file.

Implementation: `src/persistence/ProjectStore.h`, `ProjectStore.cpp`, `WicProjectCodec.cpp`. Public `IProjectStore` exposes `load`, `save`, and `recover`; `OpenProject` carries the complete document, active layer and source version. `ProjectAssetCodec` isolates all asset I/O; the production adapter uses `makeWicProjectCodec()`. Save always emits v7. `adjustmentJson`/`shapeJson` retain semantic payloads; unknown nested keys survive while editing is unavailable, but manifest/layer unknown keys are ignored as in Swift's Codable reader. Recognized optional defaults may be made explicit on resave. There is no claim of JSON byte identity.

The save sequence writes and reopens a complete sibling stage, writes a transaction journal, renames the prior package to a backup, installs the stage, then removes the backup/journal. A named session mutex coordinates this implementation's readers and writers without needing write access to open a clean read-only package. Exceptions before cleanup restore the prior package. Startup recovery restores a missing target from backup or validates an installed replacement before completing cleanup. Stage paths and markers are checked; ambiguous ownership preserves the packages and raises an error. Recursive cleanup checks reparse points first.

Limits: two directory renames are not an atomic replacement; power-cut/filesystem durability and adversarial concurrent filesystem mutation have not been verified. A crash before journal creation can leave an identifiable orphan stage. The mutex coordinates instances in the same Windows session, not arbitrary external editors or other user sessions. Failure cleaning up an already installed package leaves a journal for later recovery. The existing tests reconstruct interruption states and inject exceptions; they do not terminate a saving process or cut power. Read-only open no longer creates a sibling lock file, but ACL-specific read-only package tests remain pending. Mac fixtures, full legacy/adjustment payload exchanges, all corruption limits, UI save/dirty coordination, disk-full, removable-volume and network-share behavior remain acceptance work.

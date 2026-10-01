# CompositorWindows — instructions for coding agents

Read this file and the repository's `AGENTS.md` before changing code, tests, documentation, or UI. Then read `README.md`, `PROGRESS.md`, and the files relevant to the task. Consult `KNOWN-ISSUES.md` and `VALIDATION.md` for recorded failures or release work. Current code and tests take precedence over dated descriptions in this file.

This file guides implementation; it is not a second progress log or a specification of every subsystem. A newer explicit user instruction takes precedence. Do not infer Windows functionality from a Mac pull request, screenshot, or release note without checking the Windows implementation.

## 1. Product and upstream relationship

- CompositorWindows is an independently maintained, native Windows 11 x64 port of Robbie Tilton's Compositor. Preserve the original attribution, MIT notices, and applicable third-party licenses. Do not imply that the Mac maintainer endorses the port.
- The aim is to follow the original editor's core compositing workflows, editing behavior, design, and `.comp` project format while using appropriate Windows implementations. Windows-specific refinements are welcome when they fit that foundation.
- The documented upstream baseline is Mac Compositor 1.0.4 at `a19db9011282399785dc18efcfded904627bdcc2`. Check `dependencies.lock.json`, `PROGRESS.md`, and current code before relying on that pin. Newer Mac functionality and full Mac parity are not established by the baseline.
- Treat upstream behavior as a product reference, not Swift/AppKit code to translate mechanically. Compare the observable behavior, data contract, and edge cases before designing a C++ implementation.
- A Mac PR's proposed branch is not necessarily its final behavior. For upstream-porting work, inspect the final upstream `main` implementation and any maintainer follow-up. A closed PR can have selected changes incorporated separately.
- Do not import a Mac-only product decision as a permanent restriction on this Windows project. Check the current Windows behavior, the user's request, and applicable maintainer decisions.
- Prefer a concrete compositing or photo-editing workflow over a parallel control or subsystem that duplicates existing behavior.

## 2. Architecture and implementation

The project uses C++20, Qt Widgets, and Direct3D 11. `docs/architecture.md` identifies the main entry points:

| Concern | Primary area |
| --- | --- |
| Document state and history | `src/core` |
| Editing and pixel operations | `src/editing`, `src/graphics`, `src/effects` |
| Rendering | `src/render` |
| Windows interface | `src/ui` |
| Project persistence | `src/persistence` |
| Codecs and foreground removal | `src/imaging` |
| Build and test targets | `CMakeLists.txt` |

- Locate the existing owner of state and behavior before editing: document model, undo command, renderer, preview, import/export path, persistence, UI, or test.
- Keep the document and history independent of window and graphics-device ownership. Rendering consumes document state; recreating a graphics device must not change the project.
- Reuse existing editing, adjustment, effects, and rendering paths instead of adding duplicate state or feature-specific lifecycles.
- Use C++ ownership and lifetime rules deliberately. Prefer RAII for Windows handles, COM objects, Qt objects, graphics resources, and temporary files. Check thread affinity and cancellation when work crosses the Qt UI thread or a worker.
- Validate sizes, indices, arithmetic, paths, encoded data, and external inputs before allocation or decoding. Report malformed or oversized input explicitly rather than returning a plausible but incomplete image.
- Keep dependency inputs pinned and maintain `dependencies.lock.json` accurately when an upstream or dependency baseline changes. Preserve provenance for shared upstream C code and third-party components.
- Avoid speculative refactors, duplicate verification frameworks, and work unrelated to the user's request.

## 3. Editing behavior and UI

- Preserve the existing layer model when changing move, transform, mask, clipping, group, adjustment, effect, crop, merge, import, or export behavior. Trace the affected preview, undo/redo, save/reopen, and flattened output paths.
- Ordinary layer transforms should retain source pixels unless the user explicitly applies a destructive operation. An applied destructive edit must leave metadata consistent with the resulting pixels.
- Preserve layer identity, order, visibility, opacity, blending, masks, transforms, editable metadata, and transparency when an operation is intended to retain them. Test transitions that intentionally discard editability.
- A temporary preview must have clear Apply and Cancel behavior. Cancel restores the prior document; Apply creates an appropriate undoable edit. Guard against a late worker result changing a cancelled or different document.
- Keep controls coherent with the existing Windows interface. Use the original Mac design as a visual reference, while retaining native Windows title bars by default and the saved optional Mac-style appearance.
- Use Windows shortcut labels and actual Windows interactions in UI documentation. Do not copy Command-key, Finder, AppKit, or macOS title-bar instructions into the Windows guide.
- For UI work, inspect the rendered application when feasible and include a real screenshot or recording with the PR. An offscreen Qt test alone does not establish the appearance or behavior of a native window.
- Check canvas display, export, thumbnails, and reopened projects for agreement when changing shared rendering behavior. Test device or software fallback paths when they are affected.

## 4. `.comp` projects and external files

- `.comp` is the editable project directory; it contains a manifest and image assets. PNG and JPEG exports are flattened derivatives and do not save editable project changes.
- The documented Windows reader accepts project versions 1–7 and writes version 7 at the current upstream pin. Read `docs/project-format-v7.md` and `src/persistence` before changing a persisted field. Never substitute the newer Mac format version for the Windows version without implementing and testing the migration.
- Preserve supported older-project reads. Distinguish optional legacy fields from malformed missing required fields, and enforce version gates where the format requires them.
- Validate IDs, hierarchy, references, asset paths, regular-file requirements, dimensions, image budgets, and decoded data before installing a project into the live document.
- Keep the live document and existing destination recoverable on a failed load or save. Inspect the Windows storage implementation and test its actual interruption behavior; do not claim macOS-style atomic directory replacement without evidence.
- For a schema change, update the external format guide, compatibility handling, and meaningful round-trip and rejection tests. Exercise edit → save → reopen → render/export rather than checking only in-memory assignments.
- Mac-generated fixture exchange and full reciprocal compatibility remain unverified unless an actual comparison has been performed. `reference/README.md` describes the optional Mac capture and comparison workflow. A prepared fixture generator is not a completed Mac test.
- Import only formats and editable elements supported by the current Windows code. Describe conversions, omissions, or rejections honestly. Do not promise PSD import/export or preservation of Mac-only fields based solely on upstream support.
- Check real external files as well as synthetic fixtures when a change concerns an importer or codec. Keep parser provenance and license compatibility clear.

## 5. Limits, performance, and security

- Find the current enforcement sites before changing or quoting a dimension or memory limit. The project-format guide documents v7 storage limits; the Windows user guide separately documents a 100-million-pixel export ceiling. Editing, storage, rendering, and export can have different constraints.
- A new limit must address a demonstrated failure and must not reject valid work without justification. Record the input, dimensions, observed failure, and regression test.
- Bound allocation and decoding work for untrusted local projects and images. Treat Windows reparse points, path traversal, symlinks, partial packages, and replacement of destination files as part of file-safety review.
- Watch for excessive copies, repeated full-canvas work, blocking UI-thread tasks, resource leaks, and Direct3D/Qt lifetime problems. Measure performance claims with representative documents and hardware.
- Distinguish a local robustness issue from a network security claim. Do not present a speculative risk as an observed vulnerability.
- Never commit credentials, development signing keys, private user projects, raw verification dumps, or generated local logs.

## 6. Build and verification

- Use the repository's documented Windows toolchain and existing CMake presets. `docs/source-package.md` explains bootstrap, deployment assets, builds, and tests.
- A typical Release build and affected-test run is:

  ```powershell
  cmake --preset windows-x64-release
  cmake --build --preset windows-x64-release --target Compositor --parallel 4
  cmake --build --preset windows-x64-release --parallel 4
  ctest --preset windows-x64-release -R <affected-pattern> --output-on-failure
  ```

- Choose checks relevant to the change. Use the normal CTest suite, Qt tests, native interaction checks, or the ASAN preset when they address a concrete risk. Do not add a new test framework for a one-off change.
- Classify a failing test before changing code or expectations: determine whether production regressed, the expected behavior changed, the fixture is stale, or the environment is missing an input.
- `KNOWN-ISSUES.md` records existing full-suite discrepancies and an intermittent ASAN shutdown issue. A new run must report its own results; historical failures are neither automatic exemptions for new regressions nor passing tests.
- Some optional tests require external corpora or Mac reference fixtures. Mark those checks unrun or blocked when the inputs are absent. Never report prepared tooling, compilation, a static inspection, and observed runtime behavior as equivalent evidence.
- For an interactive or hardware-dependent change, record the actual Windows version, architecture, GPU or display configuration when relevant, test commands, inputs, results, and unverified paths. A fresh Windows machine or Mac fixture is not a prerequisite for every preview change.
- Test through production paths. A persistence regression should save and reopen a project; a preview regression should exercise Apply, Cancel, undo, and late-result behavior.

## 7. Documentation and pull requests

- Make one coherent, independently reviewable change. Keep unrelated defaults, refactors, dependency upgrades, and ported features separate unless they are required for the same behavior.
- Follow `AGENTS.md` for coordination boundaries. Update `PROGRESS.md`, `VALIDATION.md`, and `KNOWN-ISSUES.md` in place only when their actual status changes. Do not create handoff reports, agent diaries, completion matrices, or duplicate descriptions of the implementation.
- Documentation should explain user workflows, build steps, external contracts, and decisions that are not evident from code. Update the relevant guide when a user workflow or file contract changes.
- Keep generated logs, local experiments, and verification-only screenshots out of Git. A deliberate product screenshot or real test fixture needs a clear purpose and provenance.
- In a PR, explain the user problem, the implemented behavior, intentional differences from upstream, affected compatibility, and the checks actually run. Include a screenshot or recording for UI changes and reproduction steps for bug fixes.
- For upstream synchronization, identify the upstream commits considered and classify each as ported, already covered, Mac-only, or blocked. Do not advance the upstream pin or claim release parity merely because some commits were ported.
- Do not commit `PR_REVIEW_*.md` files or other local review artifacts unless the user explicitly asks for them to be tracked.
- The published preview is unsigned and updated manually. Do not describe a local build as a signed or fully reproduced release. Packaging and distribution claims require their own checks.

## Before finishing

Verify that the change serves the requested workflow, uses the existing owner of state, preserves editable data where intended, and has a coherent preview/undo/save lifecycle. Confirm the actual Windows behavior and tests, document any remaining limitation, and make no unsupported Mac-parity or release claim.
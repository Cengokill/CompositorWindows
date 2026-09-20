# Pinned macOS reference capture

Status: **blocked_reference**. No Mac is available in this Windows session. The Swift exporter is prepared source and has not been compiled or executed. There are no approved Mac fixtures in this directory.

On a logged-in Mac with macOS 26.5 or later and a compatible Xcode SDK, copy this directory and run:

```sh
python3 capture_mac.py --upstream /absolute/path/to/Compositor --output /absolute/path/to/new-reference-run
```

The output directory must be new. The script creates a detached Git worktree at the pinned SHA, records macOS/Xcode/SDK/Swift/GPU/display information, runs the unchanged unit and UI targets, then adds only `CompositorTests/MacReferenceExporter.swift` and runs its exporter. The Xcode project uses filesystem-synchronized groups. No project-file edit is needed. Sparkle's pinned package must be available to Xcode; failures remain in the logs. The script never changes the original checkout or lowers the deployment target. `--prepare-only` creates the worktree and instrumentation without executing tests.

The exact executed commands and exit codes are in `commands.json`. The `.xcresult` bundles and raw `xcresulttool` summary/test exports retain actual invocations and failure information. The source inventory predicts **310 unit invocations from 288 functions**, including nine parameterized functions. Actual counts remain unknown until reviewed in the result bundles. XCTest launch configurations may repeat one of the three UI methods. A function or assertion-site count must never be reported as tests passed.

The baseline capture leaves the source's opt-in diagnostic behavior unchanged. Two benchmark functions return early without test-host `BRUSH_BENCHMARK=1`; `LevelsTests.panelPreview` returns early without `LEVELS_PREVIEW=1`. The script does not yet configure these special Xcode TestAction environment variables. Record these three guarded behaviors as unexecuted until a separate run proves the variables reached the test process and the benchmark/panel artifacts were produced, even if the surrounding XCTest invocation reports success.

The exporter prepares 26 packages: seven version-specific records (v1–v7), 13 blend-mode cases, and six adjustment cases. Every package is saved and read using the pinned `ProjectStore`, then flattened by `ImageExporter`; metadata, PNG, raw RGBA8, and clean-install assertions are recorded. The legacy packages use fields appropriate to each version and must pass the pinned reader. They are generated legacy records, not historical release outputs. Codable probes capture actual Foundation geometry, enum-keyed HSV dictionaries, and every adjustment payload. The fixed grain seed is `0xC04F05`; all pixel fixtures are locally generated and carry no third-party photo license burden.

`file-manifest.json` records fixture sizes and SHA-256 hashes. `fixtures/environment.json` records screen scale/profile and raw format; the system-profiler log retains device details. Package metadata does not serialize history, viewport, selection, or collapse state. The exporter currently checks only clean-history/selection installation; full transaction traces, brush paths, tiled viewports, Copy Merged, JPEG, licensed photographs, filters, noise/healing seeds, and Vision refinement/quality still need additional reference cases.

The exact-byte comparator is usable on either host:

```sh
python3 compare_rgba.py /path/reference.rgba /path/windows.rgba --width 19 --height 17 --output /path/comparison.json
```

It reports `blocked_reference` when the reference is absent. Nonzero tolerances require a rationale supplied before comparison. Preserve failed comparisons; changing a threshold after a failure requires an explicit contract review. Metadata comparison must interpret the enum-keyed `adjustments` and `bands` pair arrays as dictionaries because Swift dictionary iteration order is not a persistence contract. Do not compare those arrays by storage order or silently rewrite other arrays, whose order is meaningful.

Before accepting a capture, verify all exit codes, actual test invocations, raw-buffer orientation against the asymmetric synthetic fixture, all 26 package reader assertions, the baseline SHA, and the instrumentation-only worktree diff. A prepared or completed exporter alone does not establish Windows differential parity.

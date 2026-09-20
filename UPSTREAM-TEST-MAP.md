# Upstream test mapping

Baseline: `a19db9011282399785dc18efcfded904627bdcc2`, verified from the untouched upstream checkout.

The complete mapping is [audit/upstream-test-map.json](audit/upstream-test-map.json). Each entry contains a stable test ID, actual declaring suite, function, source lines/link/hash, complete direct assertion expressions and throws closures, prerequisites, reproduction command, parameter values, implementation/test locations, result and evidence fields. Helper assertion sites are separately retained with their file locations. The audit reads the source; it does not substitute a matching number of smoke tests.

| Static inventory | Count |
|---|---:|
| Unit files | 47 |
| Unit functions | 288 |
| Parameterized unit functions | 9 |
| Expected unit invocations after parameter expansion | 310 |
| UI files / methods | 2 / 3 |
| All static assertion, requirement and failure sites, including helpers | 1954 |
| Sites within test function bodies | 1844 |
| Actual test invocations / passes | Unknown / not established |

`#require` sites are preconditions, not independent behavior tests. Loops and parameterization repeat assertion sites. One XCTest launch method runs for each target UI configuration, so three declarations are not necessarily three runtime invocations. `FloatingPanelTests` opens real panels and runs AppKit display passes; a headless Swift compiler is insufficient. The Vision smoke test admits `noSubject`; it cannot demonstrate useful foreground quality. Legacy HSV tests encode current structs while omitting optional additions; they do not establish historic release-to-release exchange.

Three functions return early unless their test-host environment is set: the brush performance benchmark and brush-intersection benchmark require `BRUSH_BENCHMARK=1`; `LevelsTests.panelPreview` requires `LEVELS_PREVIEW=1`. Their gates are recorded per function. An Xcode-reported pass without the required environment does not demonstrate that the guarded work ran.

[audit/acceptance-candidates.json](audit/acceptance-candidates.json) expands the tests into 313 invocation candidates, with exact arguments and assertion obligations. All begin **unported**, **not_run**, and **blocked_reference**. Lead-owned acceptance records may map implemented Windows tests later. Test-case source coverage is separate from Windows implementation and verified coverage.

[audit/command-inventory.json](audit/command-inventory.json) retains 208 SwiftUI control/container declarations, 12 native event-handler bodies and relevant mode enums. This is a discovery floor: nested containers and generated controls require dispatch review, and individual dynamic menu variants still need ledger entries. No complete shortcut or UI coverage claim follows from the count.

Rebuild and check reproducibility on Windows:

```powershell
python windows/audit/build_inventory.py
python windows/audit/build_inventory.py --check
python -m py_compile windows/audit/build_inventory.py windows/reference/capture_mac.py windows/reference/compare_rgba.py
```

These commands passed during preparation. macOS tests, the Swift exporter, fixtures, and Mac/Windows comparisons have not run. Follow [reference/README.md](reference/README.md) for capture commands and required environment evidence. The generator verifies the baseline SHA and hard-fails if the 288/3 inventory changes. Its balanced-delimiter scanner is an audit tool, not a Swift compiler; asserting runtime selection correctness still requires Xcode's actual discovered test records.

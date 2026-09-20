"""Summarize retained native logs; never infer upstream or Mac parity."""
from pathlib import Path
import hashlib
import json
import re
import xml.etree.ElementTree as ET

here = Path(__file__).resolve().parent
root = here.parents[2]
sha = "a19db9011282399785dc18efcfded904627bdcc2"
tests = ET.parse(here / "results.xml").getroot()
cases = tests.findall(".//testcase")
benchmarks = []
for path in sorted(here.glob("benchmark-*-run-*.log")):
    benchmarks.append({"log": path.relative_to(root).as_posix(),
        "exit_code": int(path.with_suffix(".exit.txt").read_text()),
        "results": [json.loads(line) for line in path.read_text(encoding="utf-8-sig").splitlines() if line.startswith('{')]})
source = [
    ("Compositor/Document/BrushStroke.swift", 146, "Original grid and virtual extent"),
    ("Compositor/Document/BrushStroke.swift", 253, "Continuous curve and provisional tail"),
    ("Compositor/Document/BrushStroke.swift", 565, "Affected-tile allocation budget"),
    ("Compositor/Document/BrushStroke.swift", 816, "Existing bounds plus changed alpha bounds"),
    ("Compositor/Document/EditorSession+Brush.swift", 13, "Remaining image/mask budgets"),
    ("Compositor/Document/EditorSession+Brush.swift", 103, "Immutable snapshot commit and attached mask expansion"),
]
files = [root / "upstream" / entry[0] for entry in source]
files += [root / p for p in ["windows/src/graphics/GrowingBrushSession.h", "windows/src/graphics/GrowingBrushSession.cpp", "windows/src/graphics/StackRenderer.cpp", "windows/tests/graphics/GrowingBrushSessionTests.cpp", "windows/tests/growing_brush/build/Release/growing_brush_tests.exe"]]
evidence = {
    "schema_version": 1, "baseline_sha": sha, "native_status": "passed",
    "overall_parity_status": "fail", "mac_reference_status": "blocked_reference",
    "full_upstream_test_methods_ported": 0,
    "command": "& windows/tests/growing_brush/run.ps1",
    "build_log": "windows/tests/growing_brush/build-run-13.log",
    "junit": "windows/tests/growing_brush/results.xml",
    "passed": sum(c.get("status") == "run" and c.find("failure") is None for c in cases),
    "failed": sum(c.find("failure") is not None for c in cases),
    "tests": [c.attrib for c in cases], "benchmarks": benchmarks,
    "cpu_warp_max_byte_error": 0, "cpu_warp_allowed_max_byte_error": 1,
    "environment": {"cpu": "Intel(R) Core(TM) Ultra 9 285K", "os_registry_product_name": "Windows 10 Pro", "os_display_version": "25H2", "os_build": "26200.9445", "configuration": "Release x64 /W4 /WX", "compiler": "MSVC 19.44.35221.0", "sdk": "10.0.26100.0", "gpu_test_adapter": "Microsoft Basic Render Driver (WARP)", "environment_source": "PowerShell registry reads; Get-CimInstance denied in sandbox"},
    "source_evidence": [{"path": p, "line": line, "claim": claim} for p, line, claim in source],
    "related_upstream_methods": [
        {"path": "CompositorTests/BrushTests.swift", "line": 114, "name": "continuousStrokeCrossesTilesAndCommitsOneUndo", "native_witnesses": ["unchanged_grid", "immutable_history"], "complete_assertion_equivalence": False},
        {"path": "CompositorTests/BrushTests.swift", "line": 155, "name": "softMaskPaintingPreviewMatchesCommitAndPersists", "native_witnesses": ["selection_cancel_mask", "viewport_mask"], "complete_assertion_equivalence": False, "missing": "Exact upstream session input sequence, persistence roundtrip and desktop preview"},
        {"path": "CompositorTests/BrushTests.swift", "line": 232, "name": "importedImageLayerExpandsAcrossCanvasWithoutMovingImageOrMask", "native_witnesses": ["viewport_transformed", "growth_padding_masks"], "complete_assertion_equivalence": False, "missing": "Project roundtrip and complete EditorSession/history sequence"},
        {"path": "CompositorTests/BrushTests.swift", "line": 270, "name": "paintedBoundsTrimTilePaddingAndKeepSoftEdges", "native_witnesses": ["blank_30000", "growth_padding_masks"], "complete_assertion_equivalence": False, "missing": "Exact two-hardness edge assertions and complete session sequence"},
        {"path": "CompositorTests/BrushTests.swift", "line": 434, "name": "largeBlankCanvasOnlyAllocatesTouchedTilesUntilCommit", "native_witnesses": ["blank_30000"], "complete_assertion_equivalence": False, "missing": "Exact 10000-square EditorSession input and patches API"},
        {"path": "CompositorTests/BrushPerformanceTests.swift", "line": 8, "name": "fourKInteractiveStroke", "native_witnesses": [], "complete_assertion_equivalence": False, "missing": "Supplementary growth measurements do not replace upstream workload"},
    ],
    "negative_growth_regression": {"case": "small_source_growth", "initial_timeout_log": "windows/tests/growing_brush/build-run-8.log", "coordinate_diagnostic_log": "windows/tests/growing_brush/build-run-11.log", "observed": "x=-40 phaseX=216 yielded key=-2/local256/run0 at the original compiled copy site", "fix": "int64 floor division; reject nonpositive run and local coordinates outside0..255 before copy", "standalone_probe": "windows/tests/growing_brush/signed-division-probe.log", "standalone_old_formula": "passed all8 boundaries and identical phase subtraction", "root_cause": "unresolved site-specific discrepancy; no compiler bug established; root ASAN pending"},
    "known_limits": ["Full layer retiling per event still fails frozen 16.7ms gate", "Viewport timings exclude compositor and UI presentation", "LOD point sampling may alias", "Final GrayRaster masks require flat allocation", "Placed-mask exterior uses native thumbnail approximation", "No Mac fixture, user desktop acceptance or complete upstream method equivalence"],
    "sha256": {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in dict.fromkeys(files)},
}
assert len(cases) == 15 and evidence["failed"] == 0
(here / "evidence.json").write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
print(json.dumps({"native_checks": len(cases), "failed": evidence["failed"], "benchmark_runs": len(benchmarks), "overall_parity_status": evidence["overall_parity_status"]}))

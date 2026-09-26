"""Native contract evidence is distinct from complete source and Mac parity."""
import argparse
import copy
from collections import Counter
from datetime import datetime, timezone
import hashlib
import importlib.util
import io
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
_release42_spec = importlib.util.spec_from_file_location("parity_release42", ROOT / "scripts/parity_release42.py")
release42 = importlib.util.module_from_spec(_release42_spec)
_release42_spec.loader.exec_module(release42)
_source44_spec = importlib.util.spec_from_file_location("parity_source44", ROOT / "scripts/parity_source44.py")
source44 = importlib.util.module_from_spec(_source44_spec)
_source44_spec.loader.exec_module(source44)
# Resolve the calling module's functions even under isolated importlib names;
# the new adapter has no separate global copy of ledger state.
class _Source44Host:
    def __getattr__(self, name):
        return globals()[name]
_source44_host = _Source44Host()
BASELINE = "a19db9011282399785dc18efcfded904627bdcc2"
PROOF = ROOT / "audit/native-contract-evidence.json"
REVIEW = ROOT / "audit/windows-coverage-review.json"
TILED = ROOT / "audit/tiled-acceptance-additions.json"
DOWNSAMPLE = ROOT / "audit/downsample-acceptance-additions.json"
PANEL_MAP = ROOT / "tests/layer_panel/source-assert-map.json"
EXPORT_MAP = ROOT / "tests/export_stream/evidence.json"
DISPLAY_MAP = ROOT / "tests/display_profile/evidence.json"
DISPLAY_WATCHER_MAP = ROOT / "tests/display_profile_watcher/evidence.json"
NATIVE_DISPLAY_MAP = ROOT / "tests/display_profile/native-acceptance.json"
SELECTION_MAP = ROOT / "tests/selection_gesture/source-assert-map.json"
VIEWPORT_MAP = ROOT / "audit/viewport-acceptance-additions.json"
ACCESSIBILITY_MAP = ROOT / "tests/accessibility/acceptance-additions.json"
REOPEN_MAP = ROOT / "tests/project_reopen/acceptance-additions.json"
RETOUCH_MAP = ROOT / "audit/retouch-growth-acceptance-additions.json"
PROJECT_OPEN_MAP = ROOT / "audit/project-open-acceptance-additions.json"
ADJUSTMENT_MAP = ROOT / "tests/adjustment_source/source-assert-map.json"
ADJUSTMENT_REVIEW = ROOT / "tests/adjustment_source/independent-review41.json"
LIVE_MASK_MAP = ROOT / "tests/live_mask_source/source-assert-map.json"
LIVE_MASK_REVIEW = ROOT / "tests/live_mask_source/independent-review42.json"
HUE_SOURCE_MAP = ROOT / "tests/hue_saturation_source/pending43/source-assert-map.json"
HUE_SOURCE_REVIEW = HUE_SOURCE_MAP.parent / "independent-review43.json"
DOCUMENT_SIZE_MAP = ROOT / "tests/document_size_source/pending43/source-assert-map.json"
DOCUMENT_SIZE_REVIEW = DOCUMENT_SIZE_MAP.parent / "independent-review43.json"
SUBJECT_INPUT_MAP = ROOT / "tests/imaging/input_resize42/acceptance-additions.json"
SUBJECT_INPUT_ROOT = SUBJECT_INPUT_MAP.parent
ADJUSTMENT_CHECKS = {"global_live": 14, "clipped_curve": 10, "hue_mask": 9, "persistence": 10,
    "curves_image": 5, "blend_coverage": 8, "editor_hsv": 18, "editor_levels": 21,
    "editor_curves": 19, "editor_exposure": 21, "editor_gradient_map": 21, "editor_grain": 21, "legacy_hsv": 2}
LIVE_MASK_CHECKS = {"clipping_color": 24, "option_stack": 16, "hidden_source": 10, "persistence_bake": 15, "moved_chain": 9}
HUE_SOURCE_CHECKS = {"defaults_noop": 4, "hue_ranges": 21, "colorize_alpha": 13, "selection_undo": 14,
    "preview_original": 18, "band_weights": 7, "independent_ranges": 9, "invert_range": 20,
    "range_settings": 5, "eyedroppers": 8, "targeted_drag": 7, "sampling_guards": 6}
DOCUMENT_SIZE_CHECKS = {"anchors": 133, "units": 7, "colored_extension": 20, "allocation_free": 5,
    "image_identity": 19, "resolution_export": 7, "rotated_hidden": 14}
SOURCE43_FAMILIES = ("hue_saturation_source", "document_size_source")
PAIRED_SOURCE_FAMILIES = ("adjustment_source", "live_mask_source") + SOURCE43_FAMILIES + source44.FAMILIES
SOURCE_CTEST_PREFIXES = {"adjustment_source": "adjustment_source", "live_mask_source": "source_live_mask",
    "hue_saturation_source": "source_hue_saturation", "document_size_source": "source_document_size"}
SOURCE_CTEST_PREFIXES.update(source44.PREFIXES)
COHERENT_INGESTION_FAMILIES = PAIRED_SOURCE_FAMILIES + release42.FAMILIES
SUBJECT_INPUT_CASES = ("identity", "down_even", "down_odd", "up_small", "mixed_wide", "mixed_tall", "thin_wide", "thin_tall",
    "alpha_down", "alpha_up", "constant", "transparent", "padded_stride", "cancel_before", "cancel_validation",
    "cancel_resample", "budget_reject", "invalid_premultiplication", "invalid_stride", "source_unchanged",
    "maximum_axis_memory", "dimension_preflight")
RETOUCH_EXTENT_CASES = ("clone", "heal_content", "heal_texture", "heal_proximity", "blur", "smudge", "liquify")
RETOUCH_GROWTH_CASES = ("negative_source_growth", "rotated_flipped_scaled_growth", "nil_placement_mask_white_expansion",
    "explicit_mask_placement_preserved", "mask_blur_fixed_grid", "placed_mask_small_remaining_budget", "half_selection_once",
    "empty_selection_cancel_immutable", "allocated_budget_atomic", "warp_unchanged_coverage_recomposition",
    "healing_expanded_region_seed_phase", "blank_clone_grows_and_preserves_cancel", "owned_warp_preview_mask_placement",
    "canvas_sample_preflight_limit", "healing_equal_pixels_retains_source_patch_history")
PROJECT_OPEN_CASES = ("drop_order", "drop_existing", "drop_pending_guard", "drop_mixed_failure", "batch_empty", "batch_order",
    "batch_existing", "batch_mixed_failure", "batch_all_fail", "batch_pending_guard", "batch_busy_guard", "batch_error_reentrancy",
    "welcome_fresh_session", "welcome_failure_preserves_session", "package_policy", "native_options")
PROJECT_OPEN_CASES += ("welcome_project_then_image_skipped",)
RELEASE30_CHECKPOINT = ROOT / "evidence/integration/release30-checkpoint/manifest.json"
ACCESSIBILITY_TEST_SHA256 = "17a024bd243338c08cb0b98bb373e690f8e650acfe15a643e8aadcc2be69fa86"
VIEWPORT_CACHE_CASES = ("lattice", "cache_identity_and_phase", "sparse_30k_bounds", "canonical_damage", "immutable_preview", "subpixel_grain_partition", "guards")
GRID_CASES = ("one_document_pixel_is_one_physical_pixel", "pixel_grid_absent_below_physical_zoom8", "pixel_grid_source_hairline_color_and_union_at8")
PRESENTATION_CASES = ("linear_frame", "srgb_identity", "profile_switch", "recreate_resize", "fallback", "profile_events", "budget_failure")
PRESENTATION_SCALES = {"1": 1, "1_25": 1.25, "1_5": 1.5, "2": 2}
LINEAR_PROFILE_SHA256 = "453a848b035caa68b269af3b7fcaa85649286f5f00a047ba3ae75e69abcb31a0"


def integration_observation():
    integration = ROOT / "evidence/integration"
    reports = list(integration.glob("ctest-*.xml"))
    # Coherent checkpoints now keep their full runs in releaseNN directories.
    # Do not pick focused subset XML or ASAN as the normal regression summary.
    reports.extend(integration.glob("release*/ctest-full-*.xml"))
    reports.extend(integration.glob("release*/release-full-*.xml"))
    if not reports:
        return None
    path = max(reports, key=lambda p: p.stat().st_mtime)
    root = ET.parse(path).getroot()
    cases = list(root.iter("testcase"))
    failed = [c.attrib["name"] for c in cases if c.find("failure") is not None or c.find("error") is not None]
    skipped = [c.attrib["name"] for c in cases if c.find("skipped") is not None]
    return {"path": path.relative_to(ROOT).as_posix(), "sha256": sha(path), "timestamp": root.attrib.get("timestamp"),
            "tests": len(cases), "passed": len(cases) - len(failed) - len(skipped), "failed": failed, "skipped": skipped,
            "scope": "Recorded integration run only; does not promote source contracts or certify current source hashes"}


def configuration_observations():
    if not EXPORT_MAP.exists():
        return []
    data = read(EXPORT_MAP)
    observations = data.get("instrumented_configuration_observations")
    if observations is None:
        observations = seq(data.get("instrumented_configuration_observation"))
    for observation in observations:
        if sha(local(observation["report"])) != observation["report_sha256"].lower():
            raise ValueError("Instrumented export observation report hash changed")
    return observations


def preserved_checkpoint():
    if not RELEASE30_CHECKPOINT.exists():
        return None
    data = read(RELEASE30_CHECKPOINT)
    for item in data["files"]:
        if sha(RELEASE30_CHECKPOINT.parent / item["path"]) != item["sha256"]:
            raise ValueError("Preserved Release30 artifact hash changed: " + item["path"])
    return {"path": RELEASE30_CHECKPOINT.relative_to(ROOT).as_posix(),
            "sha256": sha(RELEASE30_CHECKPOINT), "checkpoint": data["checkpoint"],
            "captured_utc": data["native_evidence_captured_utc"], "totals": data["totals"],
            "scope": "Dated archived checkpoint; does not qualify later working-tree changes"}


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def local(value):
    value = str(value).replace("\\", "/")
    if value.startswith("windows/"):
        value = value[8:]
    path = (ROOT / value).resolve()
    if not path.is_relative_to(ROOT.parent):
        raise ValueError(f"Evidence path leaves workspace: {value}")
    return path


def seq(value):
    return [] if value is None else value if isinstance(value, list) else [value]


def additions():
    release42.catalog()  # Fixed107 membership; historical manifest pass labels are not evidence.
    paths = [ROOT / "audit" / n for n in ("editing-acceptance-additions.json", "effects-tools-acceptance-additions.json")]
    paths += sorted(p for p in (ROOT / "audit").glob("*-acceptance-additions.json") if p not in paths)
    paths += [ACCESSIBILITY_MAP, REOPEN_MAP]
    if (ROOT / "audit/tiled-additions.json").exists():
        paths.append(ROOT / "audit/tiled-additions.json")
    result = []
    for path in paths:
        data = read(path)  # Missing required manifests fail.
        if data.get("baseline_sha") != BASELINE:
            raise ValueError(f"Wrong baseline: {path}")
        rows = data.get("cases", data.get("candidates"))
        if path in (TILED, DOWNSAMPLE) and isinstance(data.get("invocations"), list):
            continue  # Complete invocation maps update original IDs through the suite adapter.
        if not isinstance(rows, list):
            raise ValueError(f"Unrecognized additions schema: {path}")
        result += [(path, row) for row in rows]
    for manifest in (EXPORT_MAP, DISPLAY_MAP, DISPLAY_WATCHER_MAP, NATIVE_DISPLAY_MAP):
        if not manifest.exists():
            continue
        data = read(manifest)
        if data.get("baseline_sha") != BASELINE or not isinstance(data.get("acceptance_cases"), list):
            raise ValueError(f"Invalid native acceptance evidence: {manifest}")
        result += [(manifest, row) for row in data["acceptance_cases"]]
    for row in subject_input_contracts()["rows"]:
        result.append((SUBJECT_INPUT_MAP, dict(row, native_test=row["id"], title=row["claim"],
            manual_steps=["Run the exact named subject_input CTest with the frozen required fixtures."],
            evidence_path=["tests/imaging/input_resize42/integration-handoff.json"])))
    return result


def subject_input_contracts():
    data = read(SUBJECT_INPUT_MAP)
    protocol = read(SUBJECT_INPUT_ROOT / "protocol.json")
    if ([r["id"] for r in data.get("rows", [])] != ["subject_input." + c for c in SUBJECT_INPUT_CASES]
            or data.get("test_source_sha256") != sha(ROOT / data["test_source"])
            or data.get("protocol_sha256") != sha(SUBJECT_INPUT_ROOT / "protocol.json")
            or data.get("fixture_manifest_sha256") != sha(SUBJECT_INPUT_ROOT / "fixtures-02/manifest.json")
            or protocol["oracle"].get("normalization_tolerance_max_absolute") != .000001
            or protocol["oracle"].get("resized_rgb_max_byte_difference") != 0):
        raise ValueError("Changed subject input membership, test, recipe or frozen tolerance")
    return data


def subject_input_fixture(case, verify_hashes=True):
    if case not in SUBJECT_INPUT_CASES:
        raise ValueError("Unknown subject input case")
    directory = SUBJECT_INPUT_ROOT / "fixtures-02"
    manifest = read(directory / "manifest.json")
    protocol = read(SUBJECT_INPUT_ROOT / "protocol.json")
    if (manifest["protocol_sha256"] != sha(SUBJECT_INPUT_ROOT / "protocol.json")
            or manifest["generator_sha256"] != sha(SUBJECT_INPUT_ROOT / "generate_fixtures.py")
            or [r["id"] for r in manifest["fixtures"]] != list(SUBJECT_INPUT_CASES[:13])):
        raise ValueError("Subject input fixture manifest differs from its frozen recipe")
    fixture = case if case in SUBJECT_INPUT_CASES[:13] else "thin_tall" if case == "maximum_axis_memory" else "padded_stride" if case == "source_unchanged" else "identity"
    for row in manifest["fixtures"]:
        if {Path(item["path"]).name for item in row["files"]} != {"source.rgba", "fixture.json", "expected.rgb", "expected.f32"}:
            raise ValueError("Required subject input artifact membership changed")
        for item in row["files"]:
            path = (directory / item["path"]).resolve()
            if not path.is_relative_to(directory.resolve()) or not path.is_file() or path.stat().st_size != item["bytes"]:
                raise ValueError("Missing or malformed required subject input artifact")
            if verify_hashes and row["id"] == fixture and sha(path) != item["sha256"]:
                raise ValueError("Required subject input artifact hash differs")
    return directory / fixture, read(directory / fixture / "fixture.json"), protocol


def validate_subject_input_report(case, value, output, code):
    if value.get("schema") != "SUBJECT_INPUT_RESULT_V1" or value.get("case") != case or value.get("mode") != "candidate" or code not in (0, 1):
        raise ValueError("Malformed subject input report or process exit")
    passed = value.get("status") == "passed"
    if (code == 0) != passed or value.get("status") not in ("passed", "failed"):
        raise ValueError("Subject input process/report status disagree")
    if passed:
        if not observed_pass("subject_input." + case, output) or value.get("error"):
            raise ValueError("Subject input successful report lacks its actual PASS marker")
        if case in SUBJECT_INPUT_CASES[:13]:
            if (value.get("rgb_max_byte_difference") != 0 or value.get("rgb_differing_channels") != 0
                    or not isinstance(value.get("tensor_max_abs"), (int, float)) or not math.isfinite(value["tensor_max_abs"])
                    or not 0 <= value["tensor_max_abs"] <= .000001):
                raise ValueError("Subject input result exceeds frozen byte/float requirements")
        if case == "maximum_axis_memory" and (value.get("output_bytes") != 12582912 or not 0 < value.get("temporary_bytes", 0) <= 1048576
                or value.get("maximum_cached_rows") != 61 or value.get("prepared_source_rows") != 30000):
            raise ValueError("Subject input maximum-axis resource result differs")
        if case == "cancel_validation" and (value.get("validated_pixels") != 4096 or value.get("prepared_source_rows") != 0):
            raise ValueError("Subject input validation cancellation stage differs")
        if case == "cancel_resample" and value.get("prepared_source_rows") != 1:
            raise ValueError("Subject input resample cancellation stage differs")
        if case == "budget_reject" and (value.get("validated_pixels") != 0 or value.get("prepared_source_rows") != 0):
            raise ValueError("Subject input budget was not rejected before work")
    return passed


def source_contract_spec(family):
    if family in source44.FAMILIES:
        return source44.spec(_source44_host, family)
    if family == "adjustment_source":
        return ADJUSTMENT_MAP, ADJUSTMENT_REVIEW, ADJUSTMENT_CHECKS
    if family == "live_mask_source":
        return LIVE_MASK_MAP, LIVE_MASK_REVIEW, LIVE_MASK_CHECKS
    if family == "hue_saturation_source":
        return HUE_SOURCE_MAP, HUE_SOURCE_REVIEW, HUE_SOURCE_CHECKS
    if family == "document_size_source":
        return DOCUMENT_SIZE_MAP, DOCUMENT_SIZE_REVIEW, DOCUMENT_SIZE_CHECKS
    raise ValueError("Unknown paired source contract family")


def source_files(data):
    files = data.get("sources", [{"path": data.get("source"), "sha256": data.get("source_sha256")}])
    if not files or len({r["path"] for r in files}) != len(files):
        raise ValueError("Missing/duplicate original source file")
    for item in files:
        path = (ROOT.parent / "upstream" / item["path"]).resolve()
        if not path.is_relative_to((ROOT.parent / "upstream").resolve()) or sha(path) != item["sha256"]:
            raise ValueError("Original source hash changed")
    return files


def adjustment_contracts(family="adjustment_source"):
    if family in source44.FAMILIES:
        return source44.contracts(_source44_host, family)
    mapping, review_path, counts = source_contract_spec(family)
    invocations, executions = len(counts), sum(counts.values())
    data, review = read(mapping), read(review_path)
    files = source_files(data)
    reviewed_files = review.get("sources", [{"path": data.get("source"), "sha256": review.get("source_sha256")}])
    if (data.get("baseline_sha") != BASELINE or review.get("baseline_sha") != BASELINE
            or reviewed_files != files
            or data.get("native_source_sha256") != sha(ROOT / data["native_source"])
            or review.get("source_native_map_sha256") != sha(mapping)
            or ("native_source_sha256" in review and review["native_source_sha256"] != data["native_source_sha256"])
            or (family in SOURCE43_FAMILIES and review.get("native_source_sha256") != data["native_source_sha256"])
            or data.get("invocations") != invocations or data.get("expected_source_check_executions") != executions
            or review.get("eligible_semantic_invocations") != invocations or review.get("original_dynamic_checks") != executions):
        raise ValueError("Stale or incomplete original assertion review: " + family)
    candidate_rows = read(ROOT / "audit/acceptance-candidates.json")["cases"]
    candidates = {row["id"]: row for row in candidate_rows}
    if len(candidate_rows) != 313 or len(candidates) != 313:
        raise ValueError("Original source invocation denominator is no longer exactly313 unique IDs")
    reviewed = {r["case"]: r for r in review["rows"]}
    if (len(data["rows"]) != invocations or len(review["rows"]) != invocations or set(reviewed) != set(counts)
            or len({r["id"] for r in data["rows"]}) != invocations or {r["case"] for r in data["rows"]} != set(counts)):
        raise ValueError("Adjustment invocation/parameter membership changed")
    records = []
    for row in data["rows"]:
        case = row["case"]
        if row["id"] not in candidates or row["upstream_test_id"] != candidates[row["id"]]["upstream_test_id"] or row.get("argument") != candidates[row["id"]].get("argument"):
            raise ValueError("Adjustment map changed the original313 invocation denominator")
        if family in SOURCE43_FAMILIES:
            original = candidates[row["id"]]
            assertions = [(c["source"], c["kind"], c["expression"]) for c in row["assertions"]]
            expected = [(c["source"], c["kind"].lstrip("#"), c["expression"]) for c in original["assertions"]]
            if row["source"] != original["upstream_evidence"] or assertions != expected:
                raise ValueError("Original invocation/assertion source, order or expression changed")
        count = sum(c["expected_executions"] for c in row["assertions"] + row["helper_assertions"])
        if count != counts[case] or reviewed[case]["source_assertions_and_helpers"] != count:
            raise ValueError("Adjustment assertion/helper dynamic multiplicity changed")
        if family in SOURCE43_FAMILIES and reviewed[case].get("eligible") is not True:
            raise ValueError("Original source invocation is not independently eligible")
        hashes = {r["path"]: r["sha256"] for r in files}
        if row["source"]["path"] not in hashes:
            raise ValueError("Invocation source is outside the reviewed original files")
        records.append({"id": row["id"], "upstream_test_id": row["upstream_test_id"], "source": row["source"],
            "argument": row.get("argument"), "native_tests": [family + "." + case], "assertions": row["assertions"],
            "helper_assertions": row["helper_assertions"], "expected_source_check_executions": count,
            "reference_kind": "native_render_invariants", "review_path": review_path.relative_to(ROOT).as_posix(),
            "review_sha256": sha(review_path), "assertion_map": {"path": mapping.relative_to(ROOT).as_posix(), "sha256": sha(mapping)},
            "source_file": {"path": "../upstream/" + row["source"]["path"], "sha256": hashes[row["source"]["path"]]},
            "claim": "Every original assertion/helper and parameter conjunction is reviewed; complete native credit requires matching normal and ASAN source/binary/report evidence. No Mac differential claim."})
        if family in SOURCE43_FAMILIES:
            records[-1]["source_files"] = [{"path": "../upstream/" + f["path"], "sha256": f["sha256"]} for f in files]
    return records


def validate_adjustment_report(case, value, output, code, family="adjustment_source"):
    if family in source44.FAMILIES:
        result, count, _ = source44.validate_report(_source44_host, family, case, value, output, code)
        return result == "passed", count
    mapping, _, _ = source_contract_spec(family)
    row = next(r for r in read(mapping)["rows"] if r["case"] == case)
    schema = {"live_mask_source": "LIVE_MASK_SOURCE_RESULT_V1", "hue_saturation_source": "HUE_SATURATION_SOURCE_RESULT_V1",
              "document_size_source": "DOCUMENT_SIZE_SOURCE_RESULT_V1"}.get(family)
    if schema and value.get("schema") != schema:
        raise ValueError("Wrong source report schema")
    multiple_files = family == "document_size_source"
    def location(path, line, kind):
        return (path, line, kind) if multiple_files else (line, kind)
    expected = Counter()
    for check in row["assertions"]:
        expected[location(check["source"]["path"], check["source"]["line"], check["kind"])] += check["expected_executions"]
    for check in row["helper_assertions"]:
        expected[location(check.get("source_file"), check["source_line"], check["kind"])] += check["expected_executions"]
    if value.get("case") != case or value.get("status") not in ("passed", "failed", "incomplete", "error") or code not in (0, 1, 2):
        raise ValueError("Malformed adjustment scenario status/exit")
    checks = value.get("checks")
    if not isinstance(checks, list) or any(type(c.get("source_line")) is not int or c.get("kind") not in ("require", "expect") or type(c.get("passed")) is not bool for c in checks):
        raise ValueError("Malformed adjustment assertion record")
    actual = Counter(location(c.get("source_file"), c["source_line"], c["kind"]) for c in checks)
    failure_key = "failed_checks" if multiple_files else "source_expect_failures"
    if actual - expected or type(value.get(failure_key)) is not int or value[failure_key] != sum(not c["passed"] for c in checks):
        raise ValueError("Adjustment report contains unknown/duplicated checks or false failure count")
    if multiple_files and (type(value.get("check_count")) is not int or value["check_count"] != len(checks) or type(value.get("aborted")) is not bool):
        raise ValueError("Malformed document-size count/abort state")
    passed = value["status"] == "passed"
    if (code == 0) != passed:
        raise ValueError("Adjustment status and actual exit disagree")
    if passed and (actual != expected or value.get("error") or value.get("aborted") or not all(c["passed"] for c in checks)
            or not observed_pass(family + "." + case, output)):
        raise ValueError("Adjustment success omits an original check/helper/parameter or actual marker")
    return passed, len(checks)


def adjustment_extraction(path, require_current=True, family="adjustment_source"):
    if family in source44.FAMILIES:
        return source44.extraction(_source44_host, path, require_current, family)
    mapping, _, counts = source_contract_spec(family)
    data = read(path)
    manifest_path, junit_path = local(data["source_manifest"]), local(data["junit"])
    if (sha(manifest_path) != data["source_manifest_sha256"] or sha(junit_path) != data["junit_sha256"]
            or data.get("mapping_sha256") != sha(mapping)):
        raise ValueError("Adjustment extraction manifest, JUnit or assertion-map fingerprint changed")
    manifest = read(manifest_path)
    if (len(manifest["files"]) != data["configured_inputs"]
            or len({local(item["path"]) for item in manifest["files"]}) != len(manifest["files"])):
        raise ValueError("Adjustment extraction build-input count differs")
    if family in SOURCE43_FAMILIES:
        inputs = {local(item["path"]): item["sha256"].lower() for item in manifest["files"]}
        native = read(mapping)
        if inputs.get(local(native["native_source"])) != native["native_source_sha256"]:
            raise ValueError("Source witness differs from or is absent from the coherent build")
        _, review, _ = source_contract_spec(family)
        # The separately pinned assertion map/review are not compiled inputs.
        # Every production/configuration dependency must occur in the frozen
        # build manifest; a shortened manifest cannot qualify an old executable.
        for required in set(dependencies(family)) - {mapping, review}:
            if required not in inputs:
                raise ValueError("Required production/configuration input missing from coherent build: " + str(required))
            if require_current and sha(required) != inputs[required]:
                raise ValueError("Required production/configuration input changed: " + str(required))
    if require_current:
        for item in manifest["files"]:
            if not local(item["path"]).is_file() or sha(local(item["path"])) != item["sha256"].lower():
                raise ValueError("Stale adjustment build input: " + item["path"])
    binaries = [item for item in data["runtime_post"] if local(item["path"]).name == family + "_tests.exe"]
    if len(binaries) != 1:
        raise ValueError("Missing/duplicate actual adjustment executable fingerprint")
    binary = binaries[0]
    if require_current and (not local(binary["path"]).is_file() or sha(local(binary["path"])) != binary["sha256"]):
        raise ValueError("Stale adjustment executable")
    if family in SOURCE43_FAMILIES:
        validate_source_runtime_pair(data, binary, require_current)
    tests = {}
    for item in ET.parse(junit_path).getroot().iter("testcase"):
        name = item.attrib["name"]
        if name in tests:
            raise ValueError("Duplicate adjustment JUnit name")
        tests[name] = item
    summary_rows = [item for item in data["rows"] if item["id"].startswith(family + ".")]
    if family in SOURCE43_FAMILIES and (data.get("family") != family or len(summary_rows) != len(data["rows"])):
        raise ValueError("Source extraction contains a wrong/mixed family")
    summaries = {item["id"].split(".", 1)[1]: item for item in summary_rows}
    if len(summary_rows) != len(counts) or set(summaries) != set(counts):
        raise ValueError("Adjustment extraction omits a required source invocation")
    observations = {}
    for case in counts:
        summary = summaries[case]
        ctest_name = SOURCE_CTEST_PREFIXES[family] + "." + case
        junit = tests[ctest_name]
        output = junit.findtext("system-out") or ""
        if junit.find("skipped") is not None or junit.attrib.get("status") == "notrun":
            raise ValueError("Adjustment source invocation was skipped")
        passed = junit.find("failure") is None and junit.find("error") is None
        report_path = local(summary["raw_report"])
        if family in SOURCE43_FAMILIES:
            folder = {"hue_saturation_source": "hue-source-evidence", "document_size_source": "document-size-source-evidence"}[family]
            expected_root = local(binary["path"]).parent.parent / folder / case
            if not report_path.is_relative_to(expected_root):
                raise ValueError("Source report belongs to a different executable configuration/case")
        if sha(report_path) != summary["raw_report_sha256"]:
            raise ValueError("Changed actual adjustment assertion report")
        value = read(report_path)
        code = 0 if passed else 2 if value.get("status") in ("error", "incomplete") else 1
        actual, count = validate_adjustment_report(case, value, output, code, family)
        markers = re.findall(r"^SOURCE_REPORT (.+)/result.json checks=(\d+)\s*$", output, re.MULTILINE)
        if (len(markers) != 1 or local(markers[0][0]) / "result.json" != report_path or int(markers[0][1]) != count
                or actual != passed or summary["result"] != ("passed" if passed else "failed")
                or summary.get("source_check_executions") != count):
            raise ValueError("Adjustment JUnit, extraction and actual report disagree")
        observations[case] = {"result": "passed" if passed else "failed", "exit_code": code,
            "output": output, "output_sha256": hashlib.sha256(output.encode()).hexdigest(),
            "command": [local(binary["path"]).relative_to(ROOT).as_posix(), case, report_path.relative_to(ROOT).as_posix()],
            "executable_sha256": binary["sha256"], "result_file": {"path": report_path.relative_to(ROOT).as_posix(), "sha256": sha(report_path)},
            "junit": {"path": junit_path.relative_to(ROOT).as_posix(), "sha256": sha(junit_path)},
            "source_check_executions": count}
    if sum(row["source_check_executions"] for row in observations.values()) != data["source_check_executions"]:
        raise ValueError("Adjustment aggregate dynamic check count disagrees")
    return data, observations


def source_runtime_path(value):
    path = Path(str(value).replace("\\", "/"))
    if path.is_absolute() and path.name.lower() == "clang_rt.asan_dynamic-x86_64.dll":
        # The compiler's sanitizer DLL is loaded from the configured MSVC bin,
        # outside the project. This exception permits fingerprint reads only.
        return path.resolve()
    return local(value)


def validate_source_runtime_pair(data, binary, require_current=True):
    pair = data.get("runtime_pair") or {}
    snapshots = []
    for key in ("before", "after"):
        item = pair.get(key) or {}
        path = local(item.get("path", ""))
        if not path.is_file() or sha(path) != item.get("sha256"):
            raise ValueError("Missing/changed pre/post runtime manifest")
        rows = read(path)
        if not isinstance(rows, list) or not rows:
            raise ValueError("Empty runtime manifest")
        index = {source_runtime_path(r["path"]): r["sha256"].lower() for r in rows}
        if len(index) != len(rows) or any(not re.fullmatch(r"[0-9a-f]{64}", s) for s in index.values()):
            raise ValueError("Duplicate/malformed runtime manifest entries")
        snapshots.append(index)
    before, after = snapshots
    executable = local(binary["path"])
    if before != after or before.get(executable) != binary["sha256"]:
        raise ValueError("Pre/post executable/runtime fingerprints differ")
    sanitizer = {p for p in before if p.name.lower() == "clang_rt.asan_dynamic-x86_64.dll"}
    instrumented = b"clang_rt.asan" in executable.read_bytes().lower()
    if instrumented and len(sanitizer) != 1:
        raise ValueError("Instrumented source observation lacks one pre/post sanitizer runtime DLL fingerprint")
    # The full before/after manifests retain all targets and static libraries.
    # Current freshness checks cover this executed image, loaded DLL candidates,
    # Qt platform plugins and shaders, rather than unrelated test executables.
    qt = (ROOT / "dependencies/qt").resolve()
    # Match the full-suite driver's runtime inventory. It includes all bundled
    # Qt modules/platform plugins and shaders, but unrelated test executables
    # and static libraries do not become current runtime dependencies here.
    required = {executable}
    if instrumented:
        required.update(sanitizer)
    required.update(executable.parent / name for name in
                    ("onnxruntime.dll", "heif.dll", "libde265.dll", "onnxruntime_providers_shared.dll"))
    required.update((qt / "bin").glob("*.dll"))
    required.update((qt / "plugins/platforms").glob("*.dll"))
    required.update(path for path in (ROOT / "shaders").iterdir() if path.is_file())
    if not required <= before.keys():
        raise ValueError("Required pinned/Qt/shader runtime membership was omitted")
    relevant = {p: digest for p, digest in before.items() if p == executable or p in sanitizer
                or (p.suffix.lower() == ".dll" and (p.parent == executable.parent or p.is_relative_to(qt)))
                or p.is_relative_to((ROOT / "shaders").resolve())}
    names = {p.name.lower() for p in relevant}
    if not {"onnxruntime.dll", "heif.dll", "libde265.dll", "onnxruntime_providers_shared.dll",
            "qt6core.dll", "qt6gui.dll", "qt6widgets.dll", "qt6test.dll", "qwindows.dll", "qoffscreen.dll"} <= names:
        raise ValueError("Required pinned/Qt runtime fingerprints are absent")
    if require_current:
        for path, digest in relevant.items():
            if not path.is_file() or sha(path) != digest:
                raise ValueError("Stale executed source-family runtime: " + str(path))
    return relevant


def ingest_adjustment_source(normal_path, asan_path, family="adjustment_source"):
    """Consume coherent, current normal+ASAN reports. Never substitute an old pass."""
    if family in source44.FAMILIES:
        return source44.ingest(_source44_host, normal_path, asan_path, family)
    _, _, counts = source_contract_spec(family)
    contracts = adjustment_contracts(family)
    normal, normal_rows = adjustment_extraction(local(normal_path), family=family)
    asan, asan_rows = adjustment_extraction(local(asan_path), family=family)
    if normal["source_manifest_sha256"] != asan["source_manifest_sha256"]:
        raise ValueError("Normal and ASAN adjustment builds have different frozen inputs")
    # This explicit Windows configuration check is backed by the executed binary
    # hash, not just a directory name or a caller-provided ASAN label.
    for rows, instrumented in ((normal_rows, False), (asan_rows, True)):
        image = local(next(iter(rows.values()))["command"][0]).read_bytes().lower()
        if (b"clang_rt.asan" in image) != instrumented:
            raise ValueError("Adjustment normal/ASAN executable configuration is incorrect")
    proof = read(PROOF) if PROOF.exists() else {}
    if proof.get("baseline_sha") != BASELINE or not proof.get("runtime_files"):
        raise ValueError("A canonical runtime-fingerprinted proof is required before scenario ingestion")
    for item in proof["runtime_files"]:
        if not local(item["path"]).is_file() or sha(local(item["path"])) != item["sha256"]:
            raise ValueError("Canonical deployed runtime proof is stale")
    groups = proof.setdefault("dependency_groups", {})
    groups[family] = [{"path": p.relative_to(ROOT).as_posix(), "sha256": sha(p)} for p in dependencies(family)]
    records = [r for r in proof["runs"] if r["dependency_group"] != family]
    for case in counts:
        primary, sanitizer = normal_rows[case], asan_rows[case]
        passed = primary["result"] == sanitizer["result"] == "passed"
        records.append(dict(primary, native_test=family + "." + case,
            result="passed" if passed else "failed", normal_result=primary["result"], dependency_group=family,
            expected_pass_marker_observed=observed_pass(family + "." + case, primary["output"]),
            sanitizer=sanitizer, timeout_seconds=105 if family in SOURCE43_FAMILIES else 60,
            ingestion="verified normal and ASAN coherent full-suite reports"))
    archive = ROOT / "evidence/integration/native-contract-history" / (sha(PROOF) + ".json")
    archive.parent.mkdir(parents=True, exist_ok=True)
    if not archive.exists():shutil.copyfile(PROOF, archive)
    elif sha(archive) != sha(PROOF):raise ValueError("Prior proof archive differs")
    proof.update(previous_capture={"path": archive.relative_to(ROOT).as_posix(), "sha256": sha(archive)},
        captured_utc=datetime.now(timezone.utc).isoformat(), runs=records)
    ids = {row["id"] for row in contracts}
    proof["complete_source_contracts"] = [r for r in proof.get("complete_source_contracts", []) if r["id"] not in ids] + contracts
    proof[family + "_ingestion"] = {
        "normal": {"path": local(normal_path).relative_to(ROOT).as_posix(), "sha256": sha(local(normal_path))},
        "asan": {"path": local(asan_path).relative_to(ROOT).as_posix(), "sha256": sha(local(asan_path))},
        "source_manifest": {"path": local(normal["source_manifest"]).relative_to(ROOT).as_posix(), "sha256": normal["source_manifest_sha256"]},
        "invocations": len(counts), "required_checks_per_configuration": sum(counts.values())}
    write(PROOF, proof)


def ingest_release42_bounded(bundle_path):
    bundle_path = local(bundle_path)
    bundle = release42.validate_bundle(bundle_path)
    proof = read(PROOF) if PROOF.exists() else {}
    if proof.get("baseline_sha") != BASELINE or not proof.get("runtime_files"):
        raise ValueError("A canonical runtime-fingerprinted proof is required before bounded ingestion")
    for item in proof["runtime_files"]:
        if sha(local(item["path"])) != item["sha256"]:
            raise ValueError("Canonical deployed runtime proof is stale")
    archive = ROOT / "evidence/integration/native-contract-history" / (sha(PROOF) + ".json")
    archive.parent.mkdir(parents=True, exist_ok=True)
    if not archive.exists():shutil.copyfile(PROOF, archive)
    elif sha(archive) != sha(PROOF):raise ValueError("Prior proof archive differs")
    groups = proof.setdefault("dependency_groups", {})
    common = [{"path": p.relative_to(ROOT).as_posix(), "sha256": sha(p)} for p in release42.dependencies()]
    for family in release42.FAMILIES:
        groups[family] = copy.deepcopy(common)
    proof["runs"] = [r for r in proof["runs"] if r.get("dependency_group") not in release42.FAMILIES] + bundle["rows"]
    proof["release42_bounded_ingestion"] = {"path": bundle_path.relative_to(ROOT).as_posix(), "sha256": sha(bundle_path), "records": 107}
    proof.update(previous_capture={"path": archive.relative_to(ROOT).as_posix(), "sha256": sha(archive)},
                 captured_utc=datetime.now(timezone.utc).isoformat())
    # complete_source_contracts is deliberately untouched: these107 are bounded additions.
    write(PROOF, proof)


def retain_paired_contracts(records, retained):
    """A full paired suite supersedes an older witness of the same original ID."""
    result = {r["id"]: r for r in records}
    if len(result) != len(records):
        raise ValueError("Duplicate original source contract before paired preservation")
    active44 = {n.split(".", 1)[0] for r in retained for n in r.get("native_tests", [])} & set(source44.FAMILIES)
    for family in active44:
        eligible_ids = {r["id"] for r in source44.contracts(_source44_host, family)}
        for row in source44.contracts(_source44_host, family, False):
            if row["id"] not in eligible_ids:
                result.pop(row["id"], None)
    seen = set()
    for row in retained:
        families = {n.split(".", 1)[0] for n in row.get("native_tests", [])}
        if not families & set(PAIRED_SOURCE_FAMILIES):
            continue
        if row["id"] in seen or len(families) != 1:
            raise ValueError("Duplicate/mixed paired original contract")
        seen.add(row["id"])
        previous = result.get(row["id"])
        if previous and previous["upstream_test_id"] != row["upstream_test_id"]:
            raise ValueError("Paired preservation changed the original invocation identity")
        result[row["id"]] = row
    return list(result.values())


def capture_subject_inputs(build_dir, config, env, names):
    records = []
    executable = build_dir / config / "subject_input_tests.exe"
    for name in names:
        if not name.startswith("subject_input."):continue
        case = name.split(".", 1)[1]
        fixture, meta, _ = subject_input_fixture(case)
        directory = ROOT / "evidence/integration/subject-input-ledger" / (datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S-%f") + "-" + case)
        directory.mkdir(parents=True, exist_ok=False)
        report_path = directory / "result.json"
        args = ["candidate", str(fixture), str(meta["width"]), str(meta["height"]), str(meta["stride"]), case,
                str(report_path), "conformance" if case in SUBJECT_INPUT_CASES[:13] else "resource"]
        run = subprocess.run([str(executable), *args], cwd=ROOT, env=env, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=150)
        output = run.stdout + run.stderr
        (directory / "output.log").write_text(output, encoding="utf-8")
        value = read(report_path)
        passed = validate_subject_input_report(case, value, output, run.returncode)
        records.append({"native_test": name, "result": "passed" if passed else "failed", "exit_code": run.returncode,
            "command": [executable.relative_to(ROOT).as_posix(), *args], "executable_sha256": sha(executable),
            "expected_pass_marker_observed": observed_pass(name, output), "dependency_group": "subject_input", "timeout_seconds": 150,
            "output": output, "output_sha256": hashlib.sha256(output.encode()).hexdigest(),
            "result_file": {"path": report_path.relative_to(ROOT).as_posix(), "sha256": sha(report_path)}})
        print(records[-1]["result"].upper(), name, flush=True)
    return records


def implementations(name):
    family = name.split(".")[0]
    if family == "selection_source":
        return ["src/ui/SelectionActions.cpp", "src/ui/KeyboardActions.cpp", "src/editing/Selection.cpp", "src/editing/SelectionGesture.cpp", "src/core/Document.cpp"]
    if family == "palette_source":
        return ["src/ui/PaletteActions.cpp", "src/ui/PaletteDialog.cpp", "src/effects_tools/Color.cpp"]
    if family in release42.FAMILIES:
        return release42.PRODUCTION[family]
    if family == "live_mask_source":
        return ["src/layers/LayerOperations.cpp", "src/graphics/StackRenderer.cpp", "src/core/Document.cpp", "src/render/SoftwareRenderer.cpp", "src/persistence/ProjectStore.cpp"]
    if family == "hue_saturation_source":
        return ["src/ui/AdjustmentDialog.cpp", "src/ui/AdjustmentActions.cpp", "src/effects/Adjustments.cpp",
                "src/effects_tools/Hue.cpp", "src/core/Document.cpp"]
    if family == "document_size_source":
        return ["src/editing/DocumentGeometry.cpp", "src/core/Document.cpp", "src/persistence/ProjectStore.cpp",
                "src/imaging/wic_codec.cpp", "src/core/DocumentExport.cpp"]
    if family == "subject_input":
        return ["src/imaging/subject_input.cpp", "src/imaging/onnx_subject_provider.cpp"]
    if family == "adjustment_source":
        return ["src/ui/AdjustmentDialog.cpp", "src/ui/AdjustmentActions.cpp", "src/ui/LayerActions.cpp", "src/effects/Adjustments.cpp", "src/core/Document.cpp", "src/persistence/ProjectStore.cpp"]
    if family in ("retouch_extent", "retouch_growth"):
        return ["src/retouch/RetouchSession.cpp", "src/graphics/GrowingBrushSession.cpp"]
    if family == "project_open_batch":
        return ["src/ui/ProjectOpenDialog.cpp", "src/ui/FileActions.cpp", "src/ui/ProjectActions.cpp", "src/persistence/ProjectStore.cpp"]
    if family == "project_reopen":
        return ["src/ui/FileActions.cpp", "src/ui/ProjectActions.cpp", "src/persistence/ProjectStore.cpp"]
    if family == "accessibility":
        return ["src/ui/CanvasAccessibility.cpp", "src/ui/NativeCanvas.cpp", "src/ui/LayerPanel.cpp", "src/ui/AdjustmentDialog.cpp", "src/ui/MainWindow.cpp"]
    if family in ("viewport_cache", "physical_viewport", "native_pixel_grid"):
        return ["src/core/CompositeCache.cpp", "src/graphics/StackRenderer.cpp", "src/ui/NativeCanvas.cpp"]
    if family == "imaging_longpath":
        return ["src/imaging/win32_file_path.h", "src/imaging/wic_codec.cpp", "src/imaging/project_png.cpp", "src/imaging/stream_export.cpp"]
    if family == "selection_gesture":
        return ["src/editing/SelectionGesture.cpp", "src/editing/Selection.cpp", "src/core/Document.cpp"]
    if family in ("selection_events", "selection_followup"):
        return ["src/editing/SelectionGesture.cpp", "src/ui/SelectionActions.cpp", "src/ui/NativeCanvas.cpp", "src/ui/KeyboardActions.cpp", "src/ui/BrushActions.cpp", "src/ui/MainWindow.cpp"]
    if family == "display_profile":
        return ["src/platform/DisplayProfile.cpp"]
    if family == "display_profile_watcher":
        return ["src/platform/DisplayProfileWatcher.cpp"]
    if family == "native_display_profile":
        return ["src/ui/CanvasPresentation.cpp", "src/ui/NativeCanvas.cpp", "src/platform/DisplayProfile.cpp", "src/platform/DisplayProfileWatcher.cpp"]
    if family in ("remembered_settings", "remembered_filter"):
        return ["src/ui/ProjectToolState.cpp", "src/ui/DrawingActions.cpp", "src/ui/SelectionActions.cpp", "src/ui/FilterActions.cpp", "src/ui/AdjustmentActions.cpp", "src/ui/AdjustmentDialog.cpp", "src/imaging/SubjectDialog.cpp"]
    if family == "downsample":
        return ["src/graphics/Downsample.cpp", "src/graphics/StackRenderer.cpp", "src/core/CompositeCache.cpp"]
    if family == "tiled":
        return ["src/core/CompositeCache.cpp", "src/graphics/GrowingBrushSession.cpp", "src/graphics/StackRenderer.cpp", "src/ui/NativeCanvas.cpp", "src/ui/BrushActions.cpp"]
    if family == "layer_panel":
        return ["src/ui/LayerPanel.cpp", "src/ui/LayerActions.cpp", "src/ui/MaskActions.cpp"]
    if family in ("export_stream", "export_ui"):
        return ["src/core/DocumentExport.cpp", "src/imaging/stream_export.cpp"] + (["src/ui/FileActions.cpp"] if family == "export_ui" else [])
    if family == "adjustment_dialog":
        return ["src/ui/AdjustmentDialog.cpp", "src/ui/AdjustmentAdvancedControls.cpp"]
    folder = {"editing": "src/editing", "effects_tools": "src/effects_tools"}.get(family)
    return sorted(p.relative_to(ROOT).as_posix() for p in (ROOT / folder).glob("*.cpp")) if folder else []


def binding(name):
    family, argument = name.split(".", 1)
    if family in release42.FAMILIES:
        if argument not in release42.CASES[family]:
            raise ValueError("Unknown coherent Release42 case: " + name)
        return release42.TARGETS[family], argument
    target = {"editing": "editing_tests", "effects_tools": "effects_tools_tests", "adjustment_dialog": "adjustment_dialog_tests",
              "layer_panel": "layer_panel_tests", "export_stream": "export_stream_tests", "export_ui": "export_ui_tests", "display_profile": "display_profile_tests",
              "decoder_adversarial": "decoder_adversarial_tests", "process_save": "process_save_tests", "display_profile_watcher": "display_profile_watcher_tests",
              "native_display_profile": "native_display_profile_tests", "remembered_settings": "remembered_settings_tests",
              "remembered_filter": "remembered_filter_tests", "viewport_cache": "viewport_sampling_tests",
              "physical_viewport": "physical_viewport_tests", "native_pixel_grid": "native_pixel_grid_tests", "accessibility": "accessibility_witness",
              "project_reopen": "project_reopen_tests"}.get(family)
    target = target or {"imaging_longpath": "imaging_longpath_tests", "selection_gesture": "selection_gesture_tests", "selection_events": "selection_event_tests", "selection_followup": "selection_followup_tests"}.get(family)
    target = target or {"retouch_extent": "retouch_extent_tests", "retouch_growth": "retouch_growth_tests", "project_open_batch": "project_open_batch_tests"}.get(family)
    target = target or {"subject_input": "subject_input_tests", "adjustment_source": "adjustment_source_tests", "live_mask_source": "live_mask_source_tests"}.get(family)
    if family in SOURCE43_FAMILIES + source44.FAMILIES:
        target = family + "_tests"
    if not target:
        raise ValueError(f"No reviewed execution adapter for {name}; manifest commands are never executed as shell text")
    return target, argument


def dependencies(family):
    if family in source44.FAMILIES:
        return source44.dependencies(_source44_host, family)
    if family in release42.FAMILIES:
        return release42.dependencies()
    if family in PAIRED_SOURCE_FAMILIES:
        mapping, review, _ = source_contract_spec(family)
        files = {mapping, review, ROOT / read(mapping)["native_source"],
                 mapping.parent / "run-case.cmake", ROOT / "CMakeLists.txt"}
        if family in SOURCE43_FAMILIES:
            files.add(ROOT / "tests/PortExpansionTests.cmake")
        files.update(p for p in (ROOT / "src").rglob("*") if p.suffix in (".h", ".cpp", ".c", ".hlsl"))
        return sorted(files)
    if family == "subject_input":
        files = {SUBJECT_INPUT_MAP, ROOT / "src/imaging/subject_input.h", ROOT / "src/imaging/subject_input.cpp",
            ROOT / "src/imaging/image_types.h", ROOT / "src/imaging/onnx_subject_provider.cpp", ROOT / "CMakeLists.txt"}
        files.update(SUBJECT_INPUT_ROOT / p for p in ("InputResizeTests.cpp", "protocol.json", "generate_fixtures.py", "run-case.py", "fixtures-02/manifest.json"))
        for row in read(SUBJECT_INPUT_ROOT / "fixtures-02/manifest.json")["fixtures"]:
            files.update(SUBJECT_INPUT_ROOT / "fixtures-02" / item["path"] for item in row["files"])
        return sorted(files)
    if family in ("retouch_extent", "retouch_growth"):
        harness = "ExtentAfterWitness.cpp" if family == "retouch_extent" else "GrowthContracts.cpp"
        files = {ROOT / "tests/retouch_extent" / harness, RETOUCH_MAP, ROOT / "shaders/BrushCoverage.hlsl", ROOT / "CMakeLists.txt"}
        if family == "retouch_extent":
            files.add(ROOT / "tests/retouch_extent/run_witness.cmake")
        for folder in ("src/core", "src/graphics", "src/retouch", "src/render", "src/effects", "src/effects_tools"):
            files.update(p for p in (ROOT / folder).rglob("*") if p.suffix in (".h", ".cpp", ".c", ".hlsl"))
        return sorted(files)
    if family == "project_open_batch":
        files = {PROJECT_OPEN_MAP, ROOT / "tests/project_open_batch/ProjectOpenBatchTests.cpp", ROOT / "tests/project_open_batch/BeforeOpenBatchTests.cpp", ROOT / "CMakeLists.txt"}
        files.update(p for p in (ROOT / "src").rglob("*") if p.suffix in (".h", ".cpp", ".c", ".hlsl"))
        return sorted(files)
    if family == "project_reopen":
        files = {ROOT / "tests/project_reopen/ProjectReopenTests.cpp", REOPEN_MAP}
        files.update(p for p in (ROOT / "src").rglob("*") if p.suffix in (".h", ".cpp", ".c", ".hlsl"))
        return sorted(files)
    if family == "accessibility":
        files = {ROOT / "tests/accessibility/AccessibilityWitness.cpp", ACCESSIBILITY_MAP}
        files.update(ROOT / p for p in ("dependencies/qt/bin/Qt6Core.dll", "dependencies/qt/bin/Qt6Gui.dll", "dependencies/qt/bin/Qt6Widgets.dll", "dependencies/qt/bin/Qt6Test.dll", "dependencies/qt/plugins/platforms/qwindows.dll"))
        files.update(p for p in (ROOT / "src").rglob("*") if p.suffix in (".h", ".cpp", ".c", ".hlsl"))
        return sorted(files)
    if family in ("viewport_cache", "physical_viewport", "native_pixel_grid"):
        harness = {"viewport_cache": "ViewportSamplingTests.cpp", "physical_viewport": "PhysicalViewportTests.cpp", "native_pixel_grid": "NativePixelGridTests.cpp"}[family]
        files = {ROOT / "tests/viewport_sampling" / harness, VIEWPORT_MAP}
        # Conservative full-link source identity; independent UI edits can make
        # these runs stale, but cannot silently qualify an unexecuted build.
        files.update(p for p in (ROOT / "src").rglob("*") if p.suffix in (".h", ".cpp", ".c", ".hlsl"))
        return sorted(files)
    if family in ("remembered_settings", "remembered_filter"):
        files = {ROOT / "tests/session_state/RememberedSettingsTests.cpp"}
        if family == "remembered_filter":
            files.update(ROOT / p for p in ("tests/session_state/RememberedFilterTests.cpp", "evidence/imaging/astronaut.png", "dependencies/imaging/model/birefnet-lite.onnx"))
        files.update(p for p in (ROOT / "src").rglob("*") if p.suffix in (".h", ".cpp", ".c", ".hlsl"))
        return sorted(files)
    if family == "native_display_profile":
        files = {ROOT / p for p in ("tests/display_profile/NativeDisplayProfileTests.cpp", "tests/display_profile/fixtures/linear-rgb.icc",
            "tests/display_profile/fixtures/manifest.json", "dependencies/qt/bin/Qt6Core.dll", "dependencies/qt/bin/Qt6Gui.dll",
            "dependencies/qt/bin/Qt6Widgets.dll", "dependencies/qt/bin/Qt6Test.dll", "dependencies/qt/plugins/platforms/qwindows.dll")}
        files.update(p for p in (ROOT / "src").rglob("*") if p.suffix in (".h", ".cpp", ".c", ".hlsl"))
        return sorted(files)
    if family == "display_profile_watcher":
        return sorted(ROOT / p for p in ("src/platform/DisplayProfileWatcher.h", "src/platform/DisplayProfileWatcher.cpp",
            "tests/display_profile_watcher/DisplayProfileWatcherTests.cpp", "tests/display_profile_watcher/CMakeLists.txt", "dependencies/qt/bin/Qt6Core.dll"))
    if family == "imaging_longpath":
        return sorted([ROOT / "tests/imaging/LongPathTests.cpp"] + [p for p in (ROOT / "src/imaging").rglob("*") if p.suffix in (".h", ".cpp")])
    if family in ("decoder_adversarial", "process_save"):
        folders = ["src/core", "src/persistence", "src/imaging"]
        files = {ROOT / ("tests/imaging/DecoderAdversarialTests.cpp" if family == "decoder_adversarial" else "tests/persistence/ProcessSaveTests.cpp")}
        if family == "decoder_adversarial":
            files.update(ROOT / p for p in ("src/ui/ImportActions.h", "src/ui/ImportActions.cpp", "dependencies/imaging/lock.json",
                "dependencies/imaging/libheif/tests/data/with-alpha-512x512.heic", "dependencies/imaging/install/bin/heif.dll", "dependencies/imaging/install/bin/libde265.dll"))
        for folder in folders:
            files.update(p for p in (ROOT / folder).rglob("*") if p.suffix in (".h", ".cpp", ".c", ".hlsl"))
        return sorted(files)
    if family == "display_profile":
        return sorted(ROOT / p for p in ("src/platform/DisplayProfile.h", "src/platform/DisplayProfile.cpp", "src/imaging/image_types.h",
            "tests/display_profile/DisplayProfileTests.cpp", "tests/display_profile/fixtures/linear-rgb.icc", "tests/display_profile/fixtures/manifest.json"))
    if family in ("tiled", "downsample", "layer_panel", "export_ui", "selection_events", "selection_followup"):
        files = {ROOT / {"tiled": "tests/tiled_parity/TiledLayerParityTests.cpp", "downsample": "tests/downsample/DownsampleTests.cpp", "layer_panel": "tests/layer_panel/LayerPanelSourceTests.cpp", "export_ui": "tests/export_stream/ExportUiTests.cpp", "selection_events": "tests/selection_gesture/SelectionEventTests.cpp", "selection_followup": "tests/selection_gesture/SelectionFollowupTests.cpp"}[family]}
        if family == "tiled": files.add(TILED)
        if family == "downsample": files.add(DOWNSAMPLE)
        if family == "layer_panel": files.add(PANEL_MAP)
        files.update(p for p in (ROOT / "src").rglob("*") if p.suffix in (".h", ".cpp", ".c", ".hlsl"))
        return sorted(files)
    folders = ["src/core", "src/graphics", "src/render", "src/effects", "src/effects_tools"]
    if family == "export_stream":
        extra = ["tests/export_stream/ExportStreamTests.cpp", "src/imaging/stream_export.h", "src/imaging/stream_export.cpp", "src/imaging/wic_codec.h", "src/imaging/wic_codec.cpp", "src/imaging/image_types.h", "src/imaging/win32_file_path.h"]
    elif family in ("editing", "selection_gesture"):
        folders += ["src/editing"]
        extra = ["tests/editing/editing_tests.cpp"] if family == "editing" else ["tests/selection_gesture/SelectionGestureTests.cpp", SELECTION_MAP.relative_to(ROOT).as_posix()]
    else:
        extra = ["tests/effects_tools/effects_tools_tests.cpp" if family == "effects_tools" else "tests/effects_tools/adjustment_dialog_tests.cpp",
                 "src/ui/AdjustmentAdvancedControls.h", "src/ui/AdjustmentAdvancedControls.cpp"]
        if family == "adjustment_dialog":
            extra += ["src/ui/AdjustmentDialog.h", "src/ui/AdjustmentDialog.cpp", "src/imaging/image_types.h"]
    files = {ROOT / p for p in extra}
    for folder in folders:
        files.update(p for p in (ROOT / folder).rglob("*") if p.suffix in (".h", ".cpp", ".c", ".hlsl"))
    return sorted(files)


def pass_marker(name):
    family, suffix = name.split(".", 1)
    if family == "process_save":
        if not re.fullmatch(r"stage_[0-4]", suffix):
            raise ValueError("Unknown forced-termination stage: " + name)
        return "process-save " + suffix[-1]
    return suffix


def presentation_parameters(name):
    if not name.startswith("native_display_profile."):
        raise ValueError("Not a native presentation contract: " + name)
    suffix = name.split(".", 1)[1]
    case, separator, scale = suffix.rpartition("_dpr_")
    if not separator or case not in PRESENTATION_CASES or scale not in PRESENTATION_SCALES:
        raise ValueError("Unknown native presentation case/DPR: " + name)
    return case, PRESENTATION_SCALES[scale]


def observed_pass(name, output):
    if name.split(".", 1)[0] in release42.FAMILIES:
        return release42.pass_marker(name.split(".", 1)[1], output)
    if name.startswith("subject_input."):
        case = name.split(".", 1)[1]
        return len(re.findall(r"^PASS candidate " + re.escape(case) + r" max=[\d.e+-]+ byte=0\s*$", output, re.MULTILINE)) == 1
    if name.split(".", 1)[0] in PAIRED_SOURCE_FAMILIES:
        family, case = name.split(".", 1)
        _, _, counts = source_contract_spec(family)
        return case in counts and len(re.findall(r"^PASS " + re.escape(case) + r" checks=" + str(counts[case]) + r" failures=0\s*$", output, re.MULTILINE)) == 1
    if name.startswith("retouch_extent."):
        ident = name.split(".", 1)[1]
        return bool(re.search(r"^PASS " + re.escape(ident) + r" outside=\d+ lost=0 live=-?\d+\s*$", output, re.MULTILINE)
                    and re.search(r"^-- PASS exact_rgba " + re.escape(ident) + r" sha256=[a-f0-9]{64}\s*$", output, re.MULTILINE))
    if name.startswith("retouch_growth."):
        return len(re.findall(r"^PASS " + re.escape(name.split(".", 1)[1]) + r"\s*$", output, re.MULTILINE)) == 2
    if name.startswith("project_open_batch."):
        ident = re.escape(name.split(".", 1)[1])
        return bool(re.search(r"^START " + ident + r"\s*$", output, re.MULTILINE)
                    and re.search(r"^PASS " + ident + r"\s*$", output, re.MULTILINE))
    if name.startswith("accessibility."):
        # The actual report identifies each case. A complete suite can exit 1
        # while its other independent checks pass; never synthesize PASS lines.
        return bool(re.search(r"^accessibility witness exit=[01]\s*$", output, re.MULTILINE))
    if name.startswith("physical_viewport."):
        return all(re.search(r"^PASS " + re.escape(ident) + r" max=\d+ pixels=\d+ request=[\d.e+-]+\s*$", output, re.MULTILINE)
                   for scale in (1, 2) for ident in viewport_ids(name, scale))
    if name.startswith("native_pixel_grid."):
        ident = name.split(".", 1)[1]
        return len(re.findall(r"^PASS " + re.escape(ident) + r" \{[^\r\n]+\}\s*$", output, re.MULTILINE)) == 2
    if name.startswith("native_display_profile."):
        case, scale = presentation_parameters(name)
        match = re.search(r"^PASS " + re.escape(case) + r" max=(\d+) dpr=([\d.]+)\s*$", output, re.MULTILINE)
        return bool(match and float(match[2]) == scale and int(match[1]) <= (0 if case == "srgb_identity" else 2))
    return bool(re.search(r"^PASS " + re.escape(pass_marker(name)) + r"\s*$", output, re.MULTILINE))


def viewport_ids(name, scale):
    if name == "physical_viewport.sampling":
        zooms = (.7, 1., 1.999)
    elif name == "physical_viewport.crisp_document_pixels":
        zooms = (2., 4., 10.749, 32.)
    elif name.startswith("native_pixel_grid.") and name.split(".", 1)[1] in GRID_CASES:
        return [name.split(".", 1)[1]]
    else:
        raise ValueError("Unknown viewport contract: " + name)
    return [f"{pattern}-z{zoom:.3f}-v{variant}-pan{pan}-dpr{scale}" for pattern in ("asymmetric", "stripes")
            for zoom in zooms for variant in range(5) for pan in (0, 1)]


def accessibility_contracts():
    data = read(ACCESSIBILITY_MAP)
    rows = data["candidates"]
    ids = data["grouped_report"]["required_ids"]
    if len(rows) != 37 or len(ids) != 37 or len(set(ids)) != 37 or {c["native_witness"]["result_id"] for c in rows} != set(ids):
        raise ValueError("Frozen 37-check accessibility contract membership changed")
    if sha(ROOT / "tests/accessibility/AccessibilityWitness.cpp") != ACCESSIBILITY_TEST_SHA256:
        raise ValueError("Accessibility fixture/assertions changed; re-audit before capture")
    return {c["native_test"]: c["native_witness"]["result_id"] for c in rows}


def validate_accessibility_report(report, code, output):
    ids = set(accessibility_contracts().values())
    checks = report.get("checks", [])
    if report.get("schema_version") != 1 or len(checks) != 37 or {c.get("id") for c in checks} != ids:
        raise ValueError("UIA required checks missing, duplicated or changed")
    if code not in (0, 1) or any(c.get("status") not in ("passed", "failed") for c in checks):
        raise ValueError("UIA execution did not produce a complete valid report")
    failed = any(c["status"] == "failed" for c in checks)
    if code != int(failed) or report.get("status") != ("failed" if failed else "passed"):
        raise ValueError("UIA exit, summary and check statuses disagree")
    if not re.search(r"^accessibility witness exit=" + str(code) + r"\s*$", output, re.MULTILINE):
        raise ValueError("UIA actual completion marker is missing")
    if report.get("qt_version") != "6.8.3" or report.get("transport") != "Windows UIA COM on MTA; Qt-targeted keyboard events":
        raise ValueError("UIA runtime/transport qualification changed")
    if report.get("narrator_acceptance") != "unperformed" or report.get("mac_reference") != "blocked_reference":
        raise ValueError("UIA report incorrectly changes external acceptance scope")
    if not report.get("main_tree") or not report.get("dialog_tree") or not report.get("main_focus") or not report.get("dialog_focus"):
        raise ValueError("UIA actual control/focus observations are missing")
    return {c["id"]: c for c in checks}


def capture_accessibility(build_dir, config, env):
    mapping = accessibility_contracts()
    exe = build_dir / config / "accessibility_witness.exe"
    directory = ROOT / "evidence/accessibility/native-contract" / datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S-%f")
    directory.mkdir(parents=True)
    report_file = directory / "uia.json"
    run_env = dict(env, QT_QPA_PLATFORM="windows", QT_SCALE_FACTOR="1", QT_SCALE_FACTOR_ROUNDING_POLICY="PassThrough")
    run_env.pop("QT_SCREEN_SCALE_FACTORS", None)
    command = [str(exe), str(report_file)]
    started = datetime.now(timezone.utc).isoformat()
    run = subprocess.run(command, cwd=ROOT, env=run_env, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=120)
    output = run.stdout + run.stderr
    (directory / "stdout.log").write_text(run.stdout, encoding="utf-8")
    (directory / "stderr.log").write_text(run.stderr, encoding="utf-8")
    rows = validate_accessibility_report(read(report_file), run.returncode, output)
    records = []
    for name, ident in mapping.items():
        result = rows[ident]["status"]
        records.append({"native_test": name, "result": result, "exit_code": run.returncode,
            "expected_pass_marker_observed": observed_pass(name, output), "output_marker_scope": "Complete suite exit; individual status is in the typed report",
            "started_utc": started, "command": [exe.relative_to(ROOT).as_posix(), str(report_file)], "executable_sha256": sha(exe),
            "dependency_group": "accessibility", "timeout_seconds": 120, "environment": {"QT_QPA_PLATFORM": "windows", "QT_SCALE_FACTOR": "1"},
            "output": output, "output_sha256": hashlib.sha256(output.encode()).hexdigest(),
            "result_file": {"path": report_file.relative_to(ROOT).as_posix(), "sha256": sha(report_file)}, "actual_case": rows[ident], "result_selector": ident})
        print(result.upper() + " " + name + " suite_exit=" + str(run.returncode), flush=True)
    return records


def validate_viewport_report(name, report, scale, output):
    physical = name.startswith("physical_viewport.")
    expected = (viewport_ids("physical_viewport.sampling", scale) + viewport_ids("physical_viewport.crisp_document_pixels", scale)) if physical else list(GRID_CASES)
    rows = report.get("cases", [])
    if report.get("backend") != "D3D11_WARP" or report.get("presentation") != "explicit_missing_profile_sRGB_fallback":
        raise ValueError("Viewport backend/presentation qualification changed")
    if report.get("suite") != ("PHYSICAL_VIEWPORT_V1" if physical else "NATIVE_PIXEL_GRID_V1"):
        raise ValueError("Viewport suite identity changed")
    if len(rows) != len(expected) or {r.get("id") for r in rows} != set(expected):
        raise ValueError("Viewport parameter identities/multiplicity changed")
    if report.get("passed") != sum(r.get("passed") is True for r in rows) or report.get("failed") != sum(r.get("passed") is not True for r in rows):
        raise ValueError("Viewport report counts disagree")
    for row in rows:
        ident = row["id"]
        if row.get("dpr", scale) != scale or type(row.get("passed")) is not bool:
            raise ValueError("Viewport DPR/result type changed")
        if physical:
            zoom = row["zoom"]
            if zoom not in (.7, 1., 1.999, 2., 4., 10.749, 32.) or row["pattern"] not in ("asymmetric", "stripes") or type(row["variant"]) is not int or not 0 <= row["variant"] < 5 or type(row["fractional_pan"]) is not bool:
                raise ValueError("Viewport fixture parameters changed")
            crisp = zoom >= 2
            expected_id = f"{row['pattern']}-z{zoom:.3f}-v{row['variant']}-pan{int(row['fractional_pan'])}-dpr{scale}"
            units = 1 if crisp else 1 / zoom
            if ident != expected_id or row.get("crisp_document_pixels") != crisp or row.get("expected_units") != units:
                raise ValueError("Viewport phase/source parameter mismatch")
            if row.get("tolerance") != 1 or row.get("minimum_compared_pixels") != 16 or row.get("width") != 192 * scale or row.get("height") != 144 * scale:
                raise ValueError("Frozen viewport dimensions/threshold changed")
            valid = (row.get("compared_pixels", 0) >= 16 and 0 <= row.get("maximum_difference", 999) <= 1
                     and row.get("pixels_over_tolerance") == 0 and math.isfinite(row.get("requested_units", math.nan))
                     and abs(row["requested_units"] - units) < 1e-12)
        elif ident == GRID_CASES[0]:
            valid = row.get("compared_pixels") == 256 and row.get("physical_width") == 16 and row.get("tolerance") == 1 and 0 <= row.get("maximum_difference", 999) <= 1
        elif ident == GRID_CASES[1]:
            valid = row.get("zoom") == 7.999 and row.get("changed_pixels") == 0
        else:
            valid = (row.get("zoom") == 8 and row.get("compared_pixels") == 12544 and row.get("expected_full_coverage_gray") == 203
                     and row.get("changed_pixels", 0) > 0 and row.get("tolerance") == 1 and 0 <= row.get("maximum_difference", 999) <= 1)
        if row["passed"] and not valid:
            raise ValueError("Passing viewport parameter violates fixed checks: " + ident)
        marker = "PASS " if row["passed"] else "FAIL "
        found = re.search(r"^" + re.escape(marker + ident) + r" (.+)$", output, re.MULTILINE)
        if not found:
            raise ValueError("Viewport parameter output marker missing: " + ident)
        if physical:
            fields = re.fullmatch(r"max=(\d+) pixels=(\d+) request=[\d.e+-]+\s*", found[1])
            if not fields or int(fields[1]) != row["maximum_difference"] or int(fields[2]) != row["compared_pixels"]:
                raise ValueError("Viewport output measurements disagree: " + ident)
        elif json.loads(found[1]) != row:
            raise ValueError("Pixel-grid output measurements disagree: " + ident)
    selected = {r["id"]: r for r in rows if r["id"] in viewport_ids(name, scale)}
    return len(selected), all(r["passed"] for r in selected.values())


def viewport_artifact_names(family, report):
    if family == "physical_viewport":
        bases = [row["id"] + "-" + suffix for row in report["cases"]
                 if not row["passed"] or (row["zoom"] == 4 and row["variant"] == 0 and row["fractional_pan"])
                 for suffix in ("actual", "expected", "diff")]
    elif family == "native_pixel_grid":
        bases = ["actual-pixels", "grid-at8-off", "grid-at8-actual", "grid-at8-expected", "grid-at8-diff"]
    else:
        raise ValueError("No artifact schema for " + family)
    return {base + extension for base in bases for extension in (".png", ".rgba")}


def capture_viewport(build_dir, config, env, names):
    records = []
    for family in ("viewport_cache", "physical_viewport", "native_pixel_grid"):
        selected = [n for n in names if n.startswith(family + ".")]
        if not selected:
            continue
        exe = build_dir / config / (binding(selected[0])[0] + ".exe")
        observations = []
        for scale in ((1,) if family == "viewport_cache" else (1, 2)):
            directory = ROOT / "evidence/graphics/viewport-ledger" / datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S-%f") / (family + "-dpr" + str(scale))
            directory.mkdir(parents=True)
            run_env = dict(env, QT_QPA_PLATFORM="offscreen" if family == "viewport_cache" else "windows", QT_SCALE_FACTOR=str(scale), QT_SCALE_FACTOR_ROUNDING_POLICY="PassThrough")
            run_env.pop("QT_SCREEN_SCALE_FACTORS", None)
            command = [str(exe)] + ([] if family == "viewport_cache" else [str(directory)])
            started = datetime.now(timezone.utc).isoformat()
            run = subprocess.run(command, cwd=ROOT, env=run_env, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=120)
            output = run.stdout + run.stderr
            report_file = directory / "results.json"
            observation = {"command": [exe.relative_to(ROOT).as_posix(), *command[1:]], "dpr": scale,
                "environment": {key: run_env[key] for key in ("QT_QPA_PLATFORM", "QT_SCALE_FACTOR", "QT_SCALE_FACTOR_ROUNDING_POLICY")},
                "started_utc": started, "exit_code": run.returncode, "output": output, "output_sha256": hashlib.sha256(output.encode()).hexdigest()}
            if family == "viewport_cache":
                actual = set(re.findall(r"^PASS ([^\r\n]+)$", output, re.MULTILINE))
                if run.returncode == 0 and actual != set(VIEWPORT_CACHE_CASES):
                    raise ValueError("Viewport cache suite silently omitted/added a contract")
            else:
                report = read(report_file)
                for name in selected:
                    validate_viewport_report(name, report, scale, output)
                if (run.returncode == 0) != (report["failed"] == 0):
                    raise ValueError("Viewport suite exit disagrees with parameter results")
                observation["result_file"] = {"path": report_file.relative_to(ROOT).as_posix(), "sha256": sha(report_file)}
                observation["artifacts"] = [{"path": p.relative_to(ROOT).as_posix(), "sha256": sha(p)} for p in sorted(directory.glob("*")) if p.suffix in (".png", ".rgba")]
                if {Path(item["path"]).name for item in observation["artifacts"]} != viewport_artifact_names(family, report):
                    raise ValueError("Required viewport image/raw artifacts missing or unexpected")
            observations.append(observation)
        output = "\n".join(o["output"] for o in observations)
        for name in selected:
            marker = observed_pass(name, output)
            okay = marker and all(o["exit_code"] == 0 for o in observations)
            count = 1 if family == "viewport_cache" else sum(len(viewport_ids(name, scale)) for scale in (1, 2))
            records.append({"native_test": name, "result": "passed" if okay else "failed", "exit_code": 0 if okay else 1,
                "expected_pass_marker_observed": marker, "started_utc": observations[0]["started_utc"], "command": observations[0]["command"],
                "executable_sha256": sha(exe), "dependency_group": family, "output": output, "output_sha256": hashlib.sha256(output.encode()).hexdigest(),
                "observations": observations, "parameter_execution_count": count})
            print(records[-1]["result"].upper() + " " + name + " parameters=" + str(count), flush=True)
    return records


def validate_presentation_result(name, report):
    case, scale = presentation_parameters(name)
    if report.get("case") != case or report.get("threshold") != 2:
        raise ValueError("Native presentation case or frozen threshold changed: " + name)
    if report.get("passed") is not True:
        return
    if report.get("dpr") != scale or not 0 <= report.get("maximum_error", 999) <= (0 if case == "srgb_identity" else 2):
        raise ValueError("Native presentation passed with incorrect DPR/error: " + name)
    if report.get("physical_width", 0) <= 0 or report.get("physical_height", 0) <= 0:
        raise ValueError("Native presentation lacks a physical capture: " + name)
    if report.get("viewport_bytes", -1) < 0 or report["viewport_bytes"] > 128 * 1024 * 1024:
        raise ValueError("Native presentation exceeded its frozen viewport bound: " + name)
    if case in ("linear_frame", "recreate_resize", "budget_failure"):
        if report.get("profile_sha256") != sha(ROOT / "tests/display_profile/fixtures/linear-rgb.icc"):
            raise ValueError("Native presentation used a different linear profile: " + name)
        if report["viewport_bytes"] != report["physical_width"] * report["physical_height"] * 4:
            raise ValueError("Native presentation surface size disagrees with capture: " + name)
    if case in ("fallback", "profile_switch") and (report["viewport_bytes"] != 0 or not report.get("diagnostic")):
        raise ValueError("Native presentation fallback lacks its explicit diagnostic: " + name)


def capture_presentation(build_dir, config, env, names):
    expected = {"native_display_profile." + case + "_dpr_" + scale for case in PRESENTATION_CASES for scale in PRESENTATION_SCALES}
    if set(names) != expected:
        raise ValueError("Native presentation must retain all seven cases at four fixed DPR values")
    exe = build_dir / config / "native_display_profile_tests.exe"
    root = build_dir / "native-contract-presentation" / datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
    root.mkdir(parents=True, exist_ok=False)
    records = []
    for name in sorted(expected):
        case, scale = presentation_parameters(name)
        directory = root / name.split(".", 1)[1]
        directory.mkdir()
        command = [str(exe), case, str(ROOT / "tests/display_profile/fixtures/linear-rgb.icc"), str(directory)]
        runtime = dict(env, QT_QPA_PLATFORM="windows", QT_SCALE_FACTOR=str(scale), QT_SCALE_FACTOR_ROUNDING_POLICY="PassThrough")
        runtime.pop("QT_SCREEN_SCALE_FACTORS", None)
        started = datetime.now(timezone.utc).isoformat()
        run = subprocess.run(command, cwd=ROOT, env=runtime, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=60)
        output = run.stdout + run.stderr
        result_file = directory / (case + ".json")
        if run.returncode not in (0, 1) or not result_file.is_file():
            raise RuntimeError("Native presentation did not preserve a result: " + name + "\n" + output)
        report = read(result_file)
        validate_presentation_result(name, report)
        passed = report.get("passed") is True
        marker = observed_pass(name, output)
        if passed != marker or (run.returncode == 0) != passed:
            raise ValueError("Native presentation result, exit and output disagree: " + name)
        artifacts = []
        if passed:
            for suffix in ("-srgb.png", "-display.png"):
                path = directory / (case + suffix)
                data = path.read_bytes()
                if data[:8] != b"\x89PNG\r\n\x1a\n" or len(data) < 24:
                    raise ValueError("Native presentation did not preserve a valid PNG: " + name)
                dimensions = (int.from_bytes(data[16:20], "big"), int.from_bytes(data[20:24], "big"))
                if dimensions != (report["physical_width"], report["physical_height"]):
                    raise ValueError("Native presentation PNG dimensions disagree with report: " + name)
                artifacts.append({"path": path.relative_to(ROOT).as_posix(), "sha256": sha(path)})
        records.append({"native_test": name, "result": "passed" if passed else "failed", "exit_code": run.returncode,
            "expected_pass_marker_observed": marker, "started_utc": started, "command": [exe.relative_to(ROOT).as_posix(), *command[1:]],
            "executable_sha256": sha(exe), "dependency_group": "native_display_profile", "output": output,
            "output_sha256": hashlib.sha256(output.encode()).hexdigest(), "actual_case": report,
            "environment": {"QT_QPA_PLATFORM": "windows", "QT_SCALE_FACTOR": str(scale), "QT_SCALE_FACTOR_ROUNDING_POLICY": "PassThrough"},
            "result_file": {"path": result_file.relative_to(ROOT).as_posix(), "sha256": sha(result_file)}, "artifacts": artifacts})
        print(records[-1]["result"].upper() + " " + name, flush=True)
    return records


def capture_io_suite(build_dir, config, env, family, expected):
    decoder = family == "decoder_adversarial"
    exe = build_dir / config / (("decoder_adversarial_tests" if decoder else "process_save_tests") + ".exe")
    directory = build_dir / ("native-contract-" + family) / datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
    directory.mkdir(parents=True, exist_ok=False)
    command = [str(exe), *([str(ROOT)] if decoder else []), str(directory)]
    started = datetime.now(timezone.utc).isoformat()
    print(f"Running {len(expected)} {family} contracts in real child processes", flush=True)
    run = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=180)
    output = run.stdout + run.stderr
    reports = list(directory.glob("*/report.json"))
    if run.returncode not in (0, 1) or len(reports) != 1:
        raise RuntimeError("Required IO suite did not write its complete report: " + output)
    # Qt's private temporary directories have restricted ACLs. Preserve a byte
    # copy in the ordinary evidence directory so read-only verification can hash
    # the reports and inputs without changing any ACL or original evidence.
    result_path = directory / "report.json"
    shutil.copyfile(reports[0], result_path)
    result = read(result_path)
    rows = result.get("cases", [])
    if decoder:
        if result.get("corpus") != "DECODER_ADVERSARIAL_V1" or result.get("seed") != "0xc011ab1e" or result.get("timeout_per_case_ms") != 5000 or result.get("process_limit_bytes") != 512 * 1024 * 1024 or result.get("import_working_limit_bytes") != 64 * 1024 * 1024:
            raise ValueError("Decoder corpus identity or frozen limits changed")
        if result.get("executable_sha256") != sha(exe):
            raise ValueError("Decoder report was produced by a different executable")
    elif result.get("actual_process_termination") is not True or result.get("power_loss_tested") is not False:
        raise ValueError("Process-save report does not describe actual forced termination")
    actual_names = [family + "." + (r["id"] if decoder else "stage_" + str(r["fault_point"])) for r in rows]
    if len(rows) != len(expected) or len(set(actual_names)) != len(rows) or set(actual_names) != set(expected):
        raise ValueError("IO run omitted, duplicated or added required cases")
    records = []
    for name, row in zip(actual_names, rows):
        passed = row.get("passed") is True
        marker = bool(re.search(r"^PASS " + re.escape(pass_marker(name)) + r"\s*$", output, re.MULTILINE))
        if passed != marker:
            raise ValueError("IO result/output disagreement: " + name)
        input_file = None
        if decoder:
            if row["expectation"] != expected[name]["acceptance_expectation"]:
                raise ValueError("Decoder acceptance rule changed: " + name)
            source_path = Path(row["path"]).resolve()
            if not source_path.is_relative_to(directory) or sha(source_path) != row["input_sha256"]:
                raise ValueError("Decoder input path/hash mismatch: " + name)
            copy_path = directory / (row["id"] + ".payload")
            shutil.copyfile(source_path, copy_path)
            input_file = {"path": copy_path.relative_to(ROOT).as_posix(), "sha256": row["input_sha256"]}
            if passed:
                decoded = row["decoder"]
                if decoded["outcome"] not in ("decoded", "rejected") or (row["expectation"] != "bounded" and decoded["outcome"] != row["expectation"]):
                    raise ValueError("Decoder passed despite an invalid outcome: " + name)
                if row["elapsed_ms"] > 5000 or row["peak_working_set"] > 512 * 1024 * 1024 or row["peak_commit"] > 512 * 1024 * 1024:
                    raise ValueError("Decoder passed despite exceeding a frozen limit: " + name)
        elif passed:
            if row["recovered"] != expected[name]["acceptance_expectation"] or row["writer_pid"] <= 0 or "READY " + name[-1] not in row["writer_stdout"]:
                raise ValueError("Forced-termination recovery disagrees with its stage: " + name)
        record = {"native_test": name, "result": "passed" if passed else "failed", "exit_code": 0 if passed else 1,
            "suite_exit_code": run.returncode, "expected_pass_marker_observed": marker, "started_utc": started,
            "command": [exe.relative_to(ROOT).as_posix(), *command[1:]], "executable_sha256": sha(exe),
            "dependency_group": family, "output": output, "output_sha256": hashlib.sha256(output.encode()).hexdigest(),
            "actual_case": row, "result_file": {"path": result_path.relative_to(ROOT).as_posix(), "sha256": sha(result_path)}}
        if input_file:
            record["input_file"] = input_file
        records.append(record)
    if (run.returncode == 0) != all(r["result"] == "passed" for r in records):
        raise ValueError("IO suite exit disagrees with its individual cases")
    print(f"{sum(r['result'] == 'passed' for r in records)}/{len(records)} {family} passed", flush=True)
    return records


def panel_contracts():
    if not PANEL_MAP.exists():
        return []
    data = read(PANEL_MAP)
    source = ROOT.parent / "upstream/CompositorTests/CanvasThumbnailTests.swift"
    test = ROOT / "tests/layer_panel/LayerPanelSourceTests.cpp"
    expected = data.get("native_test_sha256", data.get("native_test_sha256_pending_run"))
    if data.get("baseline_sha") != BASELINE or sha(source) != data["source_sha256"].lower() or not expected or sha(test) != expected.lower():
        raise ValueError("Stale or unreviewed layer thumbnail assertion map")
    helper_positions = {r["source_line"] for r in data.get("helper_assertion_correspondences", [])}
    if helper_positions != {11, 15, 18, 49, 54, 60}:
        raise ValueError("Layer thumbnail fixture/reader preconditions are not completely mapped")
    records = []
    for method in data["methods"]:
        family = "UT-CanvasThumbnailTests-" + method["upstream_method"]
        records.append({"id": "ACC-" + family, "upstream_test_id": family,
            "source": {"path": method["source_path"], "line": method["source_line"]}, "native_tests": ["layer_panel." + method["native_case"]],
            "assertions": method["direct_assertions"], "helper_assertions": [] if method["native_case"] == "thumbnail_shapes" else data["helper_assertion_correspondences"],
            "reference_kind": "pure_contract" if method["native_case"] == "thumbnail_shapes" else "native_render_invariants",
            "review_path": PANEL_MAP.relative_to(ROOT).as_posix(), "review_sha256": sha(PANEL_MAP),
            "claim": "Every direct assertion and required small-image fixture/reader precondition reconciled; no Mac thumbnail differential"})
    return records


def selection_contracts():
    if not SELECTION_MAP.exists():
        return []
    data = read(SELECTION_MAP)
    source = ROOT.parent / "upstream/CompositorTests/SelectionTests.swift"
    test = ROOT / "tests/selection_gesture/SelectionGestureTests.cpp"
    if data.get("baseline_sha") != BASELINE or sha(source) != data["source_sha256"].lower() or sha(test) != data["native_test_sha256"].lower():
        raise ValueError("Stale selection gesture assertion map")
    helpers = data.get("helper_correspondences", [])
    counts = data.get("complete_candidate_coverage_helper_calls", {})
    expected = {"polygonalCornersCanBeRemovedAndClosed": (4, 2), "clickDeselectsAndSelectionStepsUndo": (5, 1), "marqueeDrawsWholePixelRectanglesInAnyDirection": (6, 6)}
    if {r["source_line"] for r in helpers} != {23, 24, 25, 28} or counts != {k: v[1] for k, v in expected.items()} or data.get("complete_candidate_helper_require_invocations") != 36:
        raise ValueError("Selection fixture/reader preconditions are not fully mapped")
    records = []
    for method in data["methods"]:
        if not method.get("full_logical_contract_candidate"):
            continue
        name = method["upstream_method"]
        if name not in expected or len(method["assertions"]) != expected[name][0]:
            raise ValueError("Unreviewed complete selection gesture contract: " + name)
        upstream = "UT-SelectionTests-" + name
        records.append({"id": "ACC-" + upstream, "upstream_test_id": upstream,
            "source": {"path": "CompositorTests/SelectionTests.swift", "line": method["line"]}, "native_tests": [method["native_case"]],
            "assertions": method["assertions"], "helper_assertions": helpers, "helper_call_count": counts[name],
            "reference_kind": "native_render_invariants", "review_path": SELECTION_MAP.relative_to(ROOT).as_posix(), "review_sha256": sha(SELECTION_MAP),
            "claim": "Every direct assertion and required 100x100 blank-layer selection/history fixture precondition reconciled; UI event integration remains a separate contract"})
    if len(records) != 3:
        raise ValueError("Selection map omitted a reviewed complete candidate")
    return records


def tiled_review():
    if not TILED.exists():
        return None
    review = read(TILED)
    if review.get("baseline_sha") != BASELINE or len(review.get("invocations", [])) != 12:
        raise ValueError("Tiled review must map all 12 pinned invocation IDs")
    for path, expected in ((ROOT.parent / "upstream/CompositorTests/TiledLayerTests.swift", review["source_sha256"]),
                           (local(review["native_source_file"]), review["native_test_sha256"])):
        if sha(path) != expected.lower():
            raise ValueError(f"Stale tiled assertion mapping: {path}")
    for row in review["invocations"]:
        if not row.get("native_method_port_complete") or not row.get("source_assertions_and_helpers_all_mapped") or not row.get("assertions"):
            raise ValueError(f"Incomplete tiled assertion mapping: {row['id']}")
    return review


def downsample_review():
    if not DOWNSAMPLE.exists():
        return None
    review = read(DOWNSAMPLE)
    rows = review.get("invocations", [])
    if review.get("baseline_sha") != BASELINE or len(rows) != 4:
        raise ValueError("Downsample review must map all four pinned invocation IDs")
    if not all(r.get("global_renderer_integrated", review.get("global_renderer_integrated", False)) for r in rows):
        return None  # Historical isolated-adapter results cannot promote production.
    for path, expected in ((ROOT.parent / "upstream/CompositorTests/DownsampleTests.swift", review["source_sha256"]),
                           (ROOT / "tests/downsample/DownsampleTests.cpp", review["native_test_sha256"])):
        if sha(path) != expected.lower():
            raise ValueError(f"Stale Downsample assertion mapping: {path}")
    sites = set()
    for row in rows:
        if not row.get("native_method_port_complete") or not row.get("source_assertions_and_helpers_all_mapped") or not row.get("assertions"):
            raise ValueError(f"Incomplete Downsample assertion mapping: {row['id']}")
        for assertion in row["assertions"]:
            if assertion.get("source_execution_count", 0) < 1 or assertion.get("native_execution_count", 0) < assertion["source_execution_count"]:
                raise ValueError("Incomplete Downsample assertion multiplicity: " + row["id"])
            sites.add(assertion["source_line"])
    if sites != {11, 18, 23, 34, 39, 41, 42, 43, 51, 52, 62, 63, 70, 72, 77, 79}:
        raise ValueError("Downsample review omitted a required source assertion/helper site")
    return review


def capture_downsample(build_dir, config, env, review):
    exe = build_dir / config / "downsample_tests.exe"
    directory = build_dir / "native-contract-downsample" / datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
    directory.mkdir(parents=True, exist_ok=False)
    command = [str(exe), "production", str(directory)]
    started = datetime.now(timezone.utc).isoformat()
    print("Running four Downsample source contracts through production rendering", flush=True)
    run = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=180)
    output = run.stdout + run.stderr
    result_path = directory / "results.json"
    if run.returncode not in (0, 1) or not result_path.is_file():
        raise RuntimeError("Downsample suite failed to produce invocation results: " + output)
    result = read(result_path)
    rows = result.get("invocations", [])
    if result.get("baseline_sha") != BASELINE or result.get("global_renderer_integrated") is not True or len(rows) != 4 or len({r["id"] for r in rows}) != 4:
        raise ValueError("Downsample run lacks four integrated invocation results")
    records = []
    for reviewed in review["invocations"]:
        actual = next(r for r in rows if r["id"] == reviewed["id"])
        if actual["argument"] != reviewed["argument"]:
            raise ValueError("Downsample source parameters changed: " + reviewed["id"])
        for assertion in reviewed["assertions"]:
            checks = [c for c in actual["checks"] if c.get("source_line") == assertion["source_line"]]
            if len(checks) != assertion["native_execution_count"]:
                raise ValueError("Downsample assertion multiplicity changed: " + reviewed["id"])
        passed = actual["passed"] and all(c.get("passed", False) for c in actual["checks"])
        marker = bool(re.search(r"^PASS " + re.escape(reviewed["id"]) + r"\s*$", output, re.MULTILINE))
        if passed != marker:
            raise ValueError("Downsample result/output disagreement: " + reviewed["id"])
        records.append({"native_test": "downsample." + reviewed["id"], "result": "passed" if passed else "failed", "exit_code": 0 if passed else 1,
            "suite_exit_code": run.returncode, "expected_pass_marker_observed": marker, "started_utc": started,
            "command": [exe.relative_to(ROOT).as_posix(), "production", directory.relative_to(ROOT).as_posix()], "executable_sha256": sha(exe),
            "dependency_group": "downsample", "output": output, "output_sha256": hashlib.sha256(output.encode()).hexdigest(),
            "actual_assertion_checks": actual["checks"], "argument": actual["argument"], "global_renderer_integrated": True,
            "result_file": {"path": result_path.relative_to(ROOT).as_posix(), "sha256": sha(result_path)}})
        print(records[-1]["result"].upper() + " downsample." + reviewed["id"], flush=True)
    if (run.returncode == 0) != all(r["result"] == "passed" for r in records):
        raise ValueError("Downsample suite exit disagrees with invocation results")
    return records


def capture_tiled(build_dir, config, env, review):
    exe = build_dir / config / "tiled_parity_tests.exe"
    directory = build_dir / "native-contract-tiled" / datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
    directory.mkdir(parents=True, exist_ok=False)
    command = [str(exe), str(directory)]
    tiled_env = dict(env, QT_QPA_PLATFORM="windows", QT_SCALE_FACTOR="1")
    print("Running 12 tiled source contracts with native Windows rendering", flush=True)
    started = datetime.now(timezone.utc).isoformat()
    run = subprocess.run(command, cwd=ROOT, env=tiled_env, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=180)
    output = run.stdout + run.stderr
    if run.returncode not in (0, 1) or not (directory / "results.json").is_file():
        raise RuntimeError("Tiled suite failed to produce invocation results: " + output)
    result = read(directory / "results.json")
    rows = result.get("invocations", [])
    if result.get("baseline_sha") != BASELINE or len(rows) != 12 or len({r["id"] for r in rows}) != 12:
        raise ValueError("Tiled run omitted or duplicated invocation results")
    records = []
    for reviewed in review["invocations"]:
        actual = next(r for r in rows if r["id"] == reviewed["id"])
        if actual["argument"] != reviewed["argument"]:
            raise ValueError("Tiled source parameters changed: " + reviewed["id"])
        for assertion in reviewed["assertions"]:
            checks = [c for c in actual["checks"] if c.get("source_line") == assertion["source_line"]]
            expected = assertion["expected_source_execution_count"]
            if len(checks) < expected or (assertion["source_kind"] == "#expect" and len(checks) != expected):
                raise ValueError("Tiled assertion multiplicity changed: " + reviewed["id"])
        passed = actual["passed"] and all(c.get("passed", False) for c in actual["checks"])
        marker = bool(re.search(r"^PASS " + re.escape(reviewed["id"]) + r"\s*$", output, re.MULTILINE))
        if passed != marker:
            raise ValueError("Tiled result/output disagreement: " + reviewed["id"])
        records.append({"native_test": "tiled." + reviewed["id"], "result": "passed" if passed else "failed", "exit_code": 0 if passed else 1,
            "suite_exit_code": run.returncode, "expected_pass_marker_observed": marker, "started_utc": started,
            "command": [exe.relative_to(ROOT).as_posix(), directory.relative_to(ROOT).as_posix()], "executable_sha256": sha(exe),
            "dependency_group": "tiled", "output": output, "output_sha256": hashlib.sha256(output.encode()).hexdigest(),
            "actual_assertion_checks": actual["checks"], "argument": actual["argument"],
            "result_file": {"path": (directory / "results.json").relative_to(ROOT).as_posix(), "sha256": sha(directory / "results.json")}})
        print(records[-1]["result"].upper() + " tiled." + reviewed["id"], flush=True)
    if (run.returncode == 0) != all(r["result"] == "passed" for r in records):
        raise ValueError("Tiled suite exit disagrees with invocation results")
    return records


def prepared_ctest_names(family):
    return {"retouch_extent": ["retouch.extent"],
            "retouch_growth": ["retouch.growth_cpu", "retouch.growth_warp"],
            "project_open_batch": ["project_open_batch." + case for case in PROJECT_OPEN_CASES]}[family]


def prepared_ctest_pattern(family):
    # CTest uses POSIX-style groups; Python's (?:...) is rejected even when
    # --show-only=json-v1 misleadingly exits zero and emits an empty catalog.
    return "^(" + "|".join(re.escape(name) for name in prepared_ctest_names(family)) + ")$"


def prepared_contracts():
    for path, required in ((RETOUCH_MAP, {"retouch_extent." + case for case in RETOUCH_EXTENT_CASES}
                           | {"retouch_growth." + case for case in RETOUCH_GROWTH_CASES}),
                          (PROJECT_OPEN_MAP, {"project_open_batch." + case for case in PROJECT_OPEN_CASES})):
        data = read(path)
        rows = data["cases"]
        if data.get("baseline_sha") != BASELINE or len(rows) != len(required) or {r["native_test"] for r in rows} != required:
            raise ValueError("Prepared acceptance membership changed: " + str(path))
        for row in rows:
            if row.get("full_upstream_method_port") is not False or row.get("complete_upstream_invocation") is not False:
                raise ValueError("Bounded prepared contract claimed complete source coverage")
            witness = row["native_witness"]
            for item in seq(witness.get("native_test_source")) + witness.get("native_test_sources", []):
                if sha(local(item["path"])) != item["sha256"]:
                    raise ValueError("Prepared native test changed; re-audit assertions before capture: " + item["path"])


def validate_prepared_commands(catalog, family, exe):
    expected = prepared_ctest_names(family)
    rows = catalog.get("tests", [])
    if len(rows) != len(expected) or {r.get("name") for r in rows} != set(expected):
        raise ValueError("Configured CTest commands differ from required family")
    for row in rows:
        command = row.get("command", [])
        properties = {item["name"]: item["value"] for item in row.get("properties", [])}
        expected_timeout = {"retouch_extent": 60, "retouch_growth": 120, "project_open_batch": 30}[family]
        if properties.get("TIMEOUT") != expected_timeout:
            raise ValueError("Configured prepared timeout differs from reviewed contract")
        if family == "project_open_batch" and "QT_QPA_PLATFORM=offscreen" not in properties.get("ENVIRONMENT", []):
            raise ValueError("Batch Open CTest must use the reviewed offscreen platform")
        if family == "retouch_extent":
            if (len(command) != 5 or Path(command[0]).name.lower() not in ("cmake", "cmake.exe")
                    or not command[1].startswith("-DWITNESS=") or local(command[1][10:]) != exe
                    or not command[2].startswith("-DEVIDENCE_ROOT=")
                    or local(command[2][16:]) != ROOT / "evidence/retouch-extent-integrated"
                    or command[3] != "-P" or local(command[4]) != ROOT / "tests/retouch_extent/run_witness.cmake"):
                raise ValueError("Configured extent wrapper does not run the locked witness")
        else:
            suffix = row["name"].split(".", 1)[1]
            if family == "retouch_growth":
                valid = len(command) == (2 if suffix == "growth_warp" else 1)
                if valid and suffix == "growth_warp":
                    valid = local(command[1]) == ROOT / "shaders/BrushCoverage.hlsl"
            else:
                valid = len(command) == 2 and command[1] == suffix
            if not command or local(command[0]) != exe or not valid:
                raise ValueError("Configured prepared executable or arguments changed: " + row["name"])
    return {row["name"]: row["command"] for row in rows}


def validate_prepared_junit(path, family, code):
    expected = prepared_ctest_names(family)
    rows = list(ET.parse(path).getroot().iter("testcase"))
    if len(rows) != len(expected) or {r.get("name") for r in rows} != set(expected):
        raise ValueError("Prepared CTest membership changed: " + family)
    result = {}
    for row in rows:
        if row.find("skipped") is not None or row.get("status") != "run":
            raise ValueError("Required CTest did not execute: " + row.get("name", ""))
        if "[This part of the test output was removed since it exceeds the threshold" in (row.findtext("system-out") or ""):
            raise ValueError("Required CTest output was truncated: " + row.get("name", ""))
        result[row.get("name")] = {"passed": row.find("failure") is None and row.find("error") is None,
                                  "output": row.findtext("system-out") or ""}
    if code != (0 if all(r["passed"] for r in result.values()) else 8):
        raise ValueError("Actual CTest exit disagrees with JUnit")
    return result


def validate_growth_output(output, warp):
    markers = re.findall(r"^(PASS|FAIL) ([a-z_]+)(?::[^\r\n]*)?\s*$", output, re.MULTILINE)
    if len(markers) != len(RETOUCH_GROWTH_CASES) or {name for _, name in markers} != set(RETOUCH_GROWTH_CASES):
        raise ValueError("Retouch growth group membership changed")
    summaries = [json.loads(line) for line in output.splitlines() if line.startswith('{"schema":')]
    if len(summaries) != 1:
        raise ValueError("Missing or duplicate retouch growth summary")
    summary = summaries[0]
    passed = sum(status == "PASS" for status, _ in markers)
    if (summary.get("schema") != "RETOUCH_GROWTH_CONTRACT_RESULTS_V1" or summary.get("passed") != passed
            or summary.get("failed") != len(RETOUCH_GROWTH_CASES) - passed or summary.get("warp") is not warp or summary.get("mac_differential") is not False):
        raise ValueError("Retouch growth summary, backend or scope disagrees")
    return {name: status == "PASS" for status, name in markers}


def extent_directory(output):
    paths = re.findall(r"^-- Retouch extent evidence: ([^\r\n]+)$", output, re.MULTILINE)
    if len(paths) != 1:
        raise ValueError("Missing/duplicate retouch extent artifact directory")
    directory = local(paths[0])
    if not directory.is_relative_to(ROOT / "evidence/retouch-extent-integrated"):
        raise ValueError("Retouch extent output leaves its evidence directory")
    return directory


def validate_extent_output(directory, output, *, report_override=None, buffer_overrides=None):
    # Overrides are used only by verifier unit checks; capture always reads files.
    report = read(directory / "results.json") if report_override is None else report_override
    rows = report.get("cases", [])
    expected = {case + "-" + suffix + ".rgba" for case in RETOUCH_EXTENT_CASES for suffix in ("actual", "padded-control", "alpha-diff")}
    if (report.get("schema") != "RETOUCH_EXTENT_AFTER_V1" or report.get("mac_differential") is not False
            or len(rows) != 7 or {r.get("id") for r in rows} != set(RETOUCH_EXTENT_CASES)
            or {p.name for p in directory.glob("*.rgba")} != expected):
        raise ValueError("Retouch extent report/raw membership changed")
    result, failed, missing = {}, 0, 0
    for row in rows:
        case = row["id"]
        buffers = [(directory / f"{case}-{suffix}.rgba").read_bytes() for suffix in ("actual", "padded-control", "alpha-diff")]
        if buffer_overrides and case in buffer_overrides:
            buffers = buffer_overrides[case]
        if any(len(b) != 8192 for b in buffers):
            raise ValueError("Retouch raw dimensions changed: " + case)
        actual, control, difference = buffers
        outside, lost, max_alpha = 0, 0, 0
        for index in range(2048):
            offset = index * 4
            x, y = index % 64, index // 64
            a, b = actual[offset + 3], control[offset + 3]
            error = abs(a - b)
            max_alpha = max(max_alpha, error)
            if difference[offset:offset + 4] != bytes([error, error, error, 255]):
                raise ValueError("Stored retouch alpha difference is incorrect: " + case)
            if any(pixel[offset + channel] > pixel[offset + 3] for pixel in (actual, control) for channel in range(3)):
                raise ValueError("Retouch raw pixels are not premultiplied: " + case)
            if x < 8 or y < 8 or x >= 24 or y >= 24:
                outside += int(b > 0)
                lost += int(b > 0 and a < b)
        precondition = row.get("control_began") is True and outside > 0
        if case == "clone":
            precondition = precondition and control[(16 * 64 + 40) * 4:(16 * 64 + 40) * 4 + 4] == bytes([200, 40, 20, 255])
        expected_status = "PRECONDITION_MISSING" if not precondition else "FAIL_LOST_OUTSIDE_PIXELS" if lost else "PASS"
        if (row.get("status") != expected_status or row.get("expected_outside_nonzero_pixels") != outside
                or row.get("lost_outside_pixels") != lost or row.get("max_alpha_error") != max_alpha):
            raise ValueError("Retouch reported predicates disagree with raw bytes: " + case)
        missing += int(not precondition)
        failed += int(precondition and lost > 0)
        probe_x = 40 if case == "clone" else 26 if case in ("smudge", "liquify") else 24
        if row.get("commit_probe_alpha") != actual[(16 * 64 + probe_x) * 4 + 3]:
            raise ValueError("Retouch committed probe differs from raw bytes: " + case)
        live_valid = row.get("warp_live_probe_alpha") == (255 if case in ("smudge", "liquify") else -1)
        exact_markers = re.findall(r"^-- PASS exact_rgba " + case + r" sha256=([a-f0-9]{64})\s*$", output, re.MULTILINE)
        exact = actual == control
        if exact_markers and (len(exact_markers) != 1 or not exact or exact_markers[0] != hashlib.sha256(actual).hexdigest()):
            raise ValueError("Retouch exact-RGBA CTest marker disagrees with raw bytes: " + case)
        result[case] = bool(precondition and row.get("actual_began") is True and lost == 0 and exact and live_valid
                            and observed_pass("retouch_extent." + case, output))
    if report.get("failed") != failed or report.get("missing_preconditions") != missing:
        raise ValueError("Retouch extent summary disagrees with raw predicates")
    return result, sorted(expected)


def prepared_outcomes(family, junit):
    if family == "retouch_extent":
        output = junit["retouch.extent"]["output"]
        values, artifacts = validate_extent_output(extent_directory(output), output)
        if junit["retouch.extent"]["passed"] != all(values.values()):
            raise ValueError("Extent raw/marker outcomes disagree with JUnit")
        return values, {case: output for case in RETOUCH_EXTENT_CASES}, artifacts
    if family == "retouch_growth":
        cpu = validate_growth_output(junit["retouch.growth_cpu"]["output"], False)
        warp = validate_growth_output(junit["retouch.growth_warp"]["output"], True)
        for test, values in (("retouch.growth_cpu", cpu), ("retouch.growth_warp", warp)):
            if junit[test]["passed"] != all(values.values()):
                raise ValueError("Growth assertions disagree with child JUnit status")
        combined = "\n".join(junit[test]["output"] for test in prepared_ctest_names(family))
        return {case: cpu[case] and warp[case] for case in RETOUCH_GROWTH_CASES}, {case: combined for case in RETOUCH_GROWTH_CASES}, []
    values, outputs = {}, {}
    for case in PROJECT_OPEN_CASES:
        name = "project_open_batch." + case
        row = junit[name]
        marker = observed_pass(name, row["output"])
        if row["passed"] != marker:
            raise ValueError("Batch Open named assertion/exit disagreement: " + name)
        values[case], outputs[case] = marker, row["output"]
    return values, outputs, []


def capture_prepared_ctest(build_dir, config, env, family):
    ctest = shutil.which("ctest")
    if not ctest:
        raise RuntimeError("CTest is required for the prepared contract adapter")
    directory = ROOT / "evidence/integration/native-contract-prepared" / (datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S-%f") + "-" + family)
    directory.mkdir(parents=True, exist_ok=False)
    result_file = directory / "results.xml"
    expected = prepared_ctest_names(family)
    pattern = prepared_ctest_pattern(family)
    command = [ctest, "--test-dir", str(build_dir), "-C", config, "--output-on-failure", "--output-junit", str(result_file),
               "--test-output-size-passed", "1048576", "--test-output-size-failed", "1048576", "--no-tests=error", "-R", pattern]
    target, _ = binding(family + "." + ({"retouch_extent": RETOUCH_EXTENT_CASES, "retouch_growth": RETOUCH_GROWTH_CASES, "project_open_batch": PROJECT_OPEN_CASES}[family][0]))
    exe = build_dir / config / (target + ".exe")
    catalog_run = subprocess.run([ctest, "--test-dir", str(build_dir), "-C", config, "--show-only=json-v1", "-R", pattern],
                                 cwd=ROOT, env=env, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=30)
    (directory / "configured-tests.stdout").write_text(catalog_run.stdout, encoding="utf-8")
    (directory / "configured-tests.stderr").write_text(catalog_run.stderr, encoding="utf-8")
    if catalog_run.returncode:
        raise RuntimeError("Could not inspect configured CTest commands: " + catalog_run.stderr)
    catalog = json.loads(catalog_run.stdout)
    resolved_commands = validate_prepared_commands(catalog, family, exe)
    catalog_file = directory / "configured-tests.json"
    write(catalog_file, catalog)
    started = datetime.now(timezone.utc).isoformat()
    run = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=180)
    driver_output = run.stdout + run.stderr
    (directory / "driver.log").write_text(driver_output, encoding="utf-8")
    junit = validate_prepared_junit(result_file, family, run.returncode)
    outcomes, outputs, artifact_names = prepared_outcomes(family, junit)
    records = []
    for case, passed in outcomes.items():
        name = family + "." + case
        target, _ = binding(name)
        exe = build_dir / config / (target + ".exe")
        output = outputs[case]
        arguments = [case] if family == "project_open_batch" else []
        record = {"native_test": name, "result": "passed" if passed else "failed", "exit_code": run.returncode,
            "exit_code_scope": "Actual CTest family driver; individual outcomes validated against JUnit and named assertions",
            "expected_pass_marker_observed": observed_pass(name, output), "started_utc": started,
            "command": [exe.relative_to(ROOT).as_posix(), *arguments], "executable_sha256": sha(exe),
            "execution_driver": command, "driver_output": driver_output, "driver_output_sha256": hashlib.sha256(driver_output.encode()).hexdigest(),
            "configured_tests": {"path": catalog_file.relative_to(ROOT).as_posix(), "sha256": sha(catalog_file)},
            "resolved_test_commands": resolved_commands,
            "dependency_group": family, "timeout_seconds": 180, "output": output, "output_sha256": hashlib.sha256(output.encode()).hexdigest(),
            "junit": {"path": result_file.relative_to(ROOT).as_posix(), "sha256": sha(result_file)},
            "junit_test_names": expected, "result_selector": case,
            "parameter_execution_count": 2 if family == "retouch_growth" else 1}
        if family == "retouch_extent":
            raw_directory = extent_directory(output)
            record["command"].append(raw_directory.relative_to(ROOT).as_posix())
            record["result_file"] = {"path": (raw_directory / "results.json").relative_to(ROOT).as_posix(), "sha256": sha(raw_directory / "results.json")}
            record["artifacts"] = [{"path": (raw_directory / path).relative_to(ROOT).as_posix(), "sha256": sha(raw_directory / path)} for path in artifact_names]
        if family == "retouch_growth":
            record["commands"] = [[exe.relative_to(ROOT).as_posix()], [exe.relative_to(ROOT).as_posix(), str(ROOT / "shaders/BrushCoverage.hlsl")]]
        records.append(record)
        print(record["result"].upper() + " " + name, flush=True)
    return records


def capture_native(build_dir, config, refresh_stale=False):
    build_dir = local(build_dir)
    if not build_dir.is_relative_to(ROOT):
        raise ValueError("Build directory must be inside the Windows workspace")
    names = sorted({c["native_test"] for _, c in additions() if "native_test" in c})
    bindings = {name: binding(name) for name in names}
    if any(n.startswith(("retouch_extent.", "retouch_growth.", "project_open_batch.")) for n in names):
        prepared_contracts()
    if any(n.startswith(("display_profile.", "native_display_profile.")) for n in names):
        fixture = read(ROOT / "tests/display_profile/fixtures/manifest.json")
        if fixture.get("sha256") != LINEAR_PROFILE_SHA256 or fixture.get("fixed_tolerance_bytes") != 2 or sha(ROOT / "tests/display_profile/fixtures/linear-rgb.icc") != LINEAR_PROFILE_SHA256:
            raise ValueError("Required display profile fixture identity or frozen tolerance changed")
    tiled = tiled_review()
    downsample = downsample_review()
    previous_evidence = Evidence()
    families = {n.split(".")[0] for n in names} | ({"tiled"} if tiled else set()) | ({"downsample"} if downsample else set())
    families.update(r["dependency_group"] for r in previous_evidence.runs.values() if r.get("dependency_group") in COHERENT_INGESTION_FAMILIES)
    capture_families = set(families)
    if refresh_stale:
        all_names = names + [r["native_test"] for r in previous_evidence.data.get("runs", []) if r.get("dependency_group") in ("tiled", "downsample")]
        capture_families = {name.split(".")[0] for name in all_names if previous_evidence.result(name)[0] in ("stale_evidence", "not_run")}
        if tiled and not any(n.startswith("tiled.") for n in all_names): capture_families.add("tiled")
        if downsample and not any(n.startswith("downsample.") for n in all_names): capture_families.add("downsample")
        print("Refreshing stale/new families: " + ", ".join(sorted(capture_families)), flush=True)
    # These families require their strict coherent report ingestion.
    # Preserve their dated runs when another family is captured; do not silently
    # replace both-configuration evidence with a normal-only rerun.
    capture_families.difference_update(COHERENT_INGESTION_FAMILIES)
    selected_names = [n for n in names if n.split(".")[0] in capture_families]
    bindings = {name: bindings[name] for name in selected_names}
    if not capture_families:
        print("No ordinary capture family needs a rerun; coherent evidence is validated separately", flush=True)
        return any(r["result"] != "passed" for r in previous_evidence.runs.values())
    cmake = shutil.which("cmake")
    if not cmake:
        raise RuntimeError("Use the configured MSVC developer shell with CMake on PATH")
    targets = {t for t, _ in bindings.values()} | ({"tiled_parity_tests"} if tiled and "tiled" in capture_families else set())
    targets |= {"downsample_tests"} if downsample and "downsample" in capture_families else set()
    command = [cmake, "--build", str(build_dir), "--config", config, "--target", *sorted(targets), "-j", "4"]
    groups = {f: [{"path": p.relative_to(ROOT).as_posix(), "sha256": sha(p)} for p in dependencies(f)] for f in sorted(families)}
    for family in COHERENT_INGESTION_FAMILIES:
        if family in groups:
            prior = previous_evidence.data.get("dependency_groups", {}).get(family)
            if prior:groups[family] = copy.deepcopy(prior)
            else:groups.pop(family)
    runtime_lock_path = ROOT / "dependencies/imaging/lock.json"
    runtime_lock_hash = sha(runtime_lock_path)
    print("Building current native contract targets", flush=True)
    build = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=600)
    if build.returncode:
        print(build.stdout + build.stderr)
        raise RuntimeError("Build failed; previous evidence retained")
    if sha(runtime_lock_path) != runtime_lock_hash:
        raise RuntimeError("Runtime dependency lock changed during build")
    runtime_files = [{"path": runtime_lock_path.relative_to(ROOT).as_posix(), "sha256": runtime_lock_hash}]
    binaries = read(runtime_lock_path).get("binaries", [])
    expected_names = {"heif.dll", "libde265.dll", "onnxruntime.dll", "onnxruntime_providers_shared.dll"}
    if len(binaries) != 4 or {Path(b["path"]).name for b in binaries} != expected_names:
        raise ValueError("Required deployed imaging runtime list changed")
    for binary in binaries:
        path = build_dir / config / Path(binary["path"]).name
        if not path.is_file() or sha(path) != binary["sha256"].lower():
            raise RuntimeError("Actual app-local runtime is missing or differs from the lock: " + str(path))
        runtime_files.append({"path": path.relative_to(ROOT).as_posix(), "sha256": binary["sha256"].lower()})
    for family, files in groups.items():
        if family in COHERENT_INGESTION_FAMILIES:continue
        for item in files:
            if sha(local(item["path"])) != item["sha256"]:
                raise RuntimeError(f"Source changed during build: {item['path']}; previous evidence retained")
    env = dict(os.environ)
    env["PATH"] = os.pathsep.join(str(ROOT / p) for p in ("dependencies/qt/bin", "dependencies/imaging/install/bin", "dependencies/imaging/onnxruntime-win-x64-1.30.0/lib")) + os.pathsep + env.get("PATH", "")
    env.update(QT_QPA_PLATFORM="offscreen", QT_PLUGIN_PATH=str(ROOT / "dependencies/qt/plugins"))
    records = [r for r in previous_evidence.data.get("runs", []) if r.get("dependency_group") in COHERENT_INGESTION_FAMILIES or refresh_stale and r.get("dependency_group") in families - capture_families]
    reused_evidence = Evidence()
    for record in records:
        if record.get("dependency_group") in COHERENT_INGESTION_FAMILIES:continue
        if reused_evidence.result(record["native_test"])[0] in ("stale_evidence", "not_run"):
            raise RuntimeError("Reused evidence changed during build: " + record["native_test"])
    for name, (target, argument) in bindings.items():
        if name.startswith(("decoder_adversarial.", "process_save.", "native_display_profile.", "viewport_cache.", "physical_viewport.", "native_pixel_grid.", "accessibility.", "retouch_extent.", "retouch_growth.", "project_open_batch.", "subject_input.")):
            continue  # Run each child-process suite once and retain all outcomes.
        exe = build_dir / config / (target + ".exe")
        if not exe.is_file():
            raise RuntimeError(f"Required executable absent: {exe}")
        started = datetime.now(timezone.utc).isoformat()
        arguments = [argument] + ([str(ROOT / "tests/display_profile/fixtures/linear-rgb.icc")] if name.startswith("display_profile.") else [])
        timeout = 30 if name.startswith("project_reopen.") else 45 if name.startswith(("remembered_settings.", "remembered_filter.")) else 60
        try:
            # export_ui runs four named assertions in one real-dialog suite. The
            # extra argument is ignored by that harness; the required PASS line
            # and overall exit still prove the requested witness executed.
            run = subprocess.run([str(exe), *arguments], cwd=ROOT, env=env, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=timeout)
            output, code = run.stdout + run.stderr, run.returncode
        except subprocess.TimeoutExpired as error:
            output, code = str(error), -1
        marker = bool(re.search(r"^PASS " + re.escape(argument) + r"\s*$", output, re.MULTILINE))
        result = "passed" if code == 0 and marker else "failed"
        records.append({"native_test": name, "result": result, "exit_code": code, "expected_pass_marker_observed": marker,
                        "started_utc": started, "command": [exe.relative_to(ROOT).as_posix(), *arguments], "executable_sha256": sha(exe),
                        "dependency_group": name.split(".")[0], "timeout_seconds": timeout, "output": output, "output_sha256": hashlib.sha256(output.encode()).hexdigest()})
        print(f"{result.upper()} {name}", flush=True)
    if tiled and "tiled" in capture_families:
        records += capture_tiled(build_dir, config, env, tiled)
    if downsample and "downsample" in capture_families:
        records += capture_downsample(build_dir, config, env, downsample)
    presentation = [name for name in selected_names if name.startswith("native_display_profile.")]
    if presentation:
        records += capture_presentation(build_dir, config, env, presentation)
    for family in ("decoder_adversarial", "process_save"):
        expected = {c["native_test"]: c for _, c in additions() if family in capture_families and c.get("native_test", "").startswith(family + ".")}
        if expected:
            records += capture_io_suite(build_dir, config, env, family, expected)
    records += capture_viewport(build_dir, config, env, selected_names)
    records += capture_subject_inputs(build_dir, config, env, selected_names)
    if "accessibility" in capture_families:
        records += capture_accessibility(build_dir, config, env)
    for family in ("retouch_extent", "retouch_growth", "project_open_batch"):
        if family in capture_families:
            records += capture_prepared_ctest(build_dir, config, env, family)
    for family, files in groups.items():
        if family in COHERENT_INGESTION_FAMILIES:continue
        for item in files:
            if sha(local(item["path"])) != item["sha256"]:
                raise RuntimeError(f"Source changed during execution: {item['path']}; previous evidence retained")
    for item in runtime_files:
        if sha(local(item["path"])) != item["sha256"]:
            raise RuntimeError("Deployed runtime or dependency lock changed during execution: " + item["path"])
    reused_evidence = Evidence()
    for record in records:
        if record.get("dependency_group") in COHERENT_INGESTION_FAMILIES:continue
        if record["dependency_group"] not in capture_families and reused_evidence.result(record["native_test"])[0] in ("stale_evidence", "not_run"):
            raise RuntimeError("Reused evidence changed during capture: " + record["native_test"])
    review = read(REVIEW)
    fingerprint = next(x for x in review["input_fingerprints"] if x["path"] == "tests/effects_tools/effects_tools_tests.cpp")
    if sha(local(fingerprint["path"])) != fingerprint["sha256"]:
        raise RuntimeError("The six-function assertion review is stale; re-audit before promotion")
    complete = [r for r in review["invocations"] if r["review_status"] == "assertion_complete_candidate"]
    complete_records = [{"id": r["id"], "upstream_test_id": r["upstream_test_id"], "source": r["source"],
        "native_tests": [n["ctest"] for n in r["native_complete_witnesses"]], "assertions": r["assertions"],
        "reference_kind": "pure_contract", "review_path": REVIEW.relative_to(ROOT).as_posix(), "review_sha256": sha(REVIEW),
        "claim": "All logical assertions reconciled; native execution only, no Mac runtime/differential claim"} for r in complete]
    if tiled:
        complete_records += [{"id": r["id"], "upstream_test_id": r["upstream_test_id"], "source": r["source"],
            "native_tests": ["tiled." + r["id"]], "assertions": r["assertions"], "reference_kind": "native_render_invariants",
            "review_path": TILED.relative_to(ROOT).as_posix(), "review_sha256": sha(TILED),
            "claim": "All source assertion/helper sites and original invocation parameters executed natively; no Mac pixel equivalence"} for r in tiled["invocations"]]
    complete_records += panel_contracts()
    complete_records += selection_contracts()
    if downsample:
        complete_records += [{"id": r["id"], "upstream_test_id": r["upstream_test_id"], "source": r["source"],
            "native_tests": ["downsample." + r["id"]], "assertions": r["assertions"], "reference_kind": "native_render_invariants",
            "review_path": DOWNSAMPLE.relative_to(ROOT).as_posix(), "review_sha256": sha(DOWNSAMPLE),
            "claim": "All 16 source assertion/helper sites retained and executed through integrated production Downsample rendering; no Mac pixel differential"} for r in downsample["invocations"]]
    complete_records = retain_paired_contracts(complete_records, previous_evidence.data.get("complete_source_contracts", []))
    for item in complete_records:
        item["source_file"] = {"path": "../upstream/" + item["source"]["path"], "sha256": sha(ROOT.parent / "upstream" / item["source"]["path"])}
    predecessor = None
    if PROOF.exists():
        archive = ROOT / "evidence/integration/native-contract-history" / (sha(PROOF) + ".json")
        archive.parent.mkdir(parents=True, exist_ok=True)
        if archive.exists() and sha(archive) != sha(PROOF):
            raise RuntimeError("Historical proof archive differs")
        if not archive.exists(): shutil.copyfile(PROOF, archive)
        predecessor = {"path": archive.relative_to(ROOT).as_posix(), "sha256": sha(archive)}
    write(PROOF, {"schema_version": 2, "baseline_sha": BASELINE, "captured_utc": datetime.now(timezone.utc).isoformat(),
        "previous_capture": predecessor, "refreshed_families": sorted(capture_families),
        "reused_runs": [r["native_test"] for r in records if r["dependency_group"] not in capture_families],
        "configuration": config, "build_directory": build_dir.relative_to(ROOT).as_posix(),
        "build": {"command": command, "exit_code": build.returncode, "output": build.stdout + build.stderr},
        "environment": {"qpa": "offscreen for editing/effects/dialog checks; windows for tiled canvas and native presentation", "tiled_scale_factor": 1,
                        "presentation_dpr": list(PRESENTATION_SCALES.values()),
                        "qt": "6.8.3", "platform": "Windows x64", "font_fixture": "%WINDIR%/Fonts/segoeui.ttf"},
        "dependency_groups": groups, "runtime_files": runtime_files, "runs": records, "assertion_review": {"path": REVIEW.relative_to(ROOT).as_posix(), "sha256": sha(REVIEW)},
        "integration_observation": integration_observation(),
        "configuration_observations": configuration_observations(),
        "adjustment_source_ingestion": previous_evidence.data.get("adjustment_source_ingestion"),
        "live_mask_source_ingestion": previous_evidence.data.get("live_mask_source_ingestion"),
        "hue_saturation_source_ingestion": previous_evidence.data.get("hue_saturation_source_ingestion"),
        "document_size_source_ingestion": previous_evidence.data.get("document_size_source_ingestion"),
        "selection_source_ingestion": previous_evidence.data.get("selection_source_ingestion"),
        "palette_source_ingestion": previous_evidence.data.get("palette_source_ingestion"),
        "release42_bounded_ingestion": previous_evidence.data.get("release42_bounded_ingestion"),
        "complete_source_contracts": complete_records,
        "limits": ["Bounded additions do not close entire source methods or broad feature rows.", "Only named assertion-complete contracts may be promoted.",
                   "Current source, executable, output and review hashes are checked on regeneration.", "External reference/model/human gates remain independent."]})
    return any(r["result"] != "passed" for r in records)


class Evidence:
    def __init__(self):
        self.data = read(PROOF) if PROOF.exists() else {}
        self.runs = {r["native_test"]: r for r in self.data.get("runs", [])}
        self.cache = {}
        self.dependency_sets = {}
        self.adjustment_bundle = {}
        self.release42_bundle = None

    def release42_observations(self):
        if self.release42_bundle is None:
            ingestion = self.data.get("release42_bounded_ingestion") or {}
            if ingestion.get("records") != 107 or not self.file(ingestion.get("path", ""), ingestion.get("sha256", "")):
                raise ValueError("Missing/changed coherent107 bounded bundle")
            value = release42.validate_bundle(local(ingestion["path"]))
            self.release42_bundle = {r["native_test"]: r for r in value["rows"]}
        return self.release42_bundle

    def adjustment_observations(self, family="adjustment_source"):
        _, _, counts = source_contract_spec(family)
        if family not in self.adjustment_bundle:
            ingestion = self.data.get(family + "_ingestion") or {}
            if ingestion.get("invocations") != len(counts) or ingestion.get("required_checks_per_configuration") != sum(counts.values()):
                raise ValueError("Missing paired source ingestion: " + family)
            for key in ("normal", "asan", "source_manifest"):
                item = ingestion.get(key)
                if not item or not self.file(item["path"], item["sha256"]):
                    raise ValueError("Changed/missing adjustment " + key + " evidence")
            normal, rows = adjustment_extraction(local(ingestion["normal"]["path"]), family=family)
            sanitizer, asan_rows = adjustment_extraction(local(ingestion["asan"]["path"]), family=family)
            if normal["source_manifest_sha256"] != sanitizer["source_manifest_sha256"] or normal["source_manifest_sha256"] != ingestion["source_manifest"]["sha256"]:
                raise ValueError("Adjustment normal/ASAN source manifests disagree")
            for observations, instrumented in ((rows, False), (asan_rows, True)):
                executable = local(next(iter(observations.values()))["command"][0])
                if (b"clang_rt.asan" in executable.read_bytes().lower()) != instrumented:
                    raise ValueError("Adjustment executed configurations differ from normal/ASAN labels")
            self.adjustment_bundle[family] = rows, asan_rows
        return self.adjustment_bundle[family]

    def file(self, path, expected):
        key = (path, expected)
        if key not in self.cache:
            p = local(path)
            self.cache[key] = p.is_file() and sha(p) == expected.lower()
        return self.cache[key]

    def result(self, name):
        r = self.runs.get(name)
        if not r:
            return "not_run", "No captured run"
        if self.data.get("baseline_sha") != BASELINE:
            return "stale_evidence", "Wrong baseline"
        if self.data.get("schema_version", 1) >= 2 and not self.data.get("runtime_files"):
            return "stale_evidence", "Missing deployed runtime fingerprints"
        for item in self.data.get("runtime_files", []):
            if not self.file(item["path"], item["sha256"]):
                return "stale_evidence", "Changed/missing deployed runtime: " + item["path"]
        files = self.data.get("dependency_groups", {}).get(r.get("dependency_group"), [])
        if not files:
            return "stale_evidence", "Missing dependency hashes"
        family = r.get("dependency_group")
        if family not in self.dependency_sets:
            self.dependency_sets[family] = {p.relative_to(ROOT).as_posix() for p in dependencies(family)}
        if {item["path"] for item in files} != self.dependency_sets[family] or len(files) != len(self.dependency_sets[family]):
            return "stale_evidence", "Dependency membership changed (added/removed source or required fixture)"
        for item in files:
            if not self.file(item["path"], item["sha256"]):
                return "stale_evidence", "Changed/missing source: " + item["path"]
        if not self.file(r["command"][0], r["executable_sha256"]):
            return "stale_evidence", "Changed/missing executed binary"
        if hashlib.sha256(r["output"].encode()).hexdigest() != r["output_sha256"]:
            return "stale_evidence", "Output hash mismatch"
        result_file = r.get("result_file")
        if result_file and not self.file(result_file["path"], result_file["sha256"]):
            return "stale_evidence", "Changed/missing invocation result file"
        for artifact in r.get("artifacts", []):
            if not self.file(artifact["path"], artifact["sha256"]):
                return "stale_evidence", "Changed/missing captured presentation artifact"
        input_file = r.get("input_file")
        if input_file and not self.file(input_file["path"], input_file["sha256"]):
            return "stale_evidence", "Changed/missing decoder input file"
        allowed_exits = (0, 1) if name.startswith("accessibility.") else (0, 8) if family in ("retouch_extent", "retouch_growth", "project_open_batch") else (0,)
        if r["result"] == "passed" and ((r["exit_code"] not in allowed_exits) or not r["expected_pass_marker_observed"]):
            return "failed", "Pass disagrees with exit/marker"
        if r["result"] == "passed" and not observed_pass(name, r["output"]):
            return "failed", "Expected pass marker absent from actual output"
        if family in release42.FAMILIES:
            try:
                expected = self.release42_observations().get(name)
                if expected is None or r != expected:
                    return "failed", "Bounded record differs from actual typed coherent reports"
            except (ValueError, KeyError, TypeError, OSError, ET.ParseError) as error:
                return "stale_evidence", str(error)
        if family == "subject_input":
            if not result_file:
                return "stale_evidence", "Missing actual subject input result"
            try:
                case = name.split(".", 1)[1]
                fixture, meta, _ = subject_input_fixture(case)
                expected = ["candidate", str(fixture), str(meta["width"]), str(meta["height"]), str(meta["stride"]), case,
                    str(local(result_file["path"])), "conformance" if case in SUBJECT_INPUT_CASES[:13] else "resource"]
                if r["command"][1:] != expected:
                    return "failed", "Subject input command/required fixture/recipe differs"
                passed = validate_subject_input_report(case, read(local(result_file["path"])), r["output"], r["exit_code"])
                if passed != (r["result"] == "passed"):
                    return "failed", "Subject input typed result disagrees with recorded outcome"
            except (ValueError, KeyError, TypeError, OSError) as error:
                return "failed", str(error)
        if family in PAIRED_SOURCE_FAMILIES:
            try:
                normal, sanitizer = self.adjustment_observations(family)
            except (ValueError, KeyError, TypeError, OSError, ET.ParseError) as error:
                return "stale_evidence", str(error)
            case = name.split(".", 1)[1]
            if case not in normal:
                return "failed", "Unknown adjustment source invocation"
            primary = normal[case]
            paired = (source44.paired_result(primary["result"], sanitizer[case]["result"]) if family in source44.FAMILIES
                      else "passed" if primary["result"] == sanitizer[case]["result"] == "passed" else "failed")
            expected = dict(primary, result=paired)
            if (any(r.get(key) != value for key, value in expected.items()) or r.get("normal_result") != primary["result"]
                    or r.get("sanitizer") != sanitizer[case]):
                return "failed", "Adjustment paired original check records differ from actual normal/ASAN reports"
        if family in ("retouch_extent", "retouch_growth", "project_open_batch"):
            item = r.get("junit")
            if not item or not self.file(item["path"], item["sha256"]):
                return "stale_evidence", "Missing/changed prepared CTest JUnit report"
            if hashlib.sha256(r.get("driver_output", "").encode()).hexdigest() != r.get("driver_output_sha256"):
                return "stale_evidence", "Prepared CTest driver output changed"
            catalog = r.get("configured_tests")
            if not catalog or not self.file(catalog["path"], catalog["sha256"]):
                return "stale_evidence", "Prepared CTest configured commands changed"
            try:
                resolved = validate_prepared_commands(read(local(catalog["path"])), family, local(r["command"][0]))
                if r.get("resolved_test_commands") != resolved:
                    return "failed", "Prepared command inventory disagrees with captured configuration"
                junit = validate_prepared_junit(local(item["path"]), family, r["exit_code"])
                values, outputs, artifacts = prepared_outcomes(family, junit)
                ident = name.split(".", 1)[1]
                if (r.get("junit_test_names") != prepared_ctest_names(family) or r.get("result_selector") != ident
                        or r.get("output") != outputs[ident] or (r["result"] == "passed") != values[ident]
                        or r.get("parameter_execution_count") != (2 if family == "retouch_growth" else 1)):
                    return "failed", "Prepared named assertions/JUnit/parameter outcome disagree"
                if family == "retouch_extent":
                    directory = extent_directory(r["output"])
                    expected_paths = {(directory / path).relative_to(ROOT).as_posix() for path in artifacts}
                    if (len(r.get("artifacts", [])) != 21 or {a["path"] for a in r.get("artifacts", [])} != expected_paths
                            or not result_file or local(result_file["path"]) != directory / "results.json"):
                        return "stale_evidence", "Required21 retouch raw buffers/report are not fingerprinted"
            except (ValueError, KeyError, TypeError, ET.ParseError) as error:
                return "failed", str(error)
        if name.startswith("accessibility."):
            if not result_file:
                return "stale_evidence", "Missing actual UIA report"
            try:
                rows = validate_accessibility_report(read(local(result_file["path"])), r["exit_code"], r["output"])
                ident = accessibility_contracts()[name]
                if r.get("result_selector") != ident or r.get("actual_case") != rows[ident] or r["result"] != rows[ident]["status"]:
                    return "failed", "UIA case status/selector disagrees with actual report"
            except (ValueError, KeyError, TypeError) as error:
                return "failed", str(error)
        if name.startswith(("viewport_cache.", "physical_viewport.", "native_pixel_grid.")):
            observations = r.get("observations", [])
            scales = [1] if name.startswith("viewport_cache.") else [1, 2]
            if [o.get("dpr") for o in observations] != scales or "\n".join(o["output"] for o in observations) != r["output"]:
                return "failed", "Viewport parameter observations or combined output disagree"
            count = 0
            for observation in observations:
                if observation["command"][0] != r["command"][0] or hashlib.sha256(observation["output"].encode()).hexdigest() != observation["output_sha256"]:
                    return "stale_evidence", "Viewport executable/output fingerprint mismatch"
                if r["result"] == "passed" and observation["exit_code"]:
                    return "failed", "Viewport parameter process failed"
                if name.startswith("viewport_cache."):
                    count += 1
                    continue
                item = observation.get("result_file")
                if not item or not self.file(item["path"], item["sha256"]):
                    return "stale_evidence", "Missing/changed viewport parameter report"
                for artifact in observation.get("artifacts", []):
                    if not self.file(artifact["path"], artifact["sha256"]):
                        return "stale_evidence", "Missing/changed viewport capture artifact"
                try:
                    report = read(local(item["path"]))
                    number, passed = validate_viewport_report(name, report, observation["dpr"], observation["output"])
                    if {Path(artifact["path"]).name for artifact in observation.get("artifacts", [])} != viewport_artifact_names(name.split(".")[0], report):
                        return "stale_evidence", "Required viewport image/raw artifact set changed"
                    count += number
                    if r["result"] == "passed" and not passed:
                        return "failed", "Viewport parameter report contains a failure"
                except (ValueError, KeyError, TypeError) as error:
                    return "failed", str(error)
            if count != r.get("parameter_execution_count"):
                return "failed", "Viewport behavior/parameter counts disagree"
        if name.startswith("native_display_profile."):
            if not result_file:
                return "stale_evidence", "Missing native presentation result report"
            try:
                report = read(local(result_file["path"]))
                validate_presentation_result(name, report)
                if (report.get("passed") is True) != (r["result"] == "passed"):
                    return "failed", "Native presentation report disagrees with recorded result"
                if r["result"] == "passed" and len(r.get("artifacts", [])) != 2:
                    return "stale_evidence", "Native presentation must retain both captures"
            except ValueError as error:
                return "failed", str(error)
        return r["result"], "Source and executed binary match captured evidence"

    def complete(self):
        result = []
        fallback = self.data.get("assertion_review", {})
        for item in self.data.get("complete_source_contracts", []):
            path, fingerprint = item.get("review_path", fallback.get("path")), item.get("review_sha256", fallback.get("sha256"))
            source = item.get("source_file")
            valid = bool(path and fingerprint and self.file(path, fingerprint))
            valid = valid and (not source or self.file(source["path"], source["sha256"]))
            paired_families = {name.split(".", 1)[0] for name in item.get("native_tests", [])} & set(PAIRED_SOURCE_FAMILIES)
            if paired_families:
                try:
                    if len(paired_families) != 1:
                        raise ValueError("Complete original method mixes unrelated scenario families")
                    expected = next(row for row in adjustment_contracts(next(iter(paired_families))) if row["id"] == item["id"])
                    valid = valid and item == expected
                except (ValueError, KeyError, TypeError, OSError, StopIteration):
                    valid = False
            result.append(dict(item, review_valid=valid))
        return result


def normalize(c):
    c = dict(c)
    for key, default in {"title": c.get("native_test", c["id"]), "kind": "native_contract", "executable_test": None,
        "implementation_status": "not_started", "native_contract_result": "not_run", "result": "unverified",
        "result_scope": "full_parity", "reference_status": "unverified"}.items():
        c.setdefault(key, default)
    c["upstream_evidence"] = seq(c.get("upstream_evidence", c.get("source_evidence")))
    for key in ("windows_implementation", "prerequisites"):
        c[key] = seq(c.get(key))
    c.setdefault("manual_steps", c.get("reproduction", {}).get("steps", []))
    c.setdefault("native_implementation_status", c["implementation_status"])
    c.setdefault("parity_result", c["result"])
    c["evidence_path"] = seq(c.get("evidence_path", c.get("evidence")))
    return c


def generate():
    existing = read(ROOT / "parity-ledger.json") if (ROOT / "parity-ledger.json").exists() else {}
    previous = {c["id"]: c for c in existing.get("cases", [])}
    evidence, cases = Evidence(), []
    for row in read(ROOT.parent / "research/feature-parity-seed.json")["rows"]:
        clauses = [s.strip().rstrip(".") for s in row["acceptance"].split(";") if s.strip()]
        for i, clause in enumerate(clauses, 1):
            cases.append({"id": f"{row['id']}.{i:02}", "group": row["id"], "title": clause, "kind": "feature_acceptance",
                "upstream_evidence": row["source_files"], "obligation_context": row["acceptance"],
                "prerequisites": ["Native Windows application", "Reference where required by the specific behavior"],
                "manual_steps": [f"Exercise {row['feature']}: {clause}.", "Retain fixture, settings, relevant mask/selection/history and result.", "Compare pinned contract and record external acceptance where required."]})
    review = read(REVIEW)
    by_review = {r["id"]: r for r in review["invocations"]}
    for candidate in read(ROOT / "audit/acceptance-candidates.json")["cases"]:
        c = dict(candidate)
        c.update(kind="upstream_test_invocation", result="unverified", result_scope="native_contract", parity_result="not_claimed",
                 native_contract_result="not_run", reference_status="not_required_for_native_contract", mac_differential_result="blocked_reference",
                 prerequisites=["Native implementation of the complete source fixture, helper preconditions, operations and assertions"], portability=by_review[c["id"]]["portability"])
        if c["portability"] == "licensed_replacement_contract":
            c["reference_status"] = "blocked_external_acceptance"
        elif c["portability"] == "capture_artifact_only":
            c["reference_status"] = "human_review_required"
        cases.append(c)
    controls = read(ROOT / "audit/command-inventory.json")
    for command in controls["control_sites"] + controls["event_handlers"]:
        cases.append({"id": command["id"], "kind": "control_or_event_audit", "title": command.get("declaration", command.get("function")),
            "upstream_evidence": command["source"], "prerequisites": ["Native interactive Windows session"],
            "manual_steps": ["Exercise source enablement/modifiers through real dispatch.", "Verify text focus, undo/cancel and Windows shortcut adaptation."]})
    for c in cases:
        old = previous.get(c["id"], {})
        for key in ("windows_implementation", "executable_test", "implementation_status", "evidence_path", "notes"):
            if key in old:
                c[key] = old[key]
        if old.get("result") == "failed":
            c.update(result="failed", native_contract_result="failed", parity_result="failed")
        elif c["kind"] != "upstream_test_invocation" and "result" in old:
            c["result"] = old["result"]
    for manifest, candidate in additions():
        c = normalize(candidate)
        name = c.get("native_test", "")
        claims = [s["claim"] for s in seq(c.get("source_evidence")) if s.get("claim")]
        outcome, reason = evidence.result(name)
        c.update(kind="bounded_native_contract", title=claims[0] if claims else c["title"], windows_implementation=c["windows_implementation"] or implementations(name),
            prerequisites=["Built Windows test and required local fixtures", "Pinned source/current evidence hashes"], executable_test=c.get("command"),
            result_scope="native_contract", parity_result="not_claimed", reference_status="not_required_for_native_contract",
            broader_reference_status=candidate.get("reference_status", "unverified"), complete_upstream_invocation=False,
            source_manifest=manifest.relative_to(ROOT).as_posix(), native_contract_result=outcome, evidence_validation=reason,
            result=outcome if outcome in ("passed", "failed") else "unverified")
        c["implementation_status"] = c["native_implementation_status"] = "implemented" if c["windows_implementation"] and all(local(p).is_file() for p in c["windows_implementation"]) else "not_started"
        if not c["upstream_evidence"]:
            c["source_relation"] = "native_platform_safeguard_no_mapped_upstream_assertion"
        c["evidence_path"] = sorted(set(c["evidence_path"] + [manifest.relative_to(ROOT).as_posix()] + ([PROOF.relative_to(ROOT).as_posix()] if name in evidence.runs else [])))
        if name in evidence.runs:
            c["executable_test"] = evidence.runs[name]["command"]
        cases.append(c)
    for gap in review["top_gaps"]:
        cases.append({"id": "COVERAGE-" + gap["id"], "kind": "discovered_behavior_review", "title": gap["title"],
            "upstream_evidence": gap["upstream_tests"] + seq(gap.get("production_source")), "windows_implementation": [gap["native_inspected"]["path"]],
            "implementation_status": "partial", "prerequisites": ["Current implementation review; frozen audit predates ongoing fixes"],
            "manual_steps": [gap["next_action"]], "notes": gap["finding"], "evidence_path": ["audit/windows-coverage-review.json", "docs/coverage-review.md"]})
    known = {c["id"] for c in cases}
    cases += [c for ident, c in previous.items() if ident not in known]
    cases = [normalize(c) for c in cases]
    if len({c["id"] for c in cases}) != len(cases):
        raise ValueError("Duplicate acceptance IDs")
    by_id = {c["id"]: c for c in cases}
    by_source = {c.get("upstream_test_id"): c for c in cases if c["kind"] == "upstream_test_invocation" and c.get("argument") is None}
    for c in cases:
        if c["kind"] != "bounded_native_contract":
            continue
        for item in c.get("source_evidence", []):
            upstream = by_source.get(item.get("upstream_test_id"))
            if upstream:
                upstream.setdefault("bounded_native_witnesses", []).append(c["id"])
                if c["native_contract_result"] == "passed":
                    upstream["windows_implementation"] = sorted(set(upstream["windows_implementation"] + c["windows_implementation"]))
                    if upstream["implementation_status"] == "not_started":
                        upstream["implementation_status"] = "partial"
                    upstream["native_implementation_status"] = upstream["implementation_status"]
                    upstream["native_evidence_status"] = "partial_assertions_only"
                elif c["native_contract_result"] == "failed":
                    upstream.update(result="failed", native_contract_result="failed", parity_result="failed", implementation_status="partial",
                                    native_implementation_status="partial", failed_native_test=c["native_test"], executable_test=c["executable_test"],
                                    windows_implementation=c["windows_implementation"], evidence_path=c["evidence_path"],
                                    native_evidence_status="required_assertion_failed")
    for complete in evidence.complete():
        c = by_id[complete["id"]]
        states = [evidence.result(n) for n in complete["native_tests"]]
        outcomes = [s[0] for s in states]
        outcome = "passed" if outcomes and all(s == "passed" for s in outcomes) else "failed" if "failed" in outcomes else "stale_evidence"
        if not complete["review_valid"]:
            outcome = "stale_evidence"
        c.update(complete_assertion_reconciliation=complete["assertions"], native_tests=complete["native_tests"], native_contract_result=outcome,
            complete_helper_reconciliation=complete.get("helper_assertions", []),
            result=outcome if outcome in ("passed", "failed") else "unverified", implementation_status="implemented", native_implementation_status="implemented",
            result_scope="native_contract", parity_result="not_claimed", mac_differential_result="blocked_reference" if complete.get("reference_kind") == "native_render_invariants" else "not_applicable_to_pure_contract",
            reference_status="not_required_for_native_contract", complete_upstream_invocation=outcome == "passed",
            windows_implementation=sorted({p for n in complete["native_tests"] for p in implementations(n)}),
            executable_test=[evidence.runs[n]["command"] for n in complete["native_tests"] if n in evidence.runs], evidence_path=["audit/native-contract-evidence.json", complete.get("review_path", "audit/windows-coverage-review.json")],
            evidence_validation=[s[1] for s in states] + ([] if complete["review_valid"] else ["Changed/missing complete assertion review or pinned source"]))
    source44.annotate_ineligible(_source44_host, evidence, by_id)
    observation = evidence.data.get("integration_observation")
    if observation and evidence.file(observation["path"], observation["sha256"]):
        for c in cases:
            if c["result"] == "failed" and any(name in str(c["executable_test"]) for name in observation["failed"]):
                c["evidence_path"] = sorted(set(c["evidence_path"] + [observation["path"], "audit/native-contract-evidence.json"]))
                c["latest_failed_observation"] = observation["timestamp"]
    if DOWNSAMPLE.exists():
        review = read(DOWNSAMPLE)
        for row in review["invocations"]:
            c = by_id[row["id"]]
            c["candidate_assertion_map"] = DOWNSAMPLE.relative_to(ROOT).as_posix()
            c["production_renderer_integrated"] = bool(row.get("global_renderer_integrated", review.get("global_renderer_integrated", False)))
            c["isolated_adapter_historical_result"] = row.get("native_result", "not_run")
            c["candidate_note"] = "A historical isolated-adapter result does not promote the production renderer; current integrated execution and source hashes are required."
    for c in cases:
        c.update(implemented=c["implementation_status"] == "implemented", verified=c["result"] == "passed",
                 full_parity_verified=c["result"] == "passed" and c["result_scope"] == "full_parity")
        if (not c["upstream_evidence"] and c.get("source_relation") != "native_platform_safeguard_no_mapped_upstream_assertion") or not c["prerequisites"] or not (c["executable_test"] or c["manual_steps"]):
            raise ValueError(f"Missing source/prerequisite/reproduction: {c['id']}")
    ledger = {"schema_version": 2, "baseline_sha": BASELINE, "reference_access": existing.get("reference_access", {"status": "blocked_reference"}),
        "evidence_evaluated_utc": datetime.now(timezone.utc).isoformat(),
        "native_evidence_captured_utc": evidence.data.get("captured_utc"),
        "preserved_checkpoint": preserved_checkpoint(),
        "denominator_status": "expanding_source_audit_not_final", "grouped_floor": 94,
        "policy": "Result is qualified by result_scope. Native contracts are not Mac differential or broad feature passes. Full source invocations require every assertion plus fresh execution. Overlapping refinements are not a feature-percentage denominator.",
        "external_gates": existing.get("external_gates", [{"id": "mac_reference_and_exchange", "result": "blocked_reference"},
            {"id": "foreground_replacement_quality", "result": "unaccepted"}, {"id": "interactive_visual_acceptance", "result": "unverified"},
            {"id": "clean_machine_packaging_acceptance", "result": "unverified"}]), "integration_observation": observation,
        "native_configuration": evidence.data.get("configuration", "not_captured"),
        "configuration_observations": configuration_observations(), "cases": cases}
    write(ROOT / "parity-ledger.json", ledger)
    return ledger


def totals(ledger):
    rows = ledger["cases"]
    failed = [c for c in rows if c["native_contract_result"] == "failed"]
    failure_ids = {c.get("failed_native_test", c.get("native_test", str(c["executable_test"]))) for c in failed}
    return {"cases": len(rows), "kinds": dict(Counter(c["kind"] for c in rows)),
        "native_implemented": sum(c["native_implementation_status"] == "implemented" for c in rows),
        "native_verified": sum(c["native_contract_result"] == "passed" for c in rows),
        "complete_source_invocations_verified": sum(c["kind"] == "upstream_test_invocation" and c.get("complete_upstream_invocation", False) for c in rows),
        "full_parity_verified": sum(c["full_parity_verified"] for c in rows), "runtime_failed": len(failure_ids), "runtime_failed_records": len(failed),
        "stale_evidence": sum(c["native_contract_result"] == "stale_evidence" for c in rows), "unclosed": sum(c["result"] != "passed" for c in rows)}


def report(ledger):
    n = totals(ledger)
    lines = ["# Compositor Windows parity", "", f"Pinned upstream: `{BASELINE}`.", "",
        f"Working-tree evidence eligibility evaluated at `{ledger['evidence_evaluated_utc']}` against the native proof captured at `{ledger['native_evidence_captured_utc']}`. This evaluation checks evidence and source hashes; it does not execute native tests.", "",
        f"The expanding inventory has **{n['cases']} acceptance records**: 94 grouped obligations refined into cases, all 313 source invocations, control/event sites, **{n['kinds'].get('bounded_native_contract', 0)} bounded native contracts** and discovered gaps. Overlapping refinements are explicit; this is not a frozen feature-percentage denominator.", "",
        f"Native contracts with an implementation: **{n['native_implemented']}**. Native contracts verified with current `{ledger['native_configuration']}` evidence: **{n['native_verified']}**. Complete source invocation contracts verified natively: **{n['complete_source_invocations_verified']}/313**. Full parity records verified: **{n['full_parity_verified']}**.", "",
        f"Distinct runtime failures retained: **{n['runtime_failed']} across {n['runtime_failed_records']} ledger records**. Stale evidence: **{n['stale_evidence']}**. Unclosed records: **{n['unclosed']}**. Bounded tests do not close whole source methods or broad feature obligations.", "",
        "`result` is qualified by `result_scope`; `native_contract_result`, `parity_result` and `reference_status` remain separate in [the ledger](parity-ledger.json). Missing Mac access does not block portable logic assertions. Native passes do not establish Mac differential results or human acceptance.", "",
        "[Native evidence](audit/native-contract-evidence.json) includes actual commands, exits/stdout, source/binary/deployed-runtime hashes, captured PNG hashes and complete assertion mappings. [The source audit](docs/coverage-review.md) identifies remaining families. Six pure ColorPicker/Levels/Hue contracts, 12 TiledLayer invocations, three small-image CanvasThumbnail methods and three Selection gesture/history methods have complete assertion mappings. The four Downsample methods additionally require an explicit integrated production-renderer run. Promotions require the reviewed test source, executed binary and evidence to match; Swift/macOS execution remains separate.", "",
        "The [tiled mapping](audit/tiled-acceptance-additions.json) covers all 28 source assertion/helper sites with original parameters and thresholds (2 unrotated/edge, 12 rotated, 3 canvas shift). Its native canvas runs use the Windows Qt backend at scale 1. The earlier 212-byte failure remains in [the original evidence](evidence/graphics/tiled-parity-01/results.json); a corrected run does not erase it.", "",
        "Required editing/effects additions are retained as bounded contracts. The 74 decoder inputs, five forced-termination save stages and four long-path cases each retain an individual record; finite corpus and process-crash results do not establish exhaustive fuzzing, power-loss recovery or full source methods. New additions and prior discovered records survive regeneration. Frozen coverage gaps stay open until explicitly mapped current evidence closes them; implementation changes alone cannot promote them.", "",
        "The inventory also includes 15 remembered-setting workflows, seven standalone profile conversions, seven registry watcher contracts and 28 native presentation checks at DPR 1, 1.25, 1.5 and 2. The [Release32 presentation capture](evidence/native-display-ledger-05/manifest.json) preserves 28 reports and 56 PNGs. The [Release30 capture](evidence/native-display-ledger-04/manifest.json) and [Release28 capture](evidence/native-display-ledger-03/manifest.json) remain retained. Physical monitor appearance and human acceptance remain open.", "",
        "Twelve viewport behavior records retain seven cache checks, 280 physical-sampling parameter executions and six pixel-grid/Actual Pixels executions. These 293 measurements support 12 behavior rows; repeated DPR/parameter runs do not create extra source-invocation credit. Current eligibility still requires the typed report, source, binary and artifact checks.", "",
        "Thirty-seven accessibility records use actual Windows UI Automation snapshots, focused-control observations and an Invoke-driven dialog cancellation. Every required check must appear exactly once. The actual grouped exit and each individual status are retained, including independent passing checks when the complete suite contains a failure. Narrator and human accessibility acceptance remain unperformed.", "",
        "Release32 adds 39 bounded records: seven retouch extent cases with all 21 raw RGBA buffers checked, 15 growth groups requiring both CPU and WARP execution, and 17 project Open workflows. Their actual CTest commands, complete JUnit outputs, named assertions and artifact hashes are retained. These checks add no complete upstream method claims. Four separately executed native Open-dialog cases remain outside this typed capture pending a reviewed adapter; source deferred-drop semantics remain an open distinction.", "", "External gates:", ""]
    checkpoint = ledger.get("preserved_checkpoint")
    if checkpoint:
        count = checkpoint["totals"]
        lines[6:6] = [f"The dated [Release30 checkpoint]({checkpoint['path']}) preserves **{count['native_verified']} verified native contracts / {count['cases']} records / {count['stale_evidence']} stale evidence** at `{checkpoint['captured_utc']}`, including {count['complete_source_invocations_verified']} complete source invocations. Later edits are evaluated below; the archived result is preserved byte-for-byte.", ""]
    lines += [f"- `{g['id']}`: `{g['result']}`." for g in ledger["external_gates"]]
    observation = ledger.get("integration_observation")
    if observation:
        lines += ["", f"The recorded integration batch [{observation['path']}]({observation['path']}) reports **{observation['passed']}/{observation['tests']} passed**, {len(observation['failed'])} failed and {len(observation['skipped'])} skipped at `{observation['timestamp']}`. This dated batch is separate from full source assertion acceptance; its report hash is retained."]
    for observation in ledger.get("configuration_observations", []):
        failures = [r for r in observation["results"] if r["result"] == "failed"]
        if failures:
            lines += ["", f"The earlier `{observation['configuration']}` run at `{observation['timestamp']}` retains **{len(failures)} failed memory gates** in [{observation['report']}]({observation['report']}). These original observations remain preserved.", ""]
            rows = failures
        else:
            rows = observation["results"]
            lines += ["", f"The later `{observation['configuration']}` run at `{observation['timestamp']}` passed **{len(rows)} unchanged export memory gates** in [{observation['report']}]({observation['report']}) after the blank-document allocation correction. The earlier failed runs remain above.", ""]
        lines += [f"- `{r['native_test']}`: peak working set {r['peak_working_set_bytes']:,} B, peak commit {r['peak_commit_bytes']:,} B; unchanged limit {r['limit_bytes']:,} B." for r in rows]
    lines += ["", "`verify-parity` returns failure while a required record/external gate is unclosed or the denominator is expanding.", "",
        "Refresh evidence from the configured MSVC developer shell:", "", "```powershell",
        "python scripts/parity_ledger.py --capture-native --build-dir build/release --config Release",
        "# After a bounded change, refresh only stale/new families:",
        "python scripts/parity_ledger.py --capture-native --refresh-stale --build-dir build/release --config Release",
        "python scripts/parity_ledger.py --self-test --verify", "```", "",
        "A full capture builds the actual targets and runs every required additions test. A stale-only capture reruns affected/new families and revalidates all reused source/binary/runtime/report hashes before and after execution. Missing fixtures, failed tests, changed or newly added source and stale assertion reviews fail. Verification checks hashes and gates without running tests.", "",
        "The packaged application-source archive supports rebuilding the application. Full source-attributable parity reproduction also requires the pinned upstream checkout plus the workspace research, audit and evidence packet, including required local fixtures and locked dependencies. The application-source archive does not contain all parity inputs.", "",
        "| ID | Requirement | Native implementation | Native result | Scoped result |", "|---|---|---|---|---|"]
    for c in ledger["cases"]:
        title = str(c["title"]).replace("|", "/").replace("\n", " ")
        lines.append(f"| {c['id']} | {title} | {c['native_implementation_status']} | {c['native_contract_result']} | {c['result']} ({c['result_scope']}) |")
    (ROOT / "PARITY.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def verify(ledger):
    for c in ledger["cases"]:
        if c["result"] == "passed" and (not c["evidence_path"] or not c["windows_implementation"]):
            raise ValueError(f"Claimed pass without implementation/evidence: {c['id']}")
    n = totals(ledger)
    external = [g["id"] for g in ledger["external_gates"] if g["result"] != "passed"]
    complete = not n["unclosed"] and not external and ledger["denominator_status"] == "final_source_audited"
    print(json.dumps({**n, "external_unclosed": external, "denominator_status": ledger["denominator_status"], "parity_complete": complete}))
    return 0 if complete else 1


def self_test():
    """Exercise evidence rejection without changing a source, fixture or result file."""
    first = Evidence()
    name = next((n for n in first.runs if first.result(n)[0] == "passed"), None)
    if not name:
        raise ValueError("A current successful capture is required for verifier self checks")
    tests = [("unknown case", lambda e: e.runs.pop(name), "not_run"),
             ("wrong baseline", lambda e: e.data.update(baseline_sha="0" * 40), "stale_evidence"),
             ("output corruption", lambda e: e.runs[name].update(output=e.runs[name]["output"] + "changed"), "stale_evidence"),
             ("binary hash change", lambda e: e.runs[name].update(executable_sha256="0" * 64), "stale_evidence"),
             ("source hash change", lambda e: e.data["dependency_groups"][e.runs[name]["dependency_group"]][0].update(sha256="0" * 64), "stale_evidence"),
             ("source membership change", lambda e: e.data["dependency_groups"][e.runs[name]["dependency_group"]].pop(), "stale_evidence"),
             ("deployed runtime corruption", lambda e: e.data.update(runtime_files=[{"path": "audit/native-contract-evidence.json", "sha256": "0" * 64}]), "stale_evidence"),
             ("nonzero exit", lambda e: e.runs[name].update(exit_code=1), "failed"),
             ("missing pass marker", lambda e: e.runs[name].update(expected_pass_marker_observed=False), "failed"),
             ("invocation report corruption", lambda e: e.runs[name].update(result_file={"path": "audit/native-contract-evidence.json", "sha256": "0" * 64}), "stale_evidence"),
             ("presentation artifact corruption", lambda e: e.runs[name].update(artifacts=[{"path": "audit/native-contract-evidence.json", "sha256": "0" * 64}]), "stale_evidence"),
             ("decoder input corruption", lambda e: e.runs[name].update(input_file={"path": "audit/native-contract-evidence.json", "sha256": "0" * 64}), "stale_evidence")]
    for title, corrupt, expected in tests:
        evidence = Evidence()
        corrupt(evidence)
        actual, _ = evidence.result(name)
        if actual != expected:
            raise AssertionError(f"{title}: expected {expected}, got {actual}")
        print("PASS verifier " + title)
    evidence = Evidence()
    for row in evidence.data["complete_source_contracts"]:
        row["review_sha256"] = "0" * 64
    if any(row["review_valid"] for row in evidence.complete()):
        raise AssertionError("Changed assertion review remained eligible")
    print("PASS verifier assertion review change")
    if not observed_pass("native_display_profile.linear_frame_dpr_1_25", "PASS linear_frame max=1 dpr=1.25\n"):
        raise AssertionError("Valid presentation marker rejected")
    if observed_pass("native_display_profile.linear_frame_dpr_1_25", "PASS linear_frame max=1 dpr=1\n"):
        raise AssertionError("Incorrect presentation DPR marker accepted")
    print("PASS verifier presentation DPR mismatch")
    if observed_pass("native_display_profile.srgb_identity_dpr_1", "PASS srgb_identity max=1 dpr=1\n") or observed_pass("native_display_profile.linear_frame_dpr_1", "PASS linear_frame max=3 dpr=1\n"):
        raise AssertionError("Presentation marker exceeded its frozen threshold")
    print("PASS verifier presentation thresholds")
    viewport = read(VIEWPORT_MAP)
    for candidate in viewport["cases"]:
        name = candidate["native_test"]
        if name.startswith("viewport_cache."):
            continue
        outputs = []
        for observation in candidate["native_observations"]:
            if sha(local(observation["report"]["path"])) != observation["report"]["sha256"].lower():
                raise AssertionError("Frozen verifier viewport fixture report changed")
            report = read(local(observation["report"]["path"]))
            scale = observation["dpr"]
            directory = local(observation["report"]["path"]).parent
            log = directory.parent / (directory.name + ".log")
            output = log.read_text(encoding="utf-8-sig")
            count, passed = validate_viewport_report(name, report, scale, output)
            if count != observation["executions"] or not passed:
                raise AssertionError("Existing viewport witness failed typed verification")
            outputs.append(output)
            corruptions = [("missing parameter", lambda r: r["cases"].pop()),
                           ("DPR mismatch", lambda r: r["cases"][0].update(dpr=3)),
                           ("threshold changed", lambda r: r["cases"][0].update(tolerance=2)),
                           ("measurement mismatch", lambda r: r["cases"][0].update(maximum_difference=2))]
            for label, mutate in corruptions:
                changed = copy.deepcopy(report)
                mutate(changed)
                try:
                    validate_viewport_report(name, changed, scale, output)
                except ValueError:
                    continue
                raise AssertionError("Viewport verifier accepted " + label)
        if not observed_pass(name, "\n".join(outputs)):
            raise AssertionError("Viewport aggregate markers rejected")
    print("PASS verifier viewport exact parameters/DPR/thresholds/measurements/markers")
    uia = read(ROOT / "tests/accessibility/capture-03/uia.json")
    uia_output = (ROOT / "tests/accessibility/capture-03/stdout.log").read_text(encoding="utf-8-sig")
    result = validate_accessibility_report(uia, 1, uia_output)
    if sum(row["status"] == "passed" for row in result.values()) != 36 or result["layer_visibility_name"]["status"] != "failed":
        raise AssertionError("Retained UIA 36/37 witness changed")
    for label, mutate in (("missing check", lambda d: d["checks"].pop()),
                          ("wrong summary", lambda d: d.update(status="passed")),
                          ("unknown ID", lambda d: d["checks"][0].update(id="unknown")),
                          ("missing native tree", lambda d: d.update(main_tree=[]))):
        changed = copy.deepcopy(uia)
        mutate(changed)
        try:
            validate_accessibility_report(changed, 1, uia_output)
        except ValueError:
            continue
        raise AssertionError("UIA verifier accepted " + label)
    for code, output in ((0, uia_output), (2, uia_output), (1, "")):
        try:
            validate_accessibility_report(uia, code, output)
        except ValueError:
            continue
        raise AssertionError("UIA verifier accepted incorrect exit/marker")
    print("PASS verifier UIA exact checks/individual failures/complete exit/native tree")
    cpu = (ROOT / "tests/retouch_extent/contracts-04/contracts-cpu.log").read_text(encoding="utf-8-sig")
    warp = (ROOT / "tests/retouch_extent/contracts-04/contracts-warp.log").read_text(encoding="utf-8-sig")
    if not all(validate_growth_output(cpu, False).values()) or not all(validate_growth_output(warp, True).values()):
        raise AssertionError("Historical growth verifier fixtures changed")
    for label, output, backend in (("missing growth group", cpu.replace("PASS half_selection_once\n", ""), False),
                                   ("wrong backend", cpu, True),
                                   ("wrong summary", cpu.replace('"passed":15', '"passed":14'), False),
                                   ("duplicate group", cpu + "PASS half_selection_once\n", False)):
        try:
            validate_growth_output(output, backend)
        except ValueError:
            continue
        raise AssertionError("Growth verifier accepted " + label)
    print("PASS verifier retouch growth membership/backend/counts")
    # This is a parser test over a retained root-run fixture, not a new native run.
    historical = ET.parse(ROOT / "evidence/integration/retouch-31.xml").getroot()
    extent = next(row for row in historical.iter("testcase") if row.get("name") == "retouch.extent")
    output = extent.findtext("system-out")
    directory = extent_directory(output)
    if not all(validate_extent_output(directory, output)[0].values()):
        raise AssertionError("Historical extent verifier fixture changed")
    original = read(directory / "results.json")
    for label, mutate in (("missing extent case", lambda r: r["cases"].pop()),
                          ("wrong loss", lambda r: r["cases"][0].update(lost_outside_pixels=1)),
                          ("missing control", lambda r: r["cases"][0].update(control_began=False))):
        changed = copy.deepcopy(original)
        mutate(changed)
        try:
            validate_extent_output(directory, output, report_override=changed)
        except ValueError:
            continue
        raise AssertionError("Extent verifier accepted " + label)
    buffers = [(directory / ("clone-" + suffix + ".rgba")).read_bytes() for suffix in ("actual", "padded-control", "alpha-diff")]
    for label, changed in (("short raw buffer", [buffers[0][:-1], *buffers[1:]]),
                           ("RGB-only corruption", [bytes([1]) + buffers[0][1:], *buffers[1:]]),
                           ("incorrect stored alpha difference", [*buffers[:2], bytes([1]) + buffers[2][1:]])):
        try:
            validate_extent_output(directory, output, buffer_overrides={"clone": changed})
        except ValueError:
            continue
        raise AssertionError("Extent verifier accepted " + label)
    print("PASS verifier retouch raw dimensions/RGBA/preconditions/differences")
    # Synthetic XML is deliberately restricted to testing rejection behavior.
    suite = ET.Element("testsuite")
    for case in PROJECT_OPEN_CASES:
        row = ET.SubElement(suite, "testcase", name="project_open_batch." + case, status="run")
        ET.SubElement(row, "system-out").text = "START " + case + "\nPASS " + case + "\n"
    def xml_input(tree):
        return io.StringIO(ET.tostring(tree, encoding="unicode"))
    validated = validate_prepared_junit(xml_input(suite), "project_open_batch", 0)
    if not all(prepared_outcomes("project_open_batch", validated)[0].values()):
        raise AssertionError("Valid synthetic JUnit rejected by parser")
    for label, mutate, code in (("missing case", lambda t: t.remove(t[0]), 0),
                               ("skipped case", lambda t: ET.SubElement(t[0], "skipped"), 0),
                               ("exit disagreement", lambda t: None, 8),
                               ("truncated output", lambda t: setattr(t[0].find("system-out"), "text", "[This part of the test output was removed since it exceeds the threshold of 1024 bytes.]"), 0),
                               ("missing named assertion", lambda t: setattr(t[0].find("system-out"), "text", ""), 0)):
        changed = copy.deepcopy(suite)
        mutate(changed)
        try:
            values = validate_prepared_junit(xml_input(changed), "project_open_batch", code)
            prepared_outcomes("project_open_batch", values)
        except ValueError:
            continue
        raise AssertionError("Prepared JUnit verifier accepted " + label)
    print("PASS verifier prepared JUnit membership/skips/actual-exit/named-assertions")
    for family, target in (("retouch_extent", "retouch_extent_tests"), ("retouch_growth", "retouch_growth_tests"),
                           ("project_open_batch", "project_open_batch_tests")):
        exe = ROOT / "build/release/Release" / (target + ".exe")
        tests = []
        for name in prepared_ctest_names(family):
            properties = [{"name": "TIMEOUT", "value": {"retouch_extent": 60, "retouch_growth": 120, "project_open_batch": 30}[family]}]
            if family == "retouch_extent":
                command = ["C:/tools/cmake.exe", "-DWITNESS=" + str(exe), "-DEVIDENCE_ROOT=" + str(ROOT / "evidence/retouch-extent-integrated"),
                           "-P", str(ROOT / "tests/retouch_extent/run_witness.cmake")]
            elif family == "retouch_growth":
                command = [str(exe)] + ([str(ROOT / "shaders/BrushCoverage.hlsl")] if name.endswith("_warp") else [])
            else:
                command = [str(exe), name.split(".", 1)[1]]
                properties.append({"name": "ENVIRONMENT", "value": ["QT_QPA_PLATFORM=offscreen"]})
            tests.append({"name": name, "command": command, "properties": properties})
        catalog = {"tests": tests}
        validate_prepared_commands(catalog, family, exe)
        changed = copy.deepcopy(catalog)
        changed["tests"][0]["command"].append("unexpected")
        try:
            validate_prepared_commands(changed, family, exe)
        except ValueError:
            continue
        raise AssertionError("Prepared adapter accepted unreviewed executable arguments")
    print("PASS verifier configured CTest executable/wrapper/arguments/platform/timeouts")


def self_test_live_mask_source():
    """Validate isolated reports as parser fixtures, with no current promotion."""
    from unittest.mock import patch
    contracts = adjustment_contracts("live_mask_source")
    assert len(contracts) == 5 and sum(row["expected_source_check_executions"] for row in contracts) == 74
    directory = ROOT / "tests/live_mask_source/isolated-02/runtime-01"
    capture = read(directory / "results.json")
    if capture["changed_inputs"] or {row["case"] for row in capture["rows"]} != set(LIVE_MASK_CHECKS):
        raise AssertionError("Isolated LiveMask report membership/stability differs")
    checks = [{"name": "exact five existing IDs and74 reviewed checks", "result": "passed"}]
    for row in capture["rows"]:
        case = row["case"]
        path = local(row["report"])
        assert sha(path) == row["report_sha256"]
        value = read(path)
        output = (directory / (case + ".log")).read_text(encoding="utf-8-sig")
        passed, count = validate_adjustment_report(case, value, output, row["exit_code"], "live_mask_source")
        assert passed and count == LIVE_MASK_CHECKS[case]
        checks.append({"name": case + " actual report and original multiplicities", "result": "passed"})
        changed = copy.deepcopy(value)
        changed["checks"].pop()
        try:
            validate_adjustment_report(case, changed, output, 0, "live_mask_source")
        except ValueError:
            checks.append({"name": case + " missing check rejected", "result": "passed"})
        else:
            raise AssertionError("Missing original LiveMask check accepted")
    sample = read(directory / "clipping_color.json")
    output = (directory / "clipping_color.log").read_text(encoding="utf-8-sig")
    for name, mutate in (("duplicate line29", lambda v: v["checks"].append(next(c for c in v["checks"] if c["source_line"] == 29))),
            ("missing asset helper", lambda v: v["checks"].remove(next(c for c in v["checks"] if c["source_line"] == 40))),
            ("unknown source line", lambda v: v["checks"][0].update(source_line=10000)),
            ("wrong source schema", lambda v: v.update(schema="other")),
            ("failure concealed by status", lambda v: v["checks"][0].update(passed=False))):
        changed = copy.deepcopy(sample);mutate(changed)
        try:
            validate_adjustment_report("clipping_color", changed, output, 0, "live_mask_source")
        except ValueError:
            checks.append({"name": name + " rejected", "result": "passed"})
        else:
            raise AssertionError("Malformed LiveMask evidence accepted: " + name)
    evidence = Evidence()
    evidence.data = {"complete_source_contracts": copy.deepcopy(contracts)}
    with patch.object(evidence, "file", return_value=True):
        assert sum(row["review_valid"] for row in evidence.complete()) == 5
        evidence.data["complete_source_contracts"][0]["argument"] = "wrong"
        assert sum(row["review_valid"] for row in evidence.complete()) == 4
    checks.append({"name": "changed original parameter conjunction rejected", "result": "passed"})
    assert len(read(ROOT / "audit/acceptance-candidates.json")["cases"]) == 313
    return {"schema": "LIVE_MASK_PROOF_ADAPTER42_SELFTEST_V1", "status": "passed", "checks": checks,
        "script_sha256": sha(Path(__file__)), "original_invocation_denominator": 313, "source_candidates": 5,
        "original_dynamic_checks": 74, "current_promotions": 0,
        "scope": "Parser fixture validation and independent semantic review only. Current coherent normal+ASAN evidence has not been ingested."}


def self_test_new_contracts():
    """Read existing artifacts only; never execute native tests or write proof."""
    from unittest.mock import patch
    checks = []
    def accepted(name, fn):
        fn()
        checks.append({"name": name, "result": "passed"})
    def rejected(name, fn):
        try:
            fn()
        except (ValueError, KeyError, TypeError, OSError):
            checks.append({"name": name, "result": "passed"})
            return
        raise AssertionError("Verifier accepted malformed evidence: " + name)

    contracts = adjustment_contracts()
    assert len(contracts) == 13 and sum(r["expected_source_check_executions"] for r in contracts) == 179
    accepted("original313 IDs and13 invocation/179 assertion conjunction", adjustment_contracts)
    historical = []
    for folder in ("after-03-release41", "after-04-asan41"):
        path = ROOT / "tests/adjustment_source" / folder / "results.json"
        data, observations = adjustment_extraction(path, require_current=False)
        assert len(observations) == 13 and sum(r["source_check_executions"] for r in observations.values()) == 179
        accepted(folder + " complete historical JUnit/report crosscheck", lambda p=path: adjustment_extraction(p, False))
        rejected(folder + " stale current source rejection", lambda p=path: adjustment_extraction(p, True))
        historical.append({"path": path.relative_to(ROOT).as_posix(), "sha256": sha(path), "invocations": 13,
            "source_check_executions": 179, "source_manifest_sha256": data["source_manifest_sha256"], "current_result": "stale_evidence"})
    sample = observations["editor_grain"]
    original = read(local(sample["result_file"]["path"]))
    mutations = {}
    value = copy.deepcopy(original);value["checks"].pop();mutations["missing dynamic helper/check"] = value
    value = copy.deepcopy(original);value["checks"].append(dict(value["checks"][0]));mutations["duplicate dynamic helper/check"] = value
    value = copy.deepcopy(original);value["checks"][0]["source_line"] = 999999;mutations["unknown source line"] = value
    value = copy.deepcopy(original);value["checks"][0]["passed"] = False;mutations["false success predicate"] = value
    value = copy.deepcopy(original);value["source_expect_failures"] = 1;mutations["false source failure count"] = value
    value = copy.deepcopy(original);value["case"] = "editor_levels";mutations["wrong parameter/case"] = value
    for name, value in mutations.items():
        rejected("adjustment " + name, lambda v=value: validate_adjustment_report("editor_grain", v, sample["output"], 0))
    rejected("adjustment missing actual PASS marker", lambda: validate_adjustment_report("editor_grain", original, "", 0))
    rejected("adjustment nonzero process with success report", lambda: validate_adjustment_report("editor_grain", original, sample["output"], 1))
    missing_pair = Evidence()
    missing_pair.data = {}
    rejected("adjustment missing paired ingestion", missing_pair.adjustment_observations)

    # Exercise the verifier branches with explicit synthetic freshness, using
    # already-validated historical observations. This is a parser test only;
    # real current-source rejection above remains independent and mandatory.
    _, normal_rows = adjustment_extraction(ROOT / "tests/adjustment_source/after-03-release41/results.json", False)
    _, asan_rows = adjustment_extraction(ROOT / "tests/adjustment_source/after-04-asan41/results.json", False)
    primary = normal_rows["editor_grain"]
    record = dict(primary, native_test="adjustment_source.editor_grain", normal_result=primary["result"],
        dependency_group="adjustment_source", expected_pass_marker_observed=True, sanitizer=asan_rows["editor_grain"])
    synthetic = Evidence()
    synthetic.runs = {record["native_test"]: record}
    synthetic.data = {"baseline_sha": BASELINE, "schema_version": 2,
        "runtime_files": [{"path": "synthetic-parser-runtime", "sha256": "mock"}],
        "dependency_groups": {"adjustment_source": [{"path": p.relative_to(ROOT).as_posix(), "sha256": "mock"} for p in dependencies("adjustment_source")]}}
    with patch.object(synthetic, "file", return_value=True), patch.object(synthetic, "adjustment_observations", return_value=(normal_rows, asan_rows)):
        assert synthetic.result(record["native_test"])[0] == "passed"
        checks.append({"name": "synthetic paired verifier accepts validated records", "result": "passed"})
        record["sanitizer"] = dict(asan_rows["editor_grain"], source_check_executions=20)
        assert synthetic.result(record["native_test"])[0] == "failed"
        checks.append({"name": "synthetic paired verifier rejects changed ASAN multiplicity", "result": "passed"})
    synthetic.data["complete_source_contracts"] = copy.deepcopy(contracts)
    with patch.object(synthetic, "file", return_value=True):
        assert sum(row["review_valid"] for row in synthetic.complete()) == 13
        checks.append({"name": "complete review accepts exact13 mapped records", "result": "passed"})
        synthetic.data["complete_source_contracts"][0]["expected_source_check_executions"] += 1
        assert sum(row["review_valid"] for row in synthetic.complete()) == 12
        checks.append({"name": "complete review rejects altered conjunction", "result": "passed"})

    accepted("subject22 fixed acceptance membership and recipe", subject_input_contracts)
    subject_dir = SUBJECT_INPUT_ROOT / "after-03-production"
    for case in SUBJECT_INPUT_CASES:
        value = read(subject_dir / (case + ".json"))
        output = (subject_dir / (case + ".log")).read_text(encoding="utf-8")
        accepted("subject " + case + " typed report", lambda c=case, v=value, o=output: validate_subject_input_report(c, v, o, 0))
        accepted("subject " + case + " required fixture hashes", lambda c=case: subject_input_fixture(c))
    original = read(subject_dir / "identity.json")
    output = (subject_dir / "identity.log").read_text(encoding="utf-8")
    for name, key, value in (("RGB byte failure", "rgb_max_byte_difference", 1), ("differing channel", "rgb_differing_channels", 1),
            ("float tolerance failure", "tensor_max_abs", .0000011), ("nonfinite float", "tensor_max_abs", float("nan")),
            ("wrong input recipe", "mode", "before")):
        mutated = dict(original, **{key: value})
        rejected("subject " + name, lambda v=mutated: validate_subject_input_report("identity", v, output, 0))
    for case, key, value in (("maximum_axis_memory", "temporary_bytes", 1048577), ("maximum_axis_memory", "maximum_cached_rows", 60),
            ("cancel_validation", "validated_pixels", 4097), ("cancel_resample", "prepared_source_rows", 2),
            ("budget_reject", "validated_pixels", 1)):
        mutated = dict(read(subject_dir / (case + ".json")), **{key: value})
        actual_output = (subject_dir / (case + ".log")).read_text(encoding="utf-8")
        rejected("subject " + case + " " + key, lambda c=case, v=mutated, o=actual_output: validate_subject_input_report(c, v, o, 0))
    rejected("subject missing PASS marker", lambda: validate_subject_input_report("identity", original, "", 0))
    rejected("subject success with nonzero process", lambda: validate_subject_input_report("identity", original, output, 1))
    additions_rows = additions()
    assert len([r for _, r in additions_rows if r["id"].startswith("subject_input.")]) == 22
    assert len(read(ROOT / "audit/acceptance-candidates.json")["cases"]) == 313
    checks.append({"name": "canonical loader22 bounded rows; original denominator313", "result": "passed"})
    return {"schema": "NATIVE_PROOF_ADAPTER42_REVIEW_V1", "captured_utc": datetime.now(timezone.utc).isoformat(),
        "script_sha256": sha(Path(__file__)), "status": "passed", "checks": checks, "historical_configuration_observations": historical,
        "original_invocation_denominator": 313, "complete_native_candidates": 13, "required_checks_per_configuration": 179,
        "new_bounded_contracts": 22, "current_promotions": 0,
        "scope": "Parser/fixture validation only. Historical normal+ASAN41 are stale for current source. Fresh coherent paired evidence is required before any original invocation promotion."}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--verify", action="store_true")
    parser.add_argument("--capture-native", action="store_true")
    parser.add_argument("--refresh-stale", action="store_true", help="Rebuild/rerun only stale or newly mapped families; revalidate preserved runs")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--self-test-new-contracts", action="store_true", help="Read-only strict adjustment/input adapter checks; does not generate ledger or execute native tests")
    parser.add_argument("--self-test-live-mask-source", action="store_true", help="Read-only five-method LiveMask proof adapter checks")
    parser.add_argument("--self-test-source43", action="store_true", help="Read-only strict Hue12/document-size7 adapter and adversarial checks")
    parser.add_argument("--self-test-source44", action="store_true", help="Read-only Selection23/Palette3 mixed-outcome adapter checks")
    parser.add_argument("--self-test-release42-bounded", action="store_true", help="Read-only107-case typed adapter adversarial checks")
    parser.add_argument("--extract-release42-bounded", nargs=2, metavar=("FOCUSED_DIRECTORY", "NEW_OUTPUT_JSON"),
                        help="Read existing coherent reports only; write107 typed observations without updating primary proof/ledger")
    parser.add_argument("--ingest-release42-bounded", metavar="EXTRACTION_JSON", help="Validate and ingest107 bounded coherent observations; no source invocation promotions")
    parser.add_argument("--ingest-adjustment-source", nargs=2, metavar=("NORMAL_EXTRACTION", "ASAN_EXTRACTION"),
                        help="Ingest current coherent normal and ASAN13-scenario extractions; archive prior proof before updating")
    parser.add_argument("--ingest-live-mask-source", nargs=2, metavar=("NORMAL_EXTRACTION", "ASAN_EXTRACTION"),
                        help="Ingest current coherent normal and ASAN5-method LiveMask extractions with all74 dynamic source checks")
    parser.add_argument("--ingest-hue-source", nargs=2, metavar=("NORMAL_EXTRACTION", "ASAN_EXTRACTION"),
                        help="Ingest current paired Hue12 evidence with all132 checks and stable pre/post runtimes")
    parser.add_argument("--ingest-document-size-source", nargs=2, metavar=("NORMAL_EXTRACTION", "ASAN_EXTRACTION"),
                        help="Ingest current paired Canvas4/Image3 evidence with all205 checks and stable pre/post runtimes")
    parser.add_argument("--ingest-selection-source", nargs=2, metavar=("NORMAL_EXTRACTION", "ASAN_EXTRACTION"),
                        help="Ingest current paired Selection23 observations, preserving failed/unavailable original assertions")
    parser.add_argument("--ingest-palette-source", nargs=2, metavar=("NORMAL_EXTRACTION", "ASAN_EXTRACTION"),
                        help="Ingest current paired Palette3 observations with21 original checks")
    parser.add_argument("--build-dir", default="build/release")
    parser.add_argument("--config", default="Release")
    args = parser.parse_args()
    mutating = any((args.capture_native, args.ingest_adjustment_source, args.ingest_live_mask_source,
                    args.ingest_release42_bounded, args.ingest_hue_source, args.ingest_document_size_source,
                    args.ingest_selection_source, args.ingest_palette_source))
    if args.self_test_source44:
        if mutating:
            parser.error("Read-only adapter checks cannot run with evidence mutation")
        from parity_source44_selftest import run
        print(json.dumps(run(), indent=2))
        raise SystemExit(0)
    if args.self_test_source43:
        if mutating:
            parser.error("Read-only adapter checks cannot run with evidence mutation")
        from parity_source43_selftest import run
        print(json.dumps(run(), indent=2))
        raise SystemExit(0)
    if args.self_test_release42_bounded:
        if mutating:
            parser.error("Read-only adapter checks cannot run with evidence mutation")
        print(json.dumps(release42.self_test(), indent=2))
        raise SystemExit(0)
    if args.extract_release42_bounded:
        if mutating:
            parser.error("Read-only extraction cannot run with primary evidence mutation")
        directory, output = args.extract_release42_bounded
        result = release42.extract(output, directory)
        print(json.dumps(result["counts"]))
        raise SystemExit(0)
    if args.self_test_live_mask_source:
        if mutating:
            parser.error("Read-only adapter checks cannot run with evidence mutation")
        print(json.dumps(self_test_live_mask_source(), indent=2))
        raise SystemExit(0)
    if args.self_test_new_contracts:
        if mutating:
            parser.error("Read-only adapter checks cannot run with evidence mutation")
        print(json.dumps(self_test_new_contracts(), indent=2))
        raise SystemExit(0)
    if args.ingest_adjustment_source:
        ingest_adjustment_source(*args.ingest_adjustment_source)
    if args.ingest_live_mask_source:
        ingest_adjustment_source(*args.ingest_live_mask_source, family="live_mask_source")
    if args.ingest_hue_source:
        ingest_adjustment_source(*args.ingest_hue_source, family="hue_saturation_source")
    if args.ingest_document_size_source:
        ingest_adjustment_source(*args.ingest_document_size_source, family="document_size_source")
    if args.ingest_selection_source:
        ingest_adjustment_source(*args.ingest_selection_source, family="selection_source")
    if args.ingest_palette_source:
        ingest_adjustment_source(*args.ingest_palette_source, family="palette_source")
    if args.ingest_release42_bounded:
        ingest_release42_bounded(args.ingest_release42_bounded)
    failed = capture_native(args.build_dir, args.config, args.refresh_stale) if args.capture_native else False
    if args.self_test:
        self_test()
    ledger = generate()
    report(ledger)
    if args.verify:
        raise SystemExit(verify(ledger))
    print(json.dumps(totals(ledger)))
    if failed:
        raise SystemExit(1)

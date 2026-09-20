"""Build an auditable ledger; missing proof never becomes a pass."""
import argparse
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASELINE = "a19db9011282399785dc18efcfded904627bdcc2"

def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))

def generate():
    existing = read(ROOT / "parity-ledger.json") if (ROOT / "parity-ledger.json").exists() else {}
    previous = {c["id"]: c for c in existing.get("cases", [])}
    cases = []
    for row in read(ROOT.parent / "research/feature-parity-seed.json")["rows"]:
        # Stable ordered subcases retain the entire original obligation as context.
        clauses = [s.strip().rstrip(".") for s in row["acceptance"].split(";") if s.strip()]
        for i, clause in enumerate(clauses, 1):
            cases.append({"id": f"{row['id']}.{i:02}", "group": row["id"], "title": clause,
                "kind": "feature_acceptance", "upstream_evidence": row["source_files"],
                "obligation_context": row["acceptance"], "windows_implementation": [],
                "prerequisites": ["Native Windows application", "Reference fixtures where pixels or platform behavior require comparison"],
                "executable_test": None,
                "manual_steps": [f"Open the source-attributable fixture for {row['feature']}.", f"Exercise: {clause}.", "Repeat with partial alpha, mask, selection and history where applicable.", "Capture state, pixels and native interaction evidence; compare the pinned source contract."],
                "implementation_status": "not_started", "result": "unverified", "evidence_path": []})
    for candidate in read(ROOT / "audit/acceptance-candidates.json")["cases"]:
        c = dict(candidate)
        c["kind"] = "upstream_test_invocation"
        c["implementation_status"] = "not_started"
        c["result"] = "blocked_reference" if c.get("reference_status") == "blocked_reference" else "unverified"
        cases.append(c)
    controls = read(ROOT / "audit/command-inventory.json")
    for command in controls["control_sites"] + controls["event_handlers"]:
        cases.append({"id": command["id"], "kind": "control_or_event_audit",
            "title": command.get("declaration", command.get("function")), "upstream_evidence": command["source"],
            "windows_implementation": [], "prerequisites": ["Native interactive Windows session"],
            "executable_test": None, "manual_steps": ["Exercise the source control with its declared enablement and modifier rules.", "Verify text focus, undo/cancel, and equivalent Windows shortcut behavior."],
            "implementation_status": "not_started", "result": "unverified", "evidence_path": []})
    for c in cases:
        if c["id"] in previous:
            for field in ("windows_implementation", "executable_test", "implementation_status", "result", "evidence_path", "notes"):
                if field in previous[c["id"]]: c[field] = previous[c["id"]][field]
    if len({c["id"] for c in cases}) != len(cases): raise ValueError("Duplicate acceptance IDs")
    ledger = {"schema_version": 1, "baseline_sha": BASELINE,
        "reference_access": existing.get("reference_access", {"status": "blocked_reference"}),
        "denominator_status": "expanding_source_audit_not_final", "grouped_floor": 94,
        "policy": "Implemented and verified coverage are separate. A native analytic pass is not a Mac differential result. Group/control obligations require refinement into complete reproducible fixtures before closure.",
        "cases": cases}
    (ROOT / "parity-ledger.json").write_text(json.dumps(ledger, indent=2)+"\n", encoding="utf-8")
    return ledger

def report(ledger):
    cases = ledger["cases"]
    implemented = sum(c["implementation_status"] == "implemented" for c in cases)
    passed = sum(c["result"] == "passed" for c in cases)
    blocked = sum(c["result"].startswith("blocked") for c in cases)
    lines = ["# Compositor Windows parity", "", f"Pinned upstream: `{BASELINE}`.", "",
        f"The current expanding inventory contains **{len(cases)} cases** from 94 grouped obligations, the upstream test invocations, and source control/event sites. This is not a frozen or complete denominator.", "",
        f"Fully implemented cases: **{implemented}/{len(cases)}**. Fully verified parity cases: **{passed}/{len(cases)}**. Reference-blocked cases: **{blocked}**. Partial native implementations and analytic tests do not close broader parity obligations.", "",
        "Machine-readable detail: [parity-ledger.json](parity-ledger.json). Native implementation verification is recorded separately in [VALIDATION.md](VALIDATION.md).", "",
        "| ID | Requirement | Implementation | Result |", "|---|---|---|---|"]
    for c in cases: lines.append(f"| {c['id']} | {str(c['title']).replace('|','/').replace(chr(10),' ')} | {c['implementation_status']} | {c['result']} |")
    (ROOT / "PARITY.md").write_text("\n".join(lines)+"\n", encoding="utf-8")

def verify(ledger):
    failures = []
    for c in ledger["cases"]:
        if c["result"] != "passed": failures.append(c["id"])
        elif not c.get("evidence_path") or not c.get("windows_implementation"):
            raise ValueError(f"Claimed pass without implementation/evidence: {c['id']}")
    print(json.dumps({"cases":len(ledger["cases"]), "unpassed":len(failures), "denominator_status":ledger["denominator_status"], "parity_complete":False}))
    return 1 if failures or ledger["denominator_status"] != "final_source_audited" else 0

if __name__ == "__main__":
    parser = argparse.ArgumentParser(); parser.add_argument("--verify", action="store_true"); args=parser.parse_args()
    ledger=generate(); report(ledger)
    if args.verify: raise SystemExit(verify(ledger))
    print(f"Wrote {len(ledger['cases'])} acceptance records.")

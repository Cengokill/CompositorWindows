"""Run on a Mac. Creates a separate worktree; preserves the original checkout.

python3 capture_mac.py --upstream /path/to/Compositor --output /path/to/new-reference-run
No generated fixture or passing result is shipped with this preparation script.
"""
from __future__ import annotations
import argparse
import base64
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import sys

BASELINE = "a19db9011282399785dc18efcfded904627bdcc2"

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--upstream",required=True,type=Path)
    parser.add_argument("--output",required=True,type=Path)
    parser.add_argument("--prepare-only",action="store_true",help="Prepare worktree and harness; execute no Mac tests")
    args=parser.parse_args()
    if platform.system()!="Darwin": raise SystemExit("blocked_reference: macOS is required; no tests ran")
    upstream=args.upstream.resolve(); output=args.output.resolve()
    if output.exists(): raise SystemExit("Output must be a new directory; use a new run name")
    sha=subprocess.check_output(["git","-C",str(upstream),"rev-parse",BASELINE+"^{commit}"],text=True).strip()
    if sha!=BASELINE: raise SystemExit("Pinned commit is unavailable")
    output.mkdir(parents=True); logs=output/"logs"; logs.mkdir(); checkout=output/"checkout"
    results=[]
    def run(name, command, cwd=None):
        with (logs/(name+".log")).open("w",encoding="utf-8") as log:
            completed=subprocess.run(command,cwd=cwd,text=True,stdout=log,stderr=subprocess.STDOUT)
        result={"name":name,"command":command,"cwd":str(cwd) if cwd else None,"exit_code":completed.returncode,"log":"logs/"+name+".log"}
        results.append(result)
        (output/"commands.json").write_text(json.dumps(results,indent=2)+"\n")
        return completed.returncode
    if run("worktree",["git","-C",str(upstream),"worktree","add","--detach",str(checkout),BASELINE]): raise SystemExit("Worktree creation failed; see log")
    for name,cmd in [
        ("sw-vers",["sw_vers"]),("xcode-version",["xcodebuild","-version"]),
        ("sdk-list",["xcodebuild","-showsdks"]),("swift-version",["xcrun","swift","--version"]),
        ("gpu-displays",["system_profiler","SPDisplaysDataType","-json"]),
        ("schemes",["xcodebuild","-list","-json","-project","Compositor.xcodeproj"]),
        ("destinations",["xcodebuild","-showdestinations","-project","Compositor.xcodeproj","-scheme","Compositor"])]:
        run(name,cmd,checkout)
    common=["xcodebuild","-project","Compositor.xcodeproj","-scheme","Compositor","-destination","platform=macOS",
            "-derivedDataPath",str(output/"DerivedData"),"CODE_SIGNING_ALLOWED=NO"]
    if not args.prepare_only:
        run("baseline-unit",common+["-resultBundlePath",str(output/"baseline-unit.xcresult"),"-only-testing:CompositorTests","test"],checkout)
        run("baseline-ui",common+["-resultBundlePath",str(output/"baseline-ui.xcresult"),"-only-testing:CompositorUITests","test"],checkout)
    fixtures=output/"fixtures"
    harness=(Path(__file__).parent/"MacReferenceExporter.swift").read_text(encoding="utf-8")
    token=base64.b64encode(str(fixtures).encode()).decode()
    harness=harness.replace("__OUTPUT_PATH_BASE64__",token)
    (checkout/"CompositorTests"/"MacReferenceExporter.swift").write_text(harness,encoding="utf-8")
    run("instrumentation-diff",["git","diff","--exit-code"],checkout)
    run("instrumentation-status",["git","status","--short"],checkout)
    if not args.prepare_only:
        run("fixture-export",common+["-resultBundlePath",str(output/"fixture-export.xcresult"),"-only-testing:CompositorTests/MacReferenceExporter/exportAll","test"],checkout)
        # Preserve raw tool output; xcresulttool schemas can vary with installed Xcode.
        for name in ["baseline-unit","baseline-ui","fixture-export"]:
            bundle=output/(name+".xcresult")
            if bundle.exists():
                run(name+"-summary",["xcrun","xcresulttool","get","test-results","summary","--path",str(bundle)])
                run(name+"-tests",["xcrun","xcresulttool","get","test-results","tests","--path",str(bundle)])
    inventory=[]
    if fixtures.exists():
        for path in sorted(fixtures.rglob("*")):
            if path.is_file(): inventory.append({"path":path.relative_to(output).as_posix(),"bytes":path.stat().st_size,"sha256":hashlib.sha256(path.read_bytes()).hexdigest()})
    (output/"file-manifest.json").write_text(json.dumps(inventory,indent=2)+"\n")
    completed={r["name"]:r["exit_code"] for r in results}
    status={"baseline_sha":BASELINE,"reference_status":"prepared_not_run" if args.prepare_only else "requires_xcresult_review",
            "unit_exit_code":completed.get("baseline-unit"),"ui_exit_code":completed.get("baseline-ui"),
            "export_exit_code":completed.get("fixture-export"),"fixture_file_count":len(inventory),
            "actual_unit_invocations":None,"actual_ui_invocations":None,
            "required_review":"Inspect actual test cases/failures and invocation counts in xcresult, including parameterized and UI configuration variants. No count is inferred from source declarations.",
            "worktree":str(checkout),"cleanup":"Retain this evidence; later remove checkout using git worktree remove only when authorized."}
    (output/"run-status.json").write_text(json.dumps(status,indent=2)+"\n")
    print(json.dumps(status,indent=2))
    if not args.prepare_only and any(completed.get(n)!=0 for n in ["baseline-unit","baseline-ui","fixture-export"]): return 1
    return 0

if __name__=="__main__": sys.exit(main())

"""Deterministic, source-only upstream audit. Does not execute or port Swift tests."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

BASELINE = "a19db9011282399785dc18efcfded904627bdcc2"
ROOT = Path(__file__).resolve().parents[2]
UPSTREAM = ROOT / "upstream"
OUT = Path(__file__).resolve().parent

def digest(text):
    return hashlib.sha256(text.encode()).hexdigest()

def masked(s):
    """Blank comments/strings, preserving offsets/newlines for balanced delimiters."""
    out = list(s)
    i = 0
    while i < len(s):
        end = None
        if s.startswith("//", i):
            end = s.find("\n", i)
            if end < 0: end = len(s)
        elif s.startswith("/*", i):
            depth, p = 1, i + 2
            while p < len(s) and depth:
                if s.startswith("/*", p): depth += 1; p += 2
                elif s.startswith("*/", p): depth -= 1; p += 2
                else: p += 1
            end = p
        elif s[i] == '"':
            delim = '"""' if s.startswith('"""', i) else '"'
            p = i + len(delim)
            while p < len(s):
                if s[p] == "\\": p += 2
                elif s.startswith(delim, p): p += len(delim); break
                else: p += 1
            end = p
        if end is not None:
            for p in range(i, end):
                if s[p] not in "\r\n": out[p] = " "
            i = end
        else: i += 1
    return "".join(out)

def closing(m, start, left="(", right=")"):
    assert m[start] == left, (start, m[start:start+20])
    depth = 1
    for i in range(start+1, len(m)):
        if m[i] == left: depth += 1
        elif m[i] == right:
            depth -= 1
            if depth == 0: return i
    raise ValueError(f"Unclosed {left} at {start}")

def line(s, i): return s.count("\n", 0, i) + 1
def source(path, s, start, end):
    rel = path.relative_to(UPSTREAM).as_posix()
    return {"path": rel, "line": line(s, start), "end_line": line(s, end),
            "url": f"https://github.com/robbietilton/Compositor/blob/{BASELINE}/{rel}#L{line(s,start)}"}

PARAMETERS = {
    "AdjustmentKind.allCases": ["hsv", "levels", "curves", "exposure", "gradientMap", "grain"],
    "SpotHealingMode.allCases": ["contentAware", "createTexture", "proximityMatch"],
    "[CGFloat(1), CGFloat(2)]": [1, 2],
    "[UTType.png, .jpeg, .tiff, .heic]": ["png", "jpeg", "tiff", "heic"],
    "[(0.2, 0.0), (0.7, 0.0), (0.3, 25.0)]": [[0.2, 0.0], [0.7, 0.0], [0.3, 25.0]],
    "[10.749, 1.0]": [10.749, 1.0],
    "[(0.3, 0.0), (0.7, 25.0)]": [[0.3, 0.0], [0.7, 25.0]],
}

def assertions(path, s, m, start, end):
    found=[]
    pattern = r"#(?:expect|require)\s*\(|\bXCTAssert\w*\s*\(|\bXCTFail\s*\(|\bIssue\.record\s*\("
    for match in re.finditer(pattern, m[start:end]):
        a = start + match.start(); p = m.index("(", a); b = closing(m, p)
        tail=b+1
        while tail<len(m) and m[tail].isspace(): tail+=1
        closure_end=closing(m,tail,"{","}") if tail<len(m) and m[tail]=="{" else None
        found.append({"kind": s[a:p].strip(), "expression": s[p+1:b].strip(),
                      "trailing_closure":s[tail:closure_end+1] if closure_end is not None else None,
                      "source": source(path,s,a,closure_end if closure_end is not None else b)})
    return found

def test_inventory():
    records=[]; files=[]; all_assertions=[]
    for folder, kind in [("CompositorTests","unit"),("CompositorUITests","ui")]:
        for path in sorted((UPSTREAM/folder).glob("*.swift")):
            s=path.read_text(encoding="utf-8-sig"); m=masked(s)
            files.append({"path":path.relative_to(UPSTREAM).as_posix(), "sha256":hashlib.sha256(path.read_bytes()).hexdigest(),
                          "line_count":len(s.splitlines()),"kind":kind})
            all_assertions += assertions(path,s,m,0,len(s))
            scopes=[]
            for declaration in re.finditer(r"\b(?:struct|class|enum)\s+(\w+)\b",m):
                start=m.index("{",declaration.end())
                scopes.append((start,closing(m,start,"{","}"),declaration.group(1)))
            matches=list(re.finditer(r"@Test\b" if kind=="unit" else r"\bfunc\s+(test\w+)\s*\(",m))
            for match in matches:
                attribute_end=match.end(); attribute="@Test"; arguments=None
                if kind=="unit":
                    p=attribute_end
                    while m[p].isspace(): p+=1
                    if m[p]=="(": attribute_end=closing(m,p)+1; attribute=s[match.start():attribute_end]
                    f=re.search(r"\bfunc\s+(\w+)\s*\(",m[attribute_end:])
                    assert f
                    func_start=attribute_end+f.start(); name=f.group(1)
                    if "arguments:" in attribute:
                        arguments=attribute.split("arguments:",1)[1].rsplit(")",1)[0].strip()
                        assert arguments in PARAMETERS, arguments
                else: func_start=match.start(); name=match.group(1)
                body_start=m.index("{",func_start); body_end=closing(m,body_start,"{","}")
                suite=min((entry for entry in scopes if entry[0]<func_start<entry[1]),key=lambda entry:entry[1]-entry[0])[2]
                src=source(path,s,match.start(),body_end)
                direct=assertions(path,s,m,body_start,body_end)
                test_id=f"{'UT' if kind=='unit' else 'UI'}-{path.stem}-{name}"
                body=s[body_start+1:body_end]
                prerequisites=["Pinned macOS test host (macOS 26.5+ and compatible Xcode SDK)"]
                environment_gates=[{"variable":a,"required_value":b,"otherwise":"Function returns before behavioral work"}
                    for a,b in re.findall(r'guard\s+ProcessInfo\.processInfo\.environment\["([^"]+)"\]\s*==\s*"([^"]+)"\s*else\s*\{\s*return\s*\}',body)]
                for gate in environment_gates:
                    prerequisites.append(f"Test host environment {gate['variable']}={gate['required_value']}; otherwise an invoked test returns without exercising its behavior")
                if kind=="ui" or re.search(r"NSWindow|NSApplication|NSApp|CanvasView|FloatingPanel|NSEvent",s):
                    prerequisites += ["Logged-in interactive macOS graphics session; AppKit event/window availability"]
                if "SubjectRemoval" in body: prerequisites += ["Apple Vision foreground instance request support"]
                vals=PARAMETERS[arguments] if arguments else [None]
                records.append({"id":test_id,"kind":kind,"suite":suite,"function":name,
                    "source":src,"source_body_sha256":digest(body),"attribute":attribute if kind=="unit" else None,
                    "parameterization":{"expression":arguments,"static_values":vals if arguments else None,
                      "expected_invocations":len(vals),"actual_invocations":None,
                      "caveat":"XCTest UI configurations may repeat this method" if name=="testLaunch" else None},
                    "direct_assertion_sites":direct,"direct_expectation_site_count":sum(a["kind"]!="#require" for a in direct),
                    "direct_requirement_site_count":sum(a["kind"]=="#require" for a in direct),
                    "assertion_count_semantics":"Static sites in function body, including local closures; excludes file helpers. Loops and parameterization can execute sites repeatedly.",
                    "helper_context":path.relative_to(UPSTREAM).as_posix(),
                    "environment_gates":environment_gates,
                    "prerequisites":prerequisites,"windows_implementation":None,"windows_test":None,
                    "port_status":"unported","reference_status":"blocked_reference","result":"not_run",
                    "evidence_path":None,
                    "reproduction":{"upstream_command":f"xcodebuild -project Compositor.xcodeproj -scheme Compositor -destination 'platform=macOS' -only-testing:{folder}/{suite}/{name} test",
                                    "steps":[f"Reproduce setup and operations in {src['path']}:{src['line']}–{src['end_line']}",
                                             "Port every expectation and required precondition, retaining helper semantics and original tolerances", "Run Windows test; record actual parameter invocations and evidence", "Run unchanged Mac baseline and compare applicable outputs"]}})
    assert sum(r["kind"]=="unit" for r in records)==288
    assert sum(r["kind"]=="ui" for r in records)==3
    assert len(set(r["id"] for r in records))==291
    assert sum(f["kind"]=="unit" for f in files)==47
    return {"schema_version":1,"baseline_sha":BASELINE,"basis":"Static source inventory, not runtime results", "counts":{
        "unit_files":47,"unit_functions":288,"ui_files":2,"ui_methods":3,
        "parameterized_unit_functions":sum(r["kind"]=="unit" and r["parameterization"]["expression"] is not None for r in records),
        "expected_unit_invocations":sum(r["parameterization"]["expected_invocations"] for r in records if r["kind"]=="unit"),
        "actual_unit_invocations":None,"actual_ui_invocations":None,
        "all_static_assertion_sites_including_helpers":len(all_assertions),
        "function_body_static_assertion_sites":sum(len(r["direct_assertion_sites"]) for r in records)},
        "files":files,"tests":records,"all_file_assertion_sites":all_assertions}

def command_inventory():
    sites=[]; events=[]; enums=[]
    production=UPSTREAM/"Compositor"
    for path in sorted(production.rglob("*.swift")):
        s=path.read_text(encoding="utf-8-sig"); m=masked(s)
        for hit in re.finditer(r"\b(Button|Toggle|Picker|Slider|TextField|Menu|CommandMenu|CommandGroup)\s*\(",m):
            a=hit.start(); p=m.index("(",a); b=closing(m,p)
            tail=b+1
            while tail<len(m) and m[tail].isspace(): tail+=1
            if tail<len(m) and m[tail]=="{": b=closing(m,tail,"{","}")
            # Capture each consecutive modifier, including multi-line arguments and closures.
            end=b+1
            while True:
                mod=re.match(r"\s*\.\w+\s*\(",m[end:])
                if not mod: break
                q=end+mod.end()-1; end=closing(m,q)+1
            snippet=s[a:end]
            sites.append({"id":"CMD-"+digest(path.relative_to(UPSTREAM).as_posix()+str(line(s,a))+snippet)[:16],
                "kind":hit.group(1),"source":source(path,s,a,end),"declaration":s[a:closing(m,p)+1],
                "source_excerpt":snippet,"keyboard_shortcuts":re.findall(r"\.keyboardShortcut\(([^\n]*)",snippet),
                "expansion_required":"ForEach" in snippet or '\\(' in s[a:p+1]+s[p+1:closing(m,p)],
                "windows_command":None,"result":"unported"})
        for hit in re.finditer(r"\b(Button|Toggle|Picker|Slider|TextField)\s*\{",m):
            a=hit.start(); p=m.index("{",a); end=closing(m,p,"{","}")+1
            label=re.match(r"\s*label\s*:\s*\{",m[end:])
            if label:
                q=end+label.end()-1; end=closing(m,q,"{","}")+1
            while True:
                mod=re.match(r"\s*\.\w+\s*\(",m[end:])
                if not mod: break
                q=end+mod.end()-1; end=closing(m,q)+1
            snippet=s[a:end]
            sites.append({"id":"CMD-"+digest(path.relative_to(UPSTREAM).as_posix()+str(line(s,a))+snippet)[:16],
                "kind":hit.group(1),"source":source(path,s,a,end),"declaration":hit.group(1)+" { action } label: { view }",
                "source_excerpt":snippet,"keyboard_shortcuts":re.findall(r"\.keyboardShortcut\(([^\n]*)",snippet),
                "expansion_required":True,"windows_command":None,"result":"unported"})
        for hit in re.finditer(r"\bfunc\s+(keyDown|keyUp|performKeyEquivalent|flagsChanged|scrollWheel|mouseDown|mouseDragged|mouseUp|rightMouseDown|magnify)\s*\(",m):
            a=hit.start(); b=m.index("{",a); end=closing(m,b,"{","}")
            events.append({"id":"EVT-"+digest(path.relative_to(UPSTREAM).as_posix()+str(line(s,a)))[:16],
                "function":hit.group(1),"source":source(path,s,a,end),"source_excerpt":s[a:end+1],"result":"unported"})
        for hit in re.finditer(r"\benum\s+(NavigationTool|LayerBlendMode|AdjustmentKind|FilterKind|SpotHealingMode|BrushMode|SmearMode|GradientKind|ShapeKind|LayerSampling|ColorRange|LevelsChannel|LevelsAutoMethod|LassoKind|MarqueeKind)\b",m):
            a=hit.start(); b=m.index("{",a); end=closing(m,b,"{","}")
            cases=[{"source":source(path,s,c.start(),s.find("\n",c.start())),"declaration":s[c.start():s.find("\n",c.start())].strip()}
                   for c in re.finditer(r"(?m)^\s*case\s+[^.\s]",m[b+1:end])]
            # Cases are retained by exact declaration from the initial enum region, before members.
            head=re.split(r"\b(?:var|func|static)\s",s[b+1:end],maxsplit=1)[0]
            enums.append({"name":hit.group(1),"source":source(path,s,a,end),"case_declarations":head.strip()})
    return {"schema_version":1,"baseline_sha":BASELINE,"status":"candidate_inventory_requires_dispatch_review",
            "limits":"Sites include nested UI containers and dynamically generated controls. This is a source-attributed floor; no completeness or implementation claim. Event handler bodies retain modifier/IME/focus conditions for manual expansion.",
            "control_site_count":len(sites),"event_handler_count":len(events),"control_sites":sites,"event_handlers":events,"enumerated_modes":enums}

def acceptance_candidates(inventory,commands):
    cases=[]
    for r in inventory["tests"]:
        values=r["parameterization"]["static_values"] or [None]
        for index,value in enumerate(values):
            cases.append({"id":"ACC-"+r["id"]+(f"-arg{index+1:02}" if value is not None else ""),
                "title":r["function"],"upstream_test_id":r["id"],"argument":value,"upstream_evidence":r["source"],
                "assertions":r["direct_assertion_sites"],"windows_implementation":None,"prerequisites":r["prerequisites"],
                "executable_test":None,"reproduction":r["reproduction"],"implemented":False,"verified":False,
                "result":"unported","reference_status":"blocked_reference","evidence_path":None})
    return {"schema_version":1,"baseline_sha":BASELINE,"scope":"Upstream test invocation candidates; production/source-only obligations must also be integrated", "count":len(cases),"cases":cases}

def main():
    parser=argparse.ArgumentParser(); parser.add_argument("--check",action="store_true"); args=parser.parse_args()
    sha=subprocess.check_output(["git","-c",f"safe.directory={UPSTREAM.as_posix()}","-C",str(UPSTREAM),"rev-parse","HEAD"],text=True).strip()
    if sha!=BASELINE: raise SystemExit(f"Wrong baseline: {sha}")
    inv=test_inventory(); commands=command_inventory()
    artifacts={"upstream-test-map.json":inv,"command-inventory.json":commands,"acceptance-candidates.json":acceptance_candidates(inv,commands)}
    for name,obj in artifacts.items():
        rendered=json.dumps(obj,indent=2,ensure_ascii=False)+"\n"
        target=OUT/name
        if args.check:
            if not target.exists() or target.read_text(encoding="utf-8")!=rendered: raise SystemExit(f"Stale audit artifact: {target}")
        else: target.write_text(rendered,encoding="utf-8")
    print(json.dumps({"baseline_sha":sha,"counts":inv["counts"],"candidate_count":artifacts["acceptance-candidates.json"]["count"],"control_sites":commands["control_site_count"],"event_handlers":commands["event_handler_count"],"check":args.check}))

if __name__=="__main__": main()

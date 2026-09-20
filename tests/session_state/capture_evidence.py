from pathlib import Path
import hashlib
import json
import xml.etree.ElementTree as ET

here=Path(__file__).resolve().parent
root=here.parents[2]
suite=ET.parse(here/'results.xml').getroot()
files=['windows/src/ui/ProjectToolState.h','windows/src/ui/ProjectToolState.cpp','windows/src/ui/ProjectActions.cpp','windows/tests/session_state/ProjectToolStateTests.cpp','windows/build/release/Release/session_state_tests.exe','upstream/Compositor/Document/EditorSession.swift','upstream/Compositor/Document/ProjectWorkspace.swift','upstream/CompositorTests/CloneStampTests.swift']
report={
    'schema_version':1,'baseline_sha':'a19db9011282399785dc18efcfded904627bdcc2',
    'native_status':'passed','mac_reference_status':'blocked_reference','desktop_visual_acceptance':False,
    'build_log':'windows/evidence/integration/release-build-15.log',
    'run_log':'windows/tests/session_state/run-1.log','junit':'windows/tests/session_state/results.xml',
    'command':". windows/scripts/bootstrap.ps1 -Offline; ctest --test-dir windows/build/release -C Release -R '^session_state\\.' --output-on-failure --output-junit <absolute-output-path>",
    'passed':int(suite.get('tests'))-int(suite.get('failures')),'failed':int(suite.get('failures')),
    'native_cases':[case.attrib for case in suite.findall('testcase')],
    'stored_property_inventory':'windows/tests/session_state/source-inventory.json','stored_properties_audited':84,
    'full_upstream_test_methods_ported':0,
    'complete_assertion_equivalence_candidates':[{
        'upstream_file':'CompositorTests/CloneStampTests.swift','method':'cloneStampKeepsItsOwnSoftBrushTip','method_line':63,
        'native_file':'windows/tests/session_state/ProjectToolStateTests.cpp','native_case':'brush_family_tips','native_line':34,
        'source_assertions':[{'line':68,'expected':'Brush hardness == 1','native_witness':'Shared SpotHealing hardness control == 100 percent after Brush diameter30'},
            {'line':70,'expected':'Clone hardness == 0 and diameter == 40','native_witness':'Clone control hardness0 and diameter40'},
            {'line':74,'expected':'SpotHealing hardness == 1 and diameter == 30','native_witness':'SpotHealing control hardness100 and diameter30'},
            {'line':76,'expected':'Clone hardness == 0.5 and diameter == 40','native_witness':'Clone control hardness50 and diameter40'}],
        'representation_note':'Native dedicated tip families are read through active Qt controls instead of Swift single active brushSettings; first check selects SpotHealing (same family) solely to expose hardness. Source setup creates40x20; native fixture creates40x20. Additional tab-isolation assertions are native regression coverage.',
        'review_status':'candidate_for_assertion_coverage_audit_not_automatically_credited'}],
    'remaining_choices':['cropRatioChoice','lassoKind remembered separately from active tool','filterSettings remembered values','selectionExpandAmount','selectionContractAmount','brushMode parked rail icon'],
    'known_lifecycle_difference':'Native tab switch cancels a pointer stroke; source canSwitch blocks while brush/warp exists.',
    'sha256':{path:hashlib.sha256((root/path).read_bytes()).hexdigest() for path in files},
}
assert report['passed']==5 and report['failed']==0
(here/'evidence.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'native_passed':report['passed'],'source_properties':84,'upstream_full_method_claims':0}))

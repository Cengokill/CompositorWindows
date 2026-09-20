"""Capture bounded command predicates/Qt tests without inferring desktop parity."""
from pathlib import Path
import hashlib
import json
import xml.etree.ElementTree as ET

here=Path(__file__).resolve().parent
root=here.parents[2]
cases=ET.parse(here/'results.xml').getroot().findall('.//testcase')
catalog=json.loads((here/'command-catalog.json').read_text())
files=['windows/src/ui/CommandRegistry.h','windows/src/ui/CommandRegistry.cpp',
       'windows/tests/command_ui/CommandRegistryTests.cpp',
       'windows/tests/command_ui/build/Release/command_ui_tests.exe',
       'upstream/Compositor/CompositorApp.swift','upstream/Compositor/Document/EditorSession.swift',
       'upstream/Compositor/Document/ProjectWorkspace.swift','upstream/Compositor/Document/SelectionClipboard.swift',
       'upstream/Compositor/Document/SelectionEdits.swift','upstream/Compositor/Document/HueSaturation.swift']
evidence={
 'schema_version':1,'baseline_sha':'a19db9011282399785dc18efcfded904627bdcc2',
 'status':'native_checks_passed','overall_parity_status':'fail','mac_reference_status':'blocked_reference',
 'command':'& windows/tests/command_ui/run.ps1','build_log':'windows/tests/command_ui/build-run-5.log',
 'junit':'windows/tests/command_ui/results.xml','catalog_entries':len(catalog),
 'passed':sum(c.find('failure') is None and c.get('status')=='run' for c in cases),
 'failed':sum(c.find('failure') is not None for c in cases),'tests':[c.attrib for c in cases],
 'configuration':'Release x64 MSVC19.44.35221 /O2 /W4 /WX Qt6.8.3 offscreen SDK10.0.26100.0',
 'full_upstream_methods_ported':0,
 'assertion_witnesses':[
  {'source':'CompositorTests/LevelsTests.swift:38','native':'source_predicates','assertions':'levels blocks canEditLayers,canUseHistory,canStartProjectOperation','complete_method':False},
  {'source':'CompositorTests/SelectionEditTests.swift:60','native':'layer_eligibility','assertions':'explicitly empty selection blocks canPaint and canEditPixels','complete_method':False}],
 'retained_failure':{'log':'windows/tests/command_ui/build-run-1.log','case':'text_menu_routes','cause':'empty initial Qt clipboard mimeData was dereferenced','fix':'guard null mimeData before text Paste eligibility'},
 'limits':['Standalone registry/QAction/native text widgets; actual MainWindow integration is separately root-tested',
           'Catalog covers implemented global commands, not every context-menu or options widget',
           'Source state helpers preserve exact listed Swift predicates; command gates add native modal/import safety',
           'ProjectOperation preparation is host-owned: cancel crop and commit transform only; save/export must use canonical pre-gradient state',
           'M/L source-test conflict remains unresolved; registration must not invent toggling behavior'],
 'sha256':{p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in files}}
assert len(cases)==9 and evidence['failed']==0
(here/'evidence.json').write_text(json.dumps(evidence,indent=2)+'\n')
print(json.dumps({k:evidence[k] for k in ['catalog_entries','passed','failed','overall_parity_status']}))

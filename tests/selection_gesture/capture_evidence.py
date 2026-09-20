"""Capture independent helper results and preserved native event failures."""
from pathlib import Path
import hashlib
import json
import re
import xml.etree.ElementTree as ET

here = Path(__file__).resolve().parent
root = here.parents[2]
pin = 'a19db9011282399785dc18efcfded904627bdcc2'
cases = ET.parse(here / 'results.xml').getroot().findall('.//testcase')
assert len(cases) == 15 and all(c.find('failure') is None for c in cases)
source = root / 'upstream/CompositorTests/SelectionTests.swift'
lines = source.read_text(encoding='utf-8').splitlines()
methods = {}
starts = [(i, re.search(r'@Test func (\w+)', line).group(1)) for i, line in enumerate(lines) if '@Test func ' in line]
for index, (start, name) in enumerate(starts):
    end = starts[index + 1][0] if index + 1 < len(starts) else len(lines)
    methods[name] = {'line': start + 1, 'assertions': [{'line': i + 1, 'expression': lines[i].strip()} for i in range(start, end) if '#expect(' in lines[i]]}
mapping = {'polygonalCornersCanBeRemovedAndClosed': ('polygon_corners', True),
           'clickDeselectsAndSelectionStepsUndo': ('click_history', True),
           'marqueeDrawsWholePixelRectanglesInAnyDirection': ('marquee_rounding', True),
           'marqueeShiftMakesSquaresAndCenteredDragsGrowFromTheAnchor': ('centered_programmatic', False),
           'marqueeEllipseSelectsAnOvalInItsBoxAndShiftMakesACircle': ('ellipse', False),
           'optionDraggingTheMarqueeSubtractsWithoutDrawingFromTheCenter': ('option_marquee', False),
           'shiftStartsAnAddAndOnlyAFreshShiftSquaresTheMarquee': ('fresh_shift', False)}
assertmap = {'schema_version': 1, 'baseline_sha': pin, 'source': source.relative_to(root).as_posix(),
             'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(), 'methods': []}
for name, (case, complete) in mapping.items():
    assertmap['methods'].append({'upstream_method': name, **methods[name], 'native_case': 'selection_gesture.' + case,
       'native_result': 'pass', 'full_logical_contract_candidate': complete,
       'scope': 'Native draft plus explicit100x100 selection/history fixture; eligibility and canvas host remain independently tested' if complete else
       'Bounded assertions: NavigationTool predicate/marquee toggle remain outside helper; Option/Shift actual UI event before-tests still fail pending host integration'})
assertmap['native_test_sha256'] = hashlib.sha256((here / 'SelectionGestureTests.cpp').read_bytes()).hexdigest()
assertmap['native_executable_sha256'] = hashlib.sha256((here / 'build/Release/selection_gesture_tests.exe').read_bytes()).hexdigest()
assertmap['native_runtime'] = 'windows/tests/selection_gesture/build-run-3.log'
assertmap['helper_correspondences'] = [
    {'source_line': 23, 'source_require': 'session.document', 'native': 'Session::current/coverage require optional Document; constructor validateDocument on100x100 blank-layer fixture'},
    {'source_line': 24, 'source_require': 'session.selection', 'native': 'Session::coverage requires selection and valid immutable gray coverage'},
    {'source_line': 25, 'source_require': 'CGContext gray coverage reader', 'native': 'production SelectionOutline::rasterize uses checked D2D software/WIC rendering; reader requires100x100 GrayRaster storage'},
    {'source_line': 28, 'source_require': 'context.data', 'native': 'Session::coverage requires10000 bytes and nonnull gray data pointer'}]
assertmap['complete_candidate_coverage_helper_calls'] = {'polygonalCornersCanBeRemovedAndClosed': 2, 'clickDeselectsAndSelectionStepsUndo': 1, 'marqueeDrawsWholePixelRectanglesInAnyDirection': 6}
assertmap['complete_candidate_helper_require_invocations'] = 36
(here / 'source-assert-map.json').write_text(json.dumps(assertmap, indent=2) + '\n', encoding='utf-8')
paths = ['windows/src/editing/SelectionGesture.h', 'windows/src/editing/SelectionGesture.cpp',
         'windows/src/editing/Selection.h', 'windows/src/editing/Selection.cpp', 'windows/src/core/Document.h', 'windows/src/core/Document.cpp',
         'windows/tests/selection_gesture/SelectionGestureTests.cpp', 'windows/tests/selection_gesture/SelectionEventTests.cpp',
         'windows/tests/selection_gesture/build/Release/selection_gesture_tests.exe', 'windows/tests/selection_gesture/selection_events_before.exe',
         'windows/tests/selection_gesture/build-run-3.log', 'windows/tests/selection_gesture/before-run-1.log',
         'windows/tests/selection_gesture/results.xml', 'windows/tests/selection_gesture/integrated-build25.json', 'windows/tests/selection_gesture/SelectionFollowupTests.cpp', 'windows/tests/selection_gesture/followup-before-2.log', 'windows/tests/selection_gesture/selection_followup_before.exe', 'windows/tests/selection_gesture/source-assert-map.json',
         'upstream/Compositor/Document/Selection.swift', 'upstream/Compositor/Rendering/EditorCanvas.swift',
         'upstream/Compositor/Rendering/TransformOverlay.swift', source.relative_to(root).as_posix()]
before = (here / 'before-run-1.log').read_text(encoding='utf-8')
assert all('EXIT ' + case + ' 1' in before for case in ['option_marquee', 'fresh_shift_marquee', 'polygon_click_close', 'canonical_draft'])
evidence = {'schema_version': 1, 'baseline_sha': pin, 'helper_status': '15_passed', 'before_ui_status': '4_failed', 'after_ui_status': '15_build25_actual_event_cases_passed',
    'overall_parity_status': 'fail', 'mac_reference_status': 'blocked_reference',
    'helper_command': '& windows/tests/selection_gesture/run.ps1', 'before_command': '& windows/tests/selection_gesture/capture_before.ps1',
    'configuration': 'Release MSVC19.44 x64 /O2 /W4 /WX; helper D2D software, before UI Qt6.8.3 offscreen actual events',
    'tests': [c.attrib for c in cases], 'full_logical_contract_candidates': 3,
    'sha256': {p: hashlib.sha256((root / p).read_bytes()).hexdigest() for p in paths},
    'limits': ['Before executable is preserved; its root libraries were linked without rebuilding and are not represented by later source hashes',
               'Integrated build25 independently passes15 actual event cases; original and review failures remain preserved in before executables/logs',
               'Geometric equality uses exact bounds,AA and D2D XOR at0.25-source-pixel tolerance; CGPath structural identity remains different',
               'Source coverage helpers use CG; native fixture uses D2D and exact listed logical/sample assertions only']}
(here / 'evidence.json').write_text(json.dumps(evidence, indent=2) + '\n', encoding='utf-8')
print(json.dumps({k: evidence[k] for k in ['helper_status', 'before_ui_status', 'after_ui_status', 'overall_parity_status']}))

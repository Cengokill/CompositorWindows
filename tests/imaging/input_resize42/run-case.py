"""One coherent-build CTest case; fixtures must already be frozen and complete."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import subprocess
import time
import uuid

BASE=Path(__file__).resolve().parent
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
parser=argparse.ArgumentParser()
parser.add_argument('--executable',required=True,type=Path)
parser.add_argument('--case',required=True)
parser.add_argument('--fixtures',required=True,type=Path)
parser.add_argument('--output-root',required=True,type=Path)
args=parser.parse_args()
protocol=json.loads((BASE/'protocol.json').read_text())
manifest=json.loads((args.fixtures/'manifest.json').read_text())
assert manifest['protocol_sha256']==sha(BASE/'protocol.json')
assert manifest['generator_sha256']==sha(BASE/'generate_fixtures.py')
assert [r['id'] for r in manifest['fixtures']]==[r['id'] for r in protocol['fixtures']]
kind='conformance' if args.case in [r['id'] for r in protocol['fixtures']] else 'resource'
assert kind=='conformance' or args.case in protocol['resource_cases']
fixture=args.case if kind=='conformance' else ('thin_tall' if args.case=='maximum_axis_memory' else
                ('padded_stride' if args.case=='source_unchanged' else 'identity'))
for row in manifest['fixtures']:
    for item in row['files']:
        path=args.fixtures/item['path']
        assert path.stat().st_size==item['bytes'],('Missing/incorrect required fixture',path)
        if row['id']==fixture:assert sha(path)==item['sha256'],('Fixture hash differs',path)
meta=json.loads((args.fixtures/fixture/'fixture.json').read_text())
output=args.output_root/(datetime.datetime.now(datetime.UTC).strftime('%Y%m%d-%H%M%S')+'-'+uuid.uuid4().hex[:10])
output.mkdir(parents=True,exist_ok=False)
executable=args.executable.resolve()
before=sha(executable)
command=[str(executable),'candidate',str((args.fixtures/fixture).resolve()),str(meta['width']),str(meta['height']),
         str(meta['stride']),args.case,str((output/'result.json').resolve()),kind]
run=None
launch_attempts=[]
for attempt in range(5):
    try:
        run=subprocess.run(command,capture_output=True,text=True,timeout=120)
        launch_attempts.append({'attempt':attempt+1,'status':'started','returncode':run.returncode})
        break
    except PermissionError as error:
        record={'attempt':attempt+1,'status':'permission_denied','error':str(error)}
        launch_attempts.append(record)
        print(f"Launch attempt {attempt+1}/5 failed: {error}",flush=True)
        if attempt==4:
            (output/'launch-attempts.json').write_text(json.dumps(launch_attempts,indent=2)+'\n')
            raise
        time.sleep(2)
(output/'launch-attempts.json').write_text(json.dumps(launch_attempts,indent=2)+'\n')
trace=''.join(f"Launch attempt {item['attempt']}/5 failed: {item['error']}\n"
              for item in launch_attempts if item['status']=='permission_denied')
(output/'stdout.log').write_text(trace+(run.stdout+run.stderr if run else ''))
print(trace,end='')
print(run.stdout,end='');print(run.stderr,end='')
assert run.returncode in [0,1],('Abnormal process exit',run.returncode)
report=json.loads((output/'result.json').read_text())
assert report['schema']=='SUBJECT_INPUT_RESULT_V1' and report['case']==args.case and report['mode']=='candidate'
assert (report['status']=='passed')==(run.returncode==0)
assert sha(executable)==before
(output/'provenance.json').write_text(json.dumps({'argv':command,'exit_code':run.returncode,
    'binary_sha256':before,'protocol_sha256':sha(BASE/'protocol.json'),
    'fixture_manifest_sha256':sha(args.fixtures/'manifest.json'),
    'report_sha256':sha(output/'result.json'),
    'launch_attempts':launch_attempts},indent=2)+'\n')
print('SUBJECT_INPUT_REPORT',str((output/'result.json').resolve()))
raise SystemExit(run.returncode)

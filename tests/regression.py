"""Run OCR against local, manually annotated fixtures; never embedded in the SDK."""
from pathlib import Path
import subprocess,json,time
root=Path(__file__).resolve().parents[1]
folder=root/'out/testdata'
rows=[];failures=[]
def run(path,kind,side):
    start=time.monotonic()
    process=subprocess.run([str(root/'out/build/native/cardlens-ocr'),str(root/'models'),str(path),kind,side],capture_output=True,text=True)
    if process.returncode:raise RuntimeError(process.stderr)
    return json.loads(process.stdout),time.monotonic()-start
for case in json.loads((root/'tests/gold.json').read_text(encoding='utf-8'))['cases']:
    result,duration=run(folder/case['file'],case['type'],case['side'])
    mismatches={key:{'expected':value,'actual':result['fields'].get(key,{}).get('value')} for key,value in case['expected'].items() if result['fields'].get(key,{}).get('value')!=value}
    rows.append({'image':case['file'],'seconds':duration,'expected_fields':len(case['expected']),'mismatches':mismatches,'warnings':result['warnings']})
    if mismatches:failures.append(case['file'])
    (root/'out/results'/case['file'].replace('.jpg','.json')).write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
(root/'out/validation/field-regression.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2)+'\n')
print(json.dumps(rows,ensure_ascii=False,indent=2))
if failures:raise SystemExit('Field regression failed: '+','.join(failures))

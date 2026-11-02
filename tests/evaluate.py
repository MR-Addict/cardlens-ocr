"""Evaluate annotated images by split, with cold/warm latency and optional exhaustive comparison."""
import argparse
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'sdk/python'))
from cardlens_ocr import ocr


def percentile(values, fraction):
    values = sorted(values)
    return values[max(0, math.ceil(len(values) * fraction) - 1)] if values else None


def peak_memory():
    try:
        import resource
        value = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
        return value if sys.platform == 'darwin' else value * 1024
    except ImportError:
        return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, default=ROOT / 'tests/gold.json')
    parser.add_argument('--images', type=Path, default=ROOT / 'out/testdata')
    parser.add_argument('--repeat', type=int, default=2)
    parser.add_argument('--compare', action='store_true')
    parser.add_argument('--output', type=Path, default=ROOT / 'out/validation/evaluation.json')
    args = parser.parse_args()
    if args.repeat < 1:
        parser.error('--repeat must be positive')
    rows, failures = [], []
    start = time.perf_counter()
    ocr.initialize(ROOT / 'models')
    initialization = time.perf_counter() - start
    try:
        for index, case in enumerate(json.loads(args.manifest.read_text(encoding='utf8'))['cases']):
            path = args.images / case['file']
            times = []
            for _ in range(args.repeat):
                start = time.perf_counter()
                result = ocr.recognize(path, {'documentType': case['type'], 'side': case['side']})
                times.append(time.perf_counter() - start)
            expected = case.get('expected', {})
            wrong = [key for key, value in expected.items() if result['fields'].get(key, {}).get('value') != value]
            unexpected = [key for key in case.get('absent', []) if key in result['fields']]
            status_wrong = case.get('status') is not None and result['status'] != case['status']
            false_complete = result['status'] == 'complete' and bool(wrong or unexpected or status_wrong)
            if false_complete or case.get('strict', True) and (wrong or unexpected or status_wrong):
                failures.append(case['file'])
            row = {'file': case['file'], 'split': case.get('split', 'regression'),
                   'condition': case.get('condition', 'reference'), 'fieldCount': len(expected),
                   'correctFields': len(expected) - len(wrong),
                   'mismatches': {key: {'expected': expected[key], 'actual': result['fields'].get(key, {}).get('value')} for key in wrong},
                   'unexpected': unexpected,
                   'status': result['status'], 'falseComplete': false_complete,
                   'firstSeconds': times[0], 'warmSeconds': times[1:], 'warnings': result['warnings']}
            if args.compare:
                outputs, counts = [], []
                for mode in ('fast', 'exhaustive'):
                    directory = ROOT / 'out/evaluation-traces' / str(index) / mode
                    executable = ROOT / 'out/build/native' / ('Release/cardlens-ocr.exe' if sys.platform == 'win32' else 'cardlens-ocr')
                    command = [str(executable), str(ROOT / 'models'), str(path),
                               case['type'], case['side'], '--trace-dir', str(directory)]
                    if mode == 'exhaustive':
                        command.append('--exhaustive')
                    process = subprocess.run(command, capture_output=True, text=True, encoding='utf8', check=True)
                    outputs.append(json.loads(process.stdout))
                    events = [json.loads(line) for line in (directory / 'events.jsonl').read_text(encoding='utf8').splitlines()]
                    counts.append(sum(event['stage'] == 'orientation' for event in events))
                def signature(output):
                    return ({key: value['value'] for key, value in output['fields'].items()}, output['status'])
                row['sameAsExhaustive'] = signature(outputs[0]) == signature(outputs[1])
                row['candidateCounts'] = dict(zip(('fast', 'exhaustive'), counts))
                if not row['sameAsExhaustive']:
                    failures.append(case['file'] + ':exhaustive')
            rows.append(row)
            print(f"{case['file']}: {row['correctFields']}/{len(expected)}, {result['status']}, {times[0]:.2f}s" + (' FALSE_COMPLETE' if false_complete else ''), flush=True)
    finally:
        ocr.release()
    groups = {}
    for split in sorted({row['split'] for row in rows}):
        selected = [row for row in rows if row['split'] == split]
        warm = [value for row in selected for value in row['warmSeconds']]
        groups[split] = {'images': len(selected), 'annotatedFields': sum(row['fieldCount'] for row in selected),
                         'correctFields': sum(row['correctFields'] for row in selected),
                         'falseCompleteImages': sum(row['falseComplete'] for row in selected),
                         'falseCompleteRate': sum(row['falseComplete'] for row in selected) / len(selected),
                         'falseCompleteAmongComplete': (sum(row['falseComplete'] for row in selected) /
                             sum(row['status'] == 'complete' for row in selected)) if any(row['status'] == 'complete' for row in selected) else None,
                         'firstP95Seconds': percentile([row['firstSeconds'] for row in selected], .95),
                         'warmP50Seconds': percentile(warm, .5), 'warmP95Seconds': percentile(warm, .95)}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({'platform': sys.platform, 'runtime': 'native-python',
        'initializationSeconds': initialization, 'peakProcessBytes': peak_memory(),
        'resourceLimits': {'cpuCores': .25, 'memoryBytes': 2147483648} if os.getenv('CARDLENS_LIMITED_BUILD') == '1' else None,
        'groups': groups, 'cases': rows, 'failures': failures}, ensure_ascii=False, indent=2) + '\n', encoding='utf8')
    if failures:
        raise SystemExit('Evaluation failed: ' + ', '.join(failures))


if __name__ == '__main__':
    main()

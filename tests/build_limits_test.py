"""Check build isolation with short, bounded subprocesses."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from build_limits import run_limited

@unittest.skipUnless(sys.platform == 'linux', 'Linux cgroup check')
class BuildLimitsTest(unittest.TestCase):
    def test_missing_isolation_stops_build(self):
        with patch('build_limits.shutil.which', return_value=None):
            with self.assertRaisesRegex(RuntimeError, 'require systemd-run'):
                run_limited([sys.executable, '-c', 'raise SystemExit(99)'])

    def test_cpu_memory_and_child_inheritance(self):
        (ROOT / 'out').mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=ROOT / 'out') as folder:
            output = Path(folder) / 'limits.json'
            code = '''
import json, os, pathlib, subprocess, sys, time
sys.path.insert(0, sys.argv[1])
from build_limits import check_limits, THREAD_LIMITS
group = check_limits()
child = subprocess.check_output([sys.executable, '-c',
    "from pathlib import Path; print(Path('/proc/self/cgroup').read_text().strip())"], text=True).strip()
start, cpu = time.monotonic(), time.process_time()
while time.process_time() - cpu < .5: pass
result = {name: (group/name).read_text().strip() for name in
    ['cpu.max', 'memory.high', 'memory.max', 'memory.swap.max', 'pids.max']}
result.update(group=str(group), child=child, wall=time.monotonic()-start,
    threads={name: os.environ.get(name) for name in THREAD_LIMITS}, affinity=list(os.sched_getaffinity(0)))
pathlib.Path(sys.argv[2]).write_text(json.dumps(result))
'''
            self.assertEqual(run_limited([sys.executable, '-c', code, str(ROOT / 'scripts'), str(output)], timeout=20), 0)
            result = json.loads(output.read_text())
            self.assertIn('cardlens-build-', result['group'])
            self.assertIn(result['group'].removeprefix('/sys/fs/cgroup'), result['child'])
            self.assertEqual(result['cpu.max'], '25000 100000')
            self.assertEqual(int(result['memory.max']), 2 * 1024**3)
            self.assertEqual(result['memory.swap.max'], '0')
            self.assertEqual(result['pids.max'], '64')
            self.assertTrue(all(value == '1' for value in result['threads'].values()))
            self.assertEqual(len(result['affinity']), 1)
            self.assertGreater(result['wall'], 1.5)
            (ROOT / 'out/build-limits-check.json').write_text(json.dumps(result, indent=2))

    def test_timeout_kills_children(self):
        with tempfile.TemporaryDirectory(dir=ROOT / 'out') as folder:
            output = Path(folder) / 'child'
            code = '''
import pathlib, subprocess, sys, time
child = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(30)'])
pathlib.Path(sys.argv[1]).write_text(str(child.pid))
time.sleep(30)
'''
            self.assertNotEqual(run_limited([sys.executable, '-c', code, str(output)], timeout=2), 0)
            pid = int(output.read_text())
            status = Path(f'/proc/{pid}/status')
            if status.exists():
                self.assertIn('Z (zombie)', status.read_text())

if __name__ == '__main__':
    unittest.main()

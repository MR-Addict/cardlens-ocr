"""Run local Linux builds in a separate, bounded systemd cgroup."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import uuid

THREAD_LIMITS = {
    'CMAKE_BUILD_PARALLEL_LEVEL': '1',
    'EMCC_CORES': '1',
    'EMSDK_NUM_CORES': '1',
    'BINARYEN_CORES': '1',
    'OMP_NUM_THREADS': '1',
    'OPENBLAS_NUM_THREADS': '1',
    'MKL_NUM_THREADS': '1',
}
PROPERTIES = {
    'CPUQuota': '25%',
    'CPUWeight': '1',
    'MemoryHigh': '1G',
    'MemoryMax': '2G',
    'MemorySwapMax': '0',
    'TasksMax': '64',
    'Nice': '19',
    'IOWeight': '1',
    'OOMPolicy': 'kill',
    'KillMode': 'control-group',
    'TimeoutStopSec': '5s',
}

def check_limits():
    relative = Path('/proc/self/cgroup').read_text().strip().split('::', 1)[1]
    group = Path('/sys/fs/cgroup') / relative.lstrip('/')
    quota, period = map(int, (group / 'cpu.max').read_text().split())
    memory = (group / 'memory.max').read_text().strip()
    if quota <= 0 or quota / period > .25 or memory == 'max' or int(memory) > 2 * 1024**3:
        raise RuntimeError('Build resource limits are not active')
    return group

def run_limited(command, timeout=1800):
    if not shutil.which('systemd-run'):
        raise RuntimeError('Local Linux builds require systemd-run with user cgroup support')
    unit = 'cardlens-build-' + uuid.uuid4().hex[:12]
    args = ['systemd-run', '--user', '--wait', '--pipe', '--collect',
            '--unit=' + unit, '--working-directory=' + str(Path.cwd())]
    properties = {**PROPERTIES, 'RuntimeMaxSec': str(timeout),
                  'CPUAffinity': str(max(os.sched_getaffinity(0)))}
    for key, value in properties.items():
        args += ['--property=' + key + '=' + value]
    for key in ['PATH', 'EMSDK', 'EM_CONFIG', 'EM_CACHE', 'EMSDK_NODE',
                'OHOS_NDK_HOME', 'DEVECO_SDK_HOME', 'JAVA_HOME', 'CARDLENS_HARMONY_TOOLS']:
        if key in os.environ:
            args += ['--setenv=' + key + '=' + os.environ[key]]
    for key, value in {**THREAD_LIMITS, 'CARDLENS_LIMITED_BUILD': '1'}.items():
        args += ['--setenv=' + key + '=' + value]
    args += ['--', *map(str, command)]
    print('Local build limits: CPU 25% of one core, memory 2 GiB, one job', flush=True)
    process = subprocess.Popen(args)
    try:
        return process.wait()
    except BaseException:
        subprocess.run(['systemctl', '--user', 'stop', unit], check=False)
        process.wait()
        raise

def protect_build():
    os.environ.update(THREAD_LIMITS)
    if sys.platform != 'linux':
        return
    if os.environ.get('CARDLENS_LIMITED_BUILD') == '1':
        check_limits()
    elif os.environ.get('GITHUB_ACTIONS') != 'true':
        raise SystemExit(run_limited([sys.executable, *sys.argv]))

if __name__ == '__main__':
    if len(sys.argv) < 2:
        raise SystemExit('Usage: python scripts/build_limits.py COMMAND [ARGUMENT ...]')
    raise SystemExit(run_limited(sys.argv[1:]))

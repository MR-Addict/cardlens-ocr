#!/usr/bin/env python3
from pathlib import Path
import json
import shutil

root = Path(__file__).resolve().parents[1]
workspace = root / 'out/workspace'
workspace.mkdir(parents=True, exist_ok=True)
for name in ['CMakeLists.txt', 'core', 'sdk']:
    target = workspace / name
    if target.is_dir(): shutil.rmtree(target)
    if (root / name).is_dir():
        shutil.copytree(root / name, target, ignore=shutil.ignore_patterns('build', '.hvigor', '.cxx', 'oh_modules', 'node_modules'))
    else: shutil.copy2(root / name, target)
profile = workspace / 'sdk/harmonyos/cardlens_ocr/build-profile.json5'
config = json.loads(profile.read_text(encoding='utf-8'))
config['buildOption']['externalNativeOptions']['arguments'] = f'-DCARDLENS_DEPS={root}/out/deps/arm64 -DCARDLENS_COMMON={root}/out/deps/common -DCARDLENS_TESTS=OFF'
profile.write_text(json.dumps(config, indent=2) + '\n')
resources = workspace / 'sdk/harmonyos/cardlens_ocr/src/main/resources/rawfile/cardlens_ocr'
resources.mkdir(parents=True, exist_ok=True)
for path in (root / 'models').iterdir():
    if path.is_file(): shutil.copy2(path, resources / path.name)
shutil.copytree(root / 'licenses', resources / 'licenses', dirs_exist_ok=True)
shutil.copy2(root / 'NOTICE', resources / 'NOTICE')

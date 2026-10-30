#!/usr/bin/env python3
"""Fetch locked dependencies and prepare local model conversion tools."""
from pathlib import Path
import hashlib,json,subprocess,shutil,os
root=Path(__file__).resolve().parents[1]
out=root/'out'
for name in ['downloads','sources','tools','cache/uv','deps/common/include/nlohmann','models/paddle','logs']:(out/name).mkdir(parents=True,exist_ok=True)
for row in json.loads((root/'config/downloads.json').read_text(encoding='utf-8')):
    path=out/'downloads'/row['file']
    if not path.exists():
        partial=path.with_suffix(path.suffix+'.partial')
        subprocess.run(['curl','-fLsS','--retry','3',row['url'],'-o',str(partial)],check=True)
        partial.rename(path)
    if hashlib.sha256(path.read_bytes()).hexdigest()!=row['sha256']:raise SystemExit('Checksum mismatch: '+str(path))
for archive in ['opencv.tar.gz','clipper2.tar.gz']:
    target='opencv-31b0eeea0b44b370fd0712312df4214d4ae1b158' if archive=='opencv.tar.gz' else 'Clipper2-Clipper2_1.5.4'
    if not (out/'sources'/target).exists():
        subprocess.run(['tar','-xzf',str(out/'downloads'/archive),'-C',str(out/'sources')],check=True)
subprocess.run(['unzip','-qo',str(out/'downloads/pnnx.zip'),'-d',str(out/'tools')],check=True)
for kind in ['det','rec']:subprocess.run(['tar','-xf',str(out/f'downloads/PP-OCRv5_mobile_{kind}_infer.tar'),'-C',str(out/'models/paddle')],check=True)
shutil.copyfile(out/'downloads/json.hpp',out/'deps/common/include/nlohmann/json.hpp')
shutil.copytree(out/'sources/Clipper2-Clipper2_1.5.4/CPP/Clipper2Lib',out/'deps/common/clipper2',dirs_exist_ok=True)
env=os.environ.copy();env.update(UV_CACHE_DIR=str(out/'cache/uv'),UV_PYTHON_INSTALL_DIR=str(out/'tools/python'))
if not (out/'venv/bin/python').exists():subprocess.run(['uv','venv','--python','3.10',str(out/'venv')],env=env,check=True)
subprocess.run(['uv','pip','install','--python',str(out/'venv/bin/python'),'-r',str(root/'config/model-requirements.txt')],env=env,check=True)

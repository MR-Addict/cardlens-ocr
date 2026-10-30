"""Fetch public reference images for local testing only; no training license is implied."""
from pathlib import Path
import json,hashlib,urllib.request
root=Path(__file__).resolve().parents[1]
folder=root/'out/testdata';folder.mkdir(parents=True,exist_ok=True)
for row in json.loads((root/'tests/reference-sources.json').read_text(encoding='utf-8')):
    target=folder/(row['id']+'.jpg')
    if not target.exists():
        data=urllib.request.urlopen(row['url'],timeout=40).read()
        if hashlib.sha256(data).hexdigest()!=row['sha256']:raise SystemExit('Upstream reference changed')
        target.write_bytes(data)
    if hashlib.sha256(target.read_bytes()).hexdigest()!=row['sha256']:raise SystemExit('Reference checksum mismatch')
    print(row['id']+': verified')

#!/usr/bin/env python3
from pathlib import Path
import tarfile,hashlib,struct,json,subprocess,argparse
root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description='Verify an ARM64 HAR and its bundled OCR resources.')
parser.add_argument('--har',type=Path,default=root/'out/release/cardlens-ocr.har')
parser.add_argument('--output',type=Path,default=root/'out/validation/har.json')
args=parser.parse_args();path=args.har
args.output.parent.mkdir(parents=True,exist_ok=True)
assert path.stat().st_size<=100_000_000,'HAR exceeds 100MB'
with tarfile.open(path) as archive:
    files={m.name:archive.extractfile(m).read() for m in archive if m.isfile()}
    libraries={n:v for n,v in files.items() if n.endswith('.so')}
    assert any(n.endswith('/libc++_shared.so') for n in libraries),'C++ runtime missing'
    for name,data in libraries.items():
        assert '/arm64-v8a/' in name and data[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',data,18)[0]==183,name
    models={p.name:p for p in (root/'models').iterdir() if p.is_file()}
    for name,source in models.items():
        key=next((n for n in files if n.endswith('/rawfile/cardlens_ocr/'+name)),None)
        assert key is not None,'Missing model '+name
        assert hashlib.sha256(files[key]).digest()==hashlib.sha256(source.read_bytes()).digest(),'Model changed '+name
    assert any('/rawfile/cardlens_ocr/licenses/' in n for n in files),'Dependency notices missing'
    assert any(n.endswith('/Index.d.ts') for n in files),'ArkTS types missing'
    for name,data in libraries.items():
        if name.endswith('/libcardlens_ocr.so'):
            temporary=args.output.parent/'libcardlens_ocr.so';temporary.write_bytes(data)
            dependencies=subprocess.check_output(['readelf','-d',str(temporary)],text=True)
            assert 'libace_napi.z.so' in dependencies
            (args.output.parent/'native-dependencies.txt').write_text(dependencies)
            for symbol in ['initialize','recognize','release']:assert symbol.encode() in data,symbol
report={'har_bytes':path.stat().st_size,'har_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
        'unpacked_bytes':sum(map(len,files.values())),'models':sorted(models),'native_libraries':list(libraries),
        'models_match_local_sha256':True,'device_runtime_verified':False}
args.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))

#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$root/out/models/ncnn" "$root/out/models/onnx"
for kind in det rec; do
  "$root/out/venv/bin/paddle2onnx" --model_dir "$root/out/models/paddle/PP-OCRv5_mobile_${kind}_infer" \
    --model_filename inference.json --params_filename inference.pdiparams \
    --save_file "$root/out/models/onnx/$kind.onnx" --opset_version 14
  if [[ "$kind" == det ]]; then shape='[1,3,320,320]'; shape2='[1,3,256,256]'; else shape='[1,3,48,160]'; shape2='[1,3,48,256]'; fi
  "$root/out/tools/pnnx-20261020-linux/pnnx" "$root/out/models/onnx/$kind.onnx" \
    inputshape="$shape" inputshape2="$shape2" fp16=0 \
    ncnnparam="$root/out/models/ncnn/$kind.param" ncnnbin="$root/out/models/ncnn/$kind.bin" \
    pnnxparam="$root/out/models/onnx/$kind.pnnx.param" pnnxbin="$root/out/models/onnx/$kind.pnnx.bin" \
    pnnxpy="$root/out/models/onnx/${kind}_pnnx.py" pnnxonnx="$root/out/models/onnx/${kind}_pnnx.onnx" \
    ncnnpy="$root/out/models/onnx/${kind}_ncnn.py"
done
"$root/out/venv/bin/python" - "$root" <<'PY'
import sys,yaml,json,hashlib
from pathlib import Path
root=Path(sys.argv[1])
x=yaml.safe_load((root/'out/models/paddle/PP-OCRv5_mobile_rec_infer/inference.yml').read_text())
chars=x['PostProcess']['character_dict']
(root/'out/models/ncnn/dictionary.txt').write_text('\n'.join(chars)+'\n')
rows=[{'file':p.name,'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'bytes':p.stat().st_size} for p in sorted((root/'out/models/ncnn').iterdir()) if p.is_file()]
(root/'out/models/measurements.json').write_text(json.dumps(rows,indent=2)+'\n')
print('Dictionary entries:',len(chars))
PY

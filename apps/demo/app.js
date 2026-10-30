import ocr from './sdk/browser.js';
const $ = id => document.getElementById(id);
const labels = { name: '姓名', sex: '性别', ethnicity: '民族', birthDate: '出生日期', address: '住址', idNumber: '身份证号码', authority: '签发机关', validity: '有效期限', nationality: '国籍', licenseNumber: '证号', firstIssueDate: '初次领证日期', vehicleClass: '准驾车型', plateNumber: '号牌号码', vehicleType: '车辆类型', owner: '所有人', useCharacter: '使用性质', model: '品牌型号', vin: '车辆识别代号', engineNumber: '发动机号码', registerDate: '注册日期', issueDate: '发证日期' };
const errors = { INVALID_IMAGE: '图片无效，请选择 64–8192 像素、2400 万像素以内的图片', UNSUPPORTED_IMAGE_FORMAT: '请选择 JPEG 或 PNG 图片', BUSY: '正在识别，请稍候', NOT_INITIALIZED: '识别引擎尚未就绪', MODEL_LOAD_FAILED: '模型加载失败，请重试' };
let file, bitmap, result, ready = false, busy = false;
const canvas = $('canvas'), ctx = canvas.getContext('2d');
function message(text, error = false) { $('message').textContent = text; $('message').classList.toggle('error', error); }
function resetOutput() { result = undefined; $('output').hidden = true; $('empty').hidden = false; $('timing').textContent = ''; }
function draw() {
  if (!bitmap) return;
  canvas.width = bitmap.width; canvas.height = bitmap.height;
  ctx.drawImage(bitmap, 0, 0);
  if (!result) return;
  ctx.strokeStyle = '#2358d5'; ctx.lineWidth = Math.max(2, canvas.width / 450);
  for (const field of Object.values(result.fields)) {
    const polygon = field.polygon;
    if (!polygon?.length) continue;
    ctx.beginPath(); ctx.moveTo(...polygon[0]);
    polygon.slice(1).forEach(point => ctx.lineTo(...point)); ctx.closePath(); ctx.stroke();
  }
}
async function select(next) {
  if (busy || !next) return;
  try {
    if (!['image/jpeg', 'image/png'].includes(next.type) || next.size > 20000000) throw new Error('请选择 20 MB 以内的 JPEG 或 PNG 图片');
    const nextBitmap = await createImageBitmap(next, { imageOrientation: 'from-image' });
    if (Math.min(nextBitmap.width, nextBitmap.height) < 64 || Math.max(nextBitmap.width, nextBitmap.height) > 8192 || nextBitmap.width * nextBitmap.height > 24000000) {
      nextBitmap.close(); throw new Error('INVALID_IMAGE');
    }
    bitmap?.close(); bitmap = nextBitmap; file = next;
    resetOutput(); draw();
    $('dropzone').hidden = true; $('preview').hidden = false; $('file-row').hidden = false;
    $('filename').textContent = file.name; $('recognize').disabled = false;
    message('图片已就绪');
  } catch (error) { message(errors[error.message] || error.message, true); }
}
$('file').addEventListener('change', event => select(event.target.files[0]));
$('change').onclick = () => { $('file').value = ''; $('file').click(); };
$('dropzone').addEventListener('keydown', event => { if (event.key === 'Enter' || event.key === ' ') { event.preventDefault(); $('file').click(); } });
for (const name of ['dragover', 'dragleave', 'drop']) $('dropzone').addEventListener(name, event => {
  event.preventDefault(); $('dropzone').classList.toggle('dragover', name === 'dragover');
  if (name === 'drop') select(event.dataTransfer.files[0]);
});
$('type').onchange = () => {
  const id = $('type').value === 'id_card';
  $('side').replaceChildren(...(id ? [['portrait', '人像面'], ['emblem', '国徽面']] : [['main', '主页']]).map(([value, text]) => new Option(text, value)));
  resetOutput(); draw();
};
$('side').onchange = () => { resetOutput(); draw(); };
$('recognize').onclick = async () => {
  if (!file || busy) return;
  busy = true; resetOutput();
  for (const id of ['recognize', 'change', 'file', 'type', 'side']) $(id).disabled = true;
  try {
    if (!ready) {
      message('首次使用正在加载识别模型…'); $('progress').hidden = false;
      await ocr.initialize({ onProgress: ({ loaded, total }) => {
        $('progress').max = total; $('progress').value = loaded;
        message(loaded === total ? '正在初始化识别引擎…' : `正在加载识别模型 ${loaded}/${total}`);
      } }); ready = true; $('progress').hidden = true;
    }
    message('正在识别…'); const start = performance.now();
    result = await ocr.recognize(file, { documentType: $('type').value, side: $('side').value });
    $('timing').textContent = `${((performance.now() - start) / 1000).toFixed(2)} s`;
    $('status').textContent = { complete: '字段完整', partial: '待复核', failed: '识别失败' }[result.status];
    $('fields').replaceChildren(...Object.entries(result.fields).map(([key, field]) => {
      const row = document.createElement('div'), term = document.createElement('dt'), value = document.createElement('dd');
      term.textContent = labels[key] || key; value.textContent = field.value;
      const confidence = document.createElement('small'); confidence.textContent = `置信分数 ${(field.confidence * 100).toFixed(1)}%`;
      value.append(confidence); row.append(term, value); return row;
    }));
    $('warnings').replaceChildren(...result.warnings.map(text => { const li = document.createElement('li'); li.textContent = text; return li; }));
    $('lines').replaceChildren(...result.lines.map(line => { const p = document.createElement('p'); p.textContent = line.text; return p; }));
    $('empty').hidden = true; $('output').hidden = false; draw(); message('识别完成，请核对字段内容');
  } catch (error) { message(errors[error.message] || `识别失败：${error.message}`, true); }
  finally {
    busy = false; $('progress').hidden = true;
    for (const id of ['recognize', 'change', 'file', 'type', 'side']) $(id).disabled = false;
  }
};
$('download').onclick = () => {
  if (!result) return;
  const url = URL.createObjectURL(new Blob([JSON.stringify(result, null, 2)], { type: 'application/json' }));
  const a = document.createElement('a'); a.href = url; a.download = 'cardlens-result.json'; a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
};
window.addEventListener('pagehide', () => { if (!busy) void ocr.release(); });

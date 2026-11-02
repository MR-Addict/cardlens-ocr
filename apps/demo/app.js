import ocr from './sdk/browser.js';
import { imageInfo } from './sdk/image-info.js';
import { labels, fieldKeys, warningText, needsReview, suggestedDocument } from './results.js';
const $ = id => document.getElementById(id);
const errors = { INVALID_IMAGE: '图片无效，请选择 64–8192 像素、2400 万像素以内的图片', UNSUPPORTED_IMAGE_FORMAT: '请选择 JPEG 或 PNG 图片', BUSY: '正在识别，请稍候', NOT_INITIALIZED: '识别引擎尚未就绪', MODEL_LOAD_FAILED: '模型加载失败，请重试', SECURE_CONTEXT_REQUIRED: '请通过 HTTPS 或 localhost 打开页面', MODEL_INTEGRITY_FAILED: '模型文件校验失败，请联网重新加载', WORKER_TIMEOUT: '识别超时，请重试或使用较小的图片' };
let file, bitmap, result, ready = false, busy = false, selectedField;
const canvas = $('canvas'), ctx = canvas.getContext('2d');
function message(text, error = false) { $('message').hidden = false; $('message').textContent = text; $('message').classList.toggle('error', error); }
function resetOutput() { result = undefined; $('output').hidden = true; $('empty').hidden = false; $('timing').textContent = ''; selectedField = undefined; $('field-focus').hidden = true; $('switch-type').hidden = true; $('retry').hidden = true; }
function draw() {
  if (!bitmap) return;
  canvas.width = bitmap.width; canvas.height = bitmap.height;
  ctx.drawImage(bitmap, 0, 0);
  if (!result) return;
  ctx.strokeStyle = '#cb5435'; ctx.lineWidth = Math.max(2, canvas.width / 450);
  for (const [key, field] of Object.entries(result.fields)) {
    if (selectedField && key !== selectedField) continue;
    const polygon = field.polygon;
    if (!polygon?.length) continue;
    ctx.beginPath(); ctx.moveTo(...polygon[0]);
    polygon.slice(1).forEach(point => ctx.lineTo(...point)); ctx.closePath(); ctx.stroke();
  }
}
async function select(next) {
  if (busy || !next) return;
  busy = true;
  message('正在读取图片…');
  for (const id of ['change', 'clear', 'file', 'type', 'side', 'switch-type', 'retry']) $(id).disabled = true;
  try {
    if (!['image/jpeg', 'image/png'].includes(next.type) || next.size > 20000000) throw new Error('请选择 20 MB 以内的 JPEG 或 PNG 图片');
    if (next.size < 8) throw new Error('INVALID_IMAGE');
    imageInfo(await next.arrayBuffer());
    const nextBitmap = await createImageBitmap(next, { imageOrientation: 'from-image' });
    if (Math.min(nextBitmap.width, nextBitmap.height) < 64 || Math.max(nextBitmap.width, nextBitmap.height) > 8192 || nextBitmap.width * nextBitmap.height > 24000000) {
      nextBitmap.close(); throw new Error('INVALID_IMAGE');
    }
    bitmap?.close(); bitmap = nextBitmap; file = next;
    resetOutput(); draw();
    $('dropzone').hidden = true; $('preview').hidden = false; $('file-row').hidden = false;
    $('filename').textContent = file.name || '剪贴板图片'; $('controls').hidden = false;
  } catch (error) {
    message(errors[error.message] || error.message, true);
    return;
  } finally {
    busy = false;
    for (const id of ['change', 'clear', 'file', 'type', 'side', 'switch-type', 'retry']) $(id).disabled = false;
  }
  await recognize();
}
document.addEventListener('paste', event => {
  const item = Array.from(event.clipboardData?.items || []).find(item => item.kind === 'file' && item.type.startsWith('image/'));
  const image = item?.getAsFile();
  if (!image) return;
  event.preventDefault();
  void select(image);
});
$('file').addEventListener('change', event => select(event.target.files[0]));
$('change').onclick = () => { $('file').value = ''; $('file').click(); };
$('dropzone').addEventListener('keydown', event => { if (event.key === 'Enter' || event.key === ' ') { event.preventDefault(); $('file').click(); } });
for (const name of ['dragover', 'dragleave', 'drop']) $('dropzone').addEventListener(name, event => {
  event.preventDefault(); $('dropzone').classList.toggle('dragover', name === 'dragover');
  if (name === 'drop') select(event.dataTransfer.files[0]);
});
function updateSide() {
  const id = $('type').value === 'id_card';
  $('side-control').hidden = !id;
  $('side').replaceChildren(...(id ? [['portrait', '人像面'], ['emblem', '国徽面']] : [['main', '主页']]).map(([value, text]) => new Option(text, value)));
}
$('type').onchange = () => { updateSide(); void recognize(); };
$('side').onchange = () => { void recognize(); };
$('retry').onclick = () => { void recognize(); };
$('clear').onclick = () => {
  if (busy) return;
  bitmap?.close(); bitmap = undefined; file = undefined; $('file').value = '';
  canvas.width = canvas.height = 0; $('focus-canvas').width = $('focus-canvas').height = 0;
  resetOutput(); $('fields').replaceChildren(); $('lines').replaceChildren(); $('warnings').replaceChildren();
  $('dropzone').hidden = false; $('preview').hidden = true; $('file-row').hidden = true; $('message').textContent = ''; $('message').hidden = true;
};
async function copy(text) {
  try { await navigator.clipboard.writeText(text); message('已复制'); }
  catch { message('复制失败，请选择字段文字手动复制', true); }
}
$('copy-all').onclick = () => { if (result) void copy(fieldKeys(result).filter(key => result.fields[key]).map(key => `${labels[key]}：${result.fields[key].value}`).join('\n')); };
function focusField(key) {
  selectedField = key; draw();
  for (const button of $('fields').querySelectorAll('[data-field]')) button.setAttribute('aria-pressed', String(button.dataset.field === key));
  const points = result?.fields[key]?.polygon;
  $('field-focus').hidden = !points?.length;
  if (!points?.length) return;
  const xs = points.map(p => p[0]), ys = points.map(p => p[1]);
  const x = Math.max(0, Math.min(...xs) - 12), y = Math.max(0, Math.min(...ys) - 12);
  const w = Math.min(bitmap.width - x, Math.max(...xs) - x + 12), h = Math.min(bitmap.height - y, Math.max(...ys) - y + 12);
  const zoom = $('focus-canvas'); const scale = Math.min(3, 1000 / w, 240 / h);
  zoom.width = Math.max(1, Math.round(w * scale)); zoom.height = Math.max(1, Math.round(h * scale));
  zoom.getContext('2d').drawImage(bitmap, x, y, w, h, 0, 0, zoom.width, zoom.height);
  $('focus-label').textContent = `${labels[key]} · 原图局部`;
  if (window.matchMedia('(max-width:680px)').matches) $('field-focus').scrollIntoView({ block: 'nearest' });
}
function renderFields() {
  $('fields').replaceChildren(...fieldKeys(result).map(key => {
    const field = result.fields[key];
    const row = document.createElement('div'), term = document.createElement('dt'), value = document.createElement('dd');
    const focus = document.createElement('button'); focus.className = 'field-link'; focus.textContent = labels[key]; focus.dataset.field = key;
    focus.setAttribute('aria-pressed', 'false'); focus.disabled = !field?.polygon?.length;
    focus.onclick = () => focusField(key); term.append(focus);
    const text = document.createElement('span'); text.textContent = field?.value || '未识别'; value.append(text);
    const warnings = result.warnings.filter(warning => warning.startsWith(key + ':'));
    const review = !field || warnings.some(needsReview);
    row.classList.toggle('needs-review', review);
    const status = document.createElement('small'); status.textContent = !field ? '待补充' : review ? '待核对' : '已提取'; value.append(status);
    for (const warning of warnings) { const note = document.createElement('small'); note.textContent = warningText(warning); value.append(note); }
    if (field) {
      const button = document.createElement('button'); button.className = 'text-button copy-field'; button.textContent = '复制';
      button.setAttribute('aria-label', `复制${labels[key]}`); button.onclick = () => { void copy(field.value); }; value.append(button);
      const details = document.createElement('details'), summary = document.createElement('summary'), detail = document.createElement('p');
      summary.textContent = '识别详情'; detail.textContent = `模型分数 ${field.confidence.toFixed(3)}（未经正确率校准）；原始文字：${field.rawText}`;
      details.append(summary, detail); value.append(details);
    }
    row.append(term, value); return row;
  }));
  const suggestion = suggestedDocument(result);
  $('switch-type').hidden = !suggestion;
  if (suggestion) {
    $('switch-type').textContent = `可能是${suggestion.label}，点击切换`;
    $('switch-type').onclick = () => { $('type').value = suggestion.documentType; updateSide(); $('side').value = suggestion.side; void recognize(); };
  }
}
async function recognize() {
  if (!file || !$('type').value || busy) return;
  busy = true; resetOutput(); draw();
  $('empty').hidden = true; $('output-panel').setAttribute('aria-busy', 'true');
  for (const id of ['change', 'clear', 'file', 'type', 'side', 'switch-type', 'retry']) $(id).disabled = true;
  try {
    if (!ready) {
      message('首次使用正在加载识别模型…'); $('progress').hidden = false;
      performance.mark('cardlens-initialize-start');
      await ocr.initialize({ onProgress: ({ loaded, total }) => {
        $('progress').max = total; $('progress').value = loaded;
        message(loaded === total ? '正在初始化识别引擎…' : `正在加载识别模型 ${loaded}/${total}`);
      } }); performance.measure('cardlens-initialize', 'cardlens-initialize-start'); ready = true; $('progress').hidden = true;
    }
    message('正在识别…'); const start = performance.now();
    result = await ocr.recognize(file, { documentType: $('type').value, side: $('side').value });
    $('timing').textContent = `${((performance.now() - start) / 1000).toFixed(2)} s`;
    $('status').dataset.status = result.status;
    $('status').textContent = { complete: '字段完整', partial: '待复核', failed: '识别失败' }[result.status];
    renderFields();
    $('warnings').replaceChildren(...result.warnings.filter(warning => !warning.includes(':')).map(warning => {
      const li = document.createElement('li'); li.textContent = warningText(warning); return li;
    }));
    $('lines').replaceChildren(...result.lines.map(line => { const p = document.createElement('p'); p.textContent = line.text; return p; }));
    $('empty').hidden = true; $('output').hidden = false; draw(); message('识别完成，请核对字段内容');
  } catch (error) { ready = false; await ocr.release().catch(() => {}); $('retry').hidden = false; message(errors[error.message] || `识别失败：${error.message}`, true); }
  finally {
    busy = false; $('progress').hidden = true; $('output-panel').setAttribute('aria-busy', 'false');
    for (const id of ['change', 'clear', 'file', 'type', 'side', 'switch-type', 'retry']) $(id).disabled = false;
  }
}
$('download').onclick = () => {
  if (!result) return;
  const url = URL.createObjectURL(new Blob([JSON.stringify(result, null, 2)], { type: 'application/json' }));
  const a = document.createElement('a'); a.href = url; a.download = 'cardlens-result.json'; a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
};
window.addEventListener('pagehide', event => { if (!event.persisted && !busy) { ready = false; void ocr.release().catch(() => {}); } });

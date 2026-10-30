import { client } from './client.js';

async function decode(input) {
  const blob = input instanceof Blob ? input : new Blob([input]);
  if (blob.size < 8 || blob.size > 20000000) throw new Error('INVALID_IMAGE');
  const bytes = new Uint8Array(await blob.slice(0, 8).arrayBuffer());
  const mime = bytes[0] === 255 && bytes[1] === 216 ? 'image/jpeg' :
    bytes[0] === 137 && bytes[1] === 80 && bytes[2] === 78 && bytes[3] === 71 ? 'image/png' : null;
  if (!mime) throw new Error('UNSUPPORTED_IMAGE_FORMAT');
  const bitmap = await createImageBitmap(new Blob([blob], { type: mime }), { imageOrientation: 'from-image' });
  try {
    const { width, height } = bitmap;
    if (Math.min(width, height) < 64 || Math.max(width, height) > 8192 || width * height > 24000000)
      throw new Error('INVALID_IMAGE');
    const canvas = new OffscreenCanvas(width, height);
    const ctx = canvas.getContext('2d', { willReadFrequently: true });
    ctx.drawImage(bitmap, 0, 0);
    return { data: ctx.getImageData(0, 0, width, height).data.buffer, width, height, stride: width * 4 };
  } finally { bitmap.close(); }
}
const ocr = client((receive, failed) => {
  const worker = new Worker(new URL('./worker-browser.js', import.meta.url), { type: 'module' });
  worker.onmessage = event => receive(event.data);
  worker.onerror = event => failed(new Error(event.message || 'WORKER_FAILED'));
  return worker;
}, decode, { modelBaseUrl: new URL('./models/', import.meta.url).href });
export default ocr;

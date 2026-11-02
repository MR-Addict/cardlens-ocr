import { client } from './client.js';

async function decode(input) {
  const blob = input instanceof Blob ? input : new Blob([input]);
  if (blob.size < 8 || blob.size > 20000000) throw new Error('INVALID_IMAGE');
  const bytes = new Uint8Array(await blob.slice(0, 8).arrayBuffer());
  const mime = bytes[0] === 255 && bytes[1] === 216 ? 'image/jpeg' :
    bytes[0] === 137 && bytes[1] === 80 && bytes[2] === 78 && bytes[3] === 71 ? 'image/png' : null;
  if (!mime) throw new Error('UNSUPPORTED_IMAGE_FORMAT');
  return { data: await blob.arrayBuffer() };
}

const ocr = client((receive, failed) => {
  const worker = new Worker(new URL('./worker-browser.js', import.meta.url), { type: 'module' });
  worker.onmessage = event => receive(event.data);
  worker.onerror = event => failed(new Error(event.message || 'WORKER_FAILED'));
  return worker;
}, decode, { modelBaseUrl: new URL('./models/', import.meta.url).href });
export default ocr;

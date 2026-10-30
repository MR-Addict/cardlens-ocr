import { Worker } from 'node:worker_threads';
import { readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { client } from './client.js';

async function decode(input) {
  const bytes = typeof input === 'string' || input instanceof URL ? await readFile(input) :
    Buffer.from(new Uint8Array(input.buffer || input, input.byteOffset || 0, input.byteLength));
  if (bytes.length < 8 || bytes.length > 20000000) throw new Error('INVALID_IMAGE');
  const { default: sharp } = await import('sharp');
  const image = sharp(bytes, { limitInputPixels: 24000000 });
  const meta = await image.metadata();
  if (!['jpeg', 'png'].includes(meta.format)) throw new Error('UNSUPPORTED_IMAGE_FORMAT');
  if (Math.min(meta.width, meta.height) < 64 || Math.max(meta.width, meta.height) > 8192) throw new Error('INVALID_IMAGE');
  const { data, info } = await image.rotate().ensureAlpha().raw().toBuffer({ resolveWithObject: true });
  return { data: Uint8Array.from(data).buffer, width: info.width, height: info.height, stride: info.width * 4 };
}
const ocr = client((receive, failed) => {
  const worker = new Worker(new URL('./worker-node.js', import.meta.url));
  worker.on('message', receive);
  worker.on('error', failed);
  worker.on('exit', code => failed(new Error(`WORKER_EXIT_${code}`)));
  return worker;
}, decode, { modelDirectory: fileURLToPath(new URL('./models/', import.meta.url)) });
export default ocr;

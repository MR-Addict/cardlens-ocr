import { parentPort } from 'node:worker_threads';
import { readFile } from 'node:fs/promises';
import { join } from 'node:path';
import { createHash } from 'node:crypto';
import { modelFiles } from './model-integrity.js';
import { Runtime } from './runtime.js';
const runtime = new Runtime();
parentPort.on('message', async ({ id, method, args }) => {
  try {
    let result;
    if (method === 'initialize') {
      const files = [];
      for (const entry of modelFiles) {
        let bytes;
        try { bytes = await readFile(join(args.modelDirectory, entry.file)); }
        catch { throw new Error('MODEL_LOAD_FAILED'); }
        if (bytes.byteLength !== entry.bytes || createHash('sha256').update(bytes).digest('hex') !== entry.sha256)
          throw new Error('MODEL_INTEGRITY_FAILED');
        files.push([entry.file, bytes]);
      }
      const wasm = await readFile(new URL('./wasm/cardlens.wasm', import.meta.url));
      await runtime.initialize(files, wasm);
    } else if (method === 'recognize') result = runtime.recognize(args);
    else if (method === 'release') runtime.release();
    else throw new Error('INVALID_METHOD');
    parentPort.postMessage({ id, result });
  } catch (error) { parentPort.postMessage({ id, error: error.message || String(error) }); }
});

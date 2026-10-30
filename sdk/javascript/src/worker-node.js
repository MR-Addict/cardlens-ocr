import { parentPort } from 'node:worker_threads';
import { readFile } from 'node:fs/promises';
import { join } from 'node:path';
import { Runtime, modelNames } from './runtime.js';
const runtime = new Runtime();
parentPort.on('message', async ({ id, method, args }) => {
  try {
    let result;
    if (method === 'initialize') {
      const files = await Promise.all(modelNames.map(async name => [name, await readFile(join(args.modelDirectory, name))]));
      const wasm = await readFile(new URL('./wasm/cardlens.wasm', import.meta.url));
      await runtime.initialize(files, wasm);
    } else if (method === 'recognize') result = runtime.recognize(args);
    else if (method === 'release') runtime.release();
    else throw new Error('INVALID_METHOD');
    parentPort.postMessage({ id, result });
  } catch (error) { parentPort.postMessage({ id, error: error.message || String(error) }); }
});

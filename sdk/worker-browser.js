import { Runtime, modelNames } from './runtime.js';
const runtime = new Runtime();
self.onmessage = async ({ data: { id, method, args } }) => {
  try {
    let result;
    if (method === 'initialize') {
      const base = new URL(args.modelBaseUrl, self.location.href);
      const files = [];
      let loaded = 0;
      for (const name of modelNames) {
        const response = await fetch(new URL(name, base));
        if (!response.ok) throw new Error(`MODEL_DOWNLOAD_FAILED: ${name}`);
        const bytes = new Uint8Array(await response.arrayBuffer());
        files.push([name, bytes]);
        self.postMessage({ loaded: ++loaded, total: modelNames.length });
      }
      await runtime.initialize(files);
    } else if (method === 'recognize') result = runtime.recognize(args);
    else if (method === 'release') runtime.release();
    else throw new Error('INVALID_METHOD');
    self.postMessage({ id, result });
  } catch (error) { self.postMessage({ id, error: error.message || String(error) }); }
};

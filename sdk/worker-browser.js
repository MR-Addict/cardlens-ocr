import { Runtime } from './runtime.js';
import { decodeBrowser } from './decode-browser.js';
import { loadModels } from './model-loader.js';
const runtime = new Runtime();
self.onmessage = async ({ data: { id, method, args } }) => {
  try {
    let result;
    if (method === 'initialize') {
      const base = new URL(args.modelBaseUrl, self.location.href);
      const files = await loadModels(base, progress => self.postMessage(progress));
      await runtime.initialize(files);
    } else if (method === 'recognize') result = runtime.recognize({ ...await decodeBrowser(args.data), options: args.options });
    else if (method === 'release') runtime.release();
    else throw new Error('INVALID_METHOD');
    self.postMessage({ id, result });
  } catch (error) { self.postMessage({ id, error: error.message || String(error) }); }
};

export function client(spawn, decode, defaults) {
  let worker, ready = false, busy = false, initializing = false, sequence = 0, progress;
  const pending = new Map();
  function receive({ id, result, error, loaded, total }) {
    if (loaded !== undefined) { progress?.({ loaded, total }); return; }
    const item = pending.get(id);
    if (!item) return;
    pending.delete(id);
    if (error) item.reject(new Error(error)); else item.resolve(result);
  }
  function failed(error) {
    ready = false;
    for (const item of pending.values()) item.reject(error);
    pending.clear();
  }
  function call(method, args, transfer = []) {
    return new Promise((resolve, reject) => {
      const id = ++sequence;
      pending.set(id, { resolve, reject });
      try { worker.postMessage({ id, method, args }, transfer); }
      catch (error) { pending.delete(id); reject(error); }
    });
  }
  return {
    async initialize(options = {}) {
      if (ready || initializing) throw new Error('ALREADY_INITIALIZED');
      initializing = true;
      try {
        progress = options.onProgress;
        worker = spawn(receive, failed);
        await call('initialize', { ...defaults, ...options, onProgress: undefined });
        ready = true;
      } catch (error) {
        await worker?.terminate(); worker = undefined;
        throw error;
      } finally { initializing = false; }
    },
    async recognize(input, options) {
      if (!ready) throw new Error('NOT_INITIALIZED');
      if (busy) throw new Error('BUSY');
      const valid = options && ((options.documentType === 'id_card' && ['portrait', 'emblem'].includes(options.side)) ||
        (['driver_license', 'vehicle_license'].includes(options.documentType) && options.side === 'main'));
      if (!valid) throw new Error('INVALID_DOCUMENT_OPTIONS');
      busy = true;
      try {
        const image = await decode(input);
        return await call('recognize', { ...image, options }, [image.data]);
      } finally { busy = false; }
    },
    async release() {
      if (busy || initializing) throw new Error('BUSY');
      ready = false;
      if (worker) {
        try { await call('release'); }
        finally { await worker.terminate(); worker = undefined; }
      }
    }
  };
}

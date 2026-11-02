export function client(spawn, decode, defaults) {
  let worker, ready = false, busy = false, initializing = false, releasing = false, sequence = 0, generation = 0, progress;
  const pending = new Map();
  function dispose() {
    const previous = worker;
    worker = undefined;
    return previous ? Promise.resolve(previous.terminate()).catch(() => {}) : Promise.resolve();
  }
  function failed(error) {
    ready = false;
    ++generation;
    for (const item of pending.values()) { clearTimeout(item.timer); item.reject(error); }
    pending.clear();
    void dispose();
  }
  function receive({ id, result, error, loaded, total }) {
    if (loaded !== undefined) {
      try { progress?.({ loaded, total }); } catch { /* A UI callback must not strand initialization. */ }
      return;
    }
    const item = pending.get(id);
    if (!item) return;
    pending.delete(id); clearTimeout(item.timer);
    if (error) item.reject(new Error(error)); else item.resolve(result);
  }
  function call(method, args, transfer = []) {
    return new Promise((resolve, reject) => {
      const id = ++sequence;
      const timer = setTimeout(() => failed(new Error('WORKER_TIMEOUT')), 300000);
      pending.set(id, { resolve, reject, timer });
      try { worker.postMessage({ id, method, args }, transfer); }
      catch (error) { failed(error); }
    });
  }
  return {
    async initialize(options = {}) {
      if (releasing || busy) throw new Error('BUSY');
      if (ready || initializing) throw new Error('ALREADY_INITIALIZED');
      initializing = true;
      const current = ++generation;
      try {
        progress = options.onProgress;
        worker = spawn(message => { if (generation === current) receive(message); },
          error => { if (generation === current) failed(error); });
        await call('initialize', { ...defaults, ...options, onProgress: undefined });
        ready = true;
      } catch (error) {
        if (generation === current) { ++generation; await dispose(); }
        throw error;
      } finally { initializing = false; progress = undefined; }
    },
    async recognize(input, options) {
      if (!ready) throw new Error('NOT_INITIALIZED');
      if (busy || releasing) throw new Error('BUSY');
      const valid = options && ((options.documentType === 'id_card' && ['portrait', 'emblem'].includes(options.side)) ||
        (['driver_license', 'vehicle_license'].includes(options.documentType) && options.side === 'main'));
      if (!valid) throw new Error('INVALID_DOCUMENT_OPTIONS');
      busy = true;
      try {
        const image = await decode(input);
        if (!ready) throw new Error('NOT_INITIALIZED');
        return await call('recognize', { ...image, options }, [image.data]);
      } finally { busy = false; }
    },
    async release() {
      if (busy || initializing || releasing) throw new Error('BUSY');
      releasing = true; ready = false;
      try {
        if (worker) await call('release');
      } finally { ++generation; await dispose(); releasing = false; }
    }
  };
}

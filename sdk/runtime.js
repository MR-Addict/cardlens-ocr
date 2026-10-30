import createModule from './wasm/cardlens.mjs';

export class Runtime {
  module;
  handle = 0;
  async initialize(files, wasmBinary) {
    if (this.handle) throw new Error('ALREADY_INITIALIZED');
    const m = await createModule(wasmBinary ? { wasmBinary } : {});
    m.FS.mkdir('/models');
    for (const [name, bytes] of files) m.FS.writeFile(`/models/${name}`, bytes);
    this.module = m;
    const path = this.string('/models');
    const slots = m._malloc(8);
    m.HEAPU8.fill(0, slots, slots + 8);
    try {
      const code = m._cl_initialize(path, slots, slots + 4);
      this.check(code, slots + 4);
      this.handle = m.getValue(slots, '*');
    } finally {
      m._free(path); m._free(slots);
    }
  }
  string(value) {
    const m = this.module;
    const size = m.lengthBytesUTF8(value) + 1;
    const p = m._malloc(size);
    if (!p) throw new Error('ALLOCATION_FAILED');
    m.stringToUTF8(value, p, size);
    return p;
  }
  check(code, slot) {
    if (!code) return;
    const p = this.module.getValue(slot, '*');
    const message = p ? this.module.UTF8ToString(p) : 'OCR_FAILED';
    this.module._cl_free(p);
    throw new Error(message);
  }
  recognize({ data, width, height, stride, options }) {
    if (!this.handle) throw new Error('NOT_INITIALIZED');
    const m = this.module;
    const image = m._malloc(data.byteLength);
    const slots = m._malloc(8);
    const type = this.string(options.documentType), side = this.string(options.side);
    if (!image || !slots) throw new Error('ALLOCATION_FAILED');
    let result = 0;
    try {
      m.HEAPU8.set(new Uint8Array(data), image);
      const code = m._cl_recognize(this.handle, image, data.byteLength, width, height, stride,
        type, side, slots, slots + 4);
      this.check(code, slots + 4);
      result = m.getValue(slots, '*');
      return JSON.parse(m.UTF8ToString(result));
    } finally {
      m._cl_free(result);
      for (const p of [image, slots, type, side]) m._free(p);
    }
  }
  release() {
    if (this.handle) this.module._cl_release(this.handle);
    this.handle = 0;
    this.module = undefined;
  }
}

export const modelNames = ['det.param', 'det.bin', 'rec.param', 'rec.bin', 'dictionary.txt'];

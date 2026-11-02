import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { test } from 'node:test';
import { loadModels } from '../out/packages/cardlens-ocr/model-loader.js';

const original = globalThis.fetch;
test('model bytes are checked before initialization, and valid models report progress', async () => {
  try {
    globalThis.fetch = async url => {
      const bytes = new Uint8Array(await readFile(url)); bytes[0] ^= 1;
      return new Response(bytes);
    };
    const base = new URL('../models/', import.meta.url);
    await assert.rejects(loadModels(base, () => assert.fail('corrupt model reported as loaded')), /MODEL_INTEGRITY_FAILED/);
    let recovered = false;
    globalThis.fetch = async (url, options) => {
      const bytes = new Uint8Array(await readFile(url));
      if (options?.cache === 'reload') recovered = true;
      else if (!recovered) bytes[0] ^= 1;
      return new Response(bytes);
    };
    await loadModels(base, () => {});
    assert.equal(recovered, true);
    globalThis.fetch = async url => new Response(await readFile(url));
    const progress = [];
    const files = await loadModels(base, value => progress.push(value));
    assert.equal(files.length, 5);
    assert.deepEqual(progress.map(value => value.loaded), [1, 2, 3, 4, 5]);
    globalThis.fetch = async () => { throw new TypeError('Failed to fetch'); };
    await assert.rejects(loadModels(base, () => {}), /MODEL_LOAD_FAILED/);
    globalThis.fetch = async () => new Response('', { status: 404 });
    await assert.rejects(loadModels(base, () => {}), /MODEL_LOAD_FAILED/);
  } finally { globalThis.fetch = original; }
});

test('browser model integrity checking reports a missing secure context clearly', async () => {
  const descriptor = Object.getOwnPropertyDescriptor(globalThis, 'crypto');
  try {
    Object.defineProperty(globalThis, 'crypto', { value: undefined, configurable: true });
    await assert.rejects(loadModels(new URL('../models/', import.meta.url), () => {}), /SECURE_CONTEXT_REQUIRED/);
  } finally { Object.defineProperty(globalThis, 'crypto', descriptor); }
});

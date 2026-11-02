import assert from 'node:assert/strict';
import { readFile, mkdir, writeFile } from 'node:fs/promises';
import ocr from '../out/packages/cardlens-ocr/node.js';
import { resolve } from 'node:path';

assert.deepEqual(Object.keys(ocr).sort(), ['initialize', 'recognize', 'release']);
const options = { documentType: 'id_card', side: 'portrait' };
await assert.rejects(ocr.recognize(new Uint8Array(), options), /NOT_INITIALIZED/);
await assert.rejects(ocr.initialize({ modelDirectory: resolve('out/missing-models') }), /MODEL_LOAD_FAILED/);
const initializationStart = performance.now();
await ocr.initialize();
const initializationSeconds = (performance.now() - initializationStart) / 1000;
let peakProcessResidentBytes = process.memoryUsage().rss;
const memorySampler = setInterval(() => { peakProcessResidentBytes = Math.max(peakProcessResidentBytes, process.memoryUsage().rss); }, 25);
await assert.rejects(ocr.initialize(), /ALREADY_INITIALIZED/);
await assert.rejects(ocr.recognize(new Uint8Array(4), options), /INVALID_IMAGE/);
await assert.rejects(ocr.recognize(new Uint8Array(10), { documentType: 'other', side: 'main' }), /INVALID_DOCUMENT_OPTIONS/);
const gold = JSON.parse(await readFile('tests/gold.json', 'utf8'));
const results = [];
try {
  for (const item of gold.cases) {
    const image = new Uint8Array(await readFile(`out/testdata/${item.file}`));
    const start = performance.now();
    const promise = ocr.recognize(image, { documentType: item.type, side: item.side });
    await assert.rejects(ocr.recognize(image, options), /BUSY/);
    await assert.rejects(ocr.release(), /BUSY/);
    const result = await promise;
    for (const [name, value] of Object.entries(item.expected)) assert.equal(result.fields[name]?.value, value, `${item.file}: ${name}`);
    const firstSeconds = (performance.now() - start) / 1000;
    const warmStart = performance.now();
    const warm = await ocr.recognize(image, { documentType: item.type, side: item.side });
    const warmSeconds = (performance.now() - warmStart) / 1000;
    for (const [name, value] of Object.entries(item.expected)) assert.equal(warm.fields[name]?.value, value);
    results.push({ file: item.file, firstSeconds, warmSeconds, result });
    console.log(`${item.file}: ${Object.keys(item.expected).length} fields passed`);
  }
} finally { clearInterval(memorySampler); await ocr.release(); }
await ocr.release();
await ocr.initialize();
await ocr.release();
await mkdir('out/validation', { recursive: true });
await writeFile('out/validation/node-results.json', JSON.stringify(results, null, 2));

const sorted = results.map(item => item.warmSeconds).sort((a, b) => a - b);
await writeFile('out/validation/node-performance.json', JSON.stringify({ runtime: 'node-wasm',
  resourceLimits: process.env.CARDLENS_LIMITED_BUILD === '1' ? { cpuCores: .25, memoryBytes: 2147483648 } : null,
  initializationSeconds, peakSampledProcessResidentBytes: peakProcessResidentBytes, memorySamplingIntervalMs: 25, warmP95Seconds: sorted[Math.ceil(sorted.length * .95) - 1],
  cases: results.map(({ file, firstSeconds, warmSeconds }) => ({ file, firstSeconds, warmSeconds })) }, null, 2));

import assert from 'node:assert/strict';
import { readFile, mkdir, writeFile } from 'node:fs/promises';
import ocr from '../out/packages/cardlens-ocr/node.js';
import { resolve } from 'node:path';

assert.deepEqual(Object.keys(ocr).sort(), ['initialize', 'recognize', 'release']);
const options = { documentType: 'id_card', side: 'portrait' };
await assert.rejects(ocr.recognize(new Uint8Array(), options), /NOT_INITIALIZED/);
await assert.rejects(ocr.initialize({ modelDirectory: resolve('out/missing-models') }));
await ocr.initialize();
await assert.rejects(ocr.initialize(), /ALREADY_INITIALIZED/);
await assert.rejects(ocr.recognize(new Uint8Array(4), options), /INVALID_IMAGE/);
await assert.rejects(ocr.recognize(new Uint8Array(10), { documentType: 'other', side: 'main' }), /INVALID_DOCUMENT_OPTIONS/);
const gold = JSON.parse(await readFile('tests/gold.json', 'utf8'));
const results = [];
try {
  for (const item of gold.cases) {
    const image = new Uint8Array(await readFile(`out/testdata/${item.file}`));
    const promise = ocr.recognize(image, { documentType: item.type, side: item.side });
    await assert.rejects(ocr.recognize(image, options), /BUSY/);
    await assert.rejects(ocr.release(), /BUSY/);
    const result = await promise;
    for (const [name, value] of Object.entries(item.expected)) assert.equal(result.fields[name]?.value, value, `${item.file}: ${name}`);
    results.push({ file: item.file, result });
    console.log(`${item.file}: ${Object.keys(item.expected).length} fields passed`);
  }
} finally { await ocr.release(); }
await ocr.release();
await ocr.initialize();
await ocr.release();
await mkdir('out/validation', { recursive: true });
await writeFile('out/validation/node-results.json', JSON.stringify(results, null, 2));

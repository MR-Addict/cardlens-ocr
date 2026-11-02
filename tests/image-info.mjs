import assert from 'node:assert/strict';
import { test } from 'node:test';
import { readFile } from 'node:fs/promises';
import { imageInfo } from '../sdk/javascript/src/image-info.js';

test('JPEG header validation accepts reference inputs before decode', async () => {
  for (const name of ['id-front.jpg', 'id-back.jpg', 'driver.jpg', 'vehicle.jpg']) {
    const result = imageInfo(await readFile(new URL(`../out/testdata/${name}`, import.meta.url)));
    assert.equal(result.mime, 'image/jpeg'); assert.ok(result.width >= 64 && result.height >= 64);
  }
});
test('encoded size limits reject oversized PNG and malformed JPEG headers', () => {
  const png = new Uint8Array(33), view = new DataView(png.buffer);
  view.setUint32(0, 0x89504e47); view.setUint32(4, 0x0d0a1a0a);
  view.setUint32(8, 13); view.setUint32(12, 0x49484452);
  view.setUint32(16, 800); view.setUint32(20, 600);
  assert.deepEqual(imageInfo(png), { width: 800, height: 600, mime: 'image/png' });
  view.setUint32(16, 8192); view.setUint32(20, 8192);
  assert.throws(() => imageInfo(png), /INVALID_IMAGE/);
  assert.throws(() => imageInfo(new Uint8Array([255, 216, 255, 192, 0, 1])), /INVALID_IMAGE/);
  assert.throws(() => imageInfo(new Uint8Array([255, 216, 255, 218, 0, 8])), /INVALID_IMAGE/);
  assert.throws(() => imageInfo(new Uint8Array(8)), /UNSUPPORTED_IMAGE_FORMAT/);
});

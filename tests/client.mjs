import assert from 'node:assert/strict';
import { test } from 'node:test';
import { client } from '../sdk/javascript/src/client.js';

function fixture() {
  const workers = [];
  let decoding;
  const ocr = client((receive, fail) => {
    const worker = { receive, fail, terminated: false, messages: [], terminate() { this.terminated = true; },
      postMessage(message) { this.messages.push(message); } };
    workers.push(worker); return worker;
  }, () => decoding || Promise.resolve({ data: new ArrayBuffer(8) }), {});
  const respond = (worker = workers.at(-1), error) => {
    const { id } = worker.messages.at(-1);
    worker.receive({ id, result: { status: 'partial' }, error });
  };
  return { ocr, workers, respond, setDecode(value) { decoding = value; } };
}
const options = { documentType: 'id_card', side: 'portrait' };
test('concurrent lifecycle operations and failed initialization can recover', async () => {
  const f = fixture();
  await assert.rejects(f.ocr.recognize('', options), /NOT_INITIALIZED/);
  const first = f.ocr.initialize();
  await assert.rejects(f.ocr.release(), /BUSY/);
  await assert.rejects(f.ocr.initialize(), /ALREADY_INITIALIZED/);
  f.respond(undefined, 'MODEL_LOAD_FAILED'); await assert.rejects(first, /MODEL_LOAD_FAILED/);
  assert.equal(f.workers[0].terminated, true);
  const second = f.ocr.initialize(); f.respond(); await second;
  await assert.rejects(f.ocr.recognize('', {}), /INVALID_DOCUMENT_OPTIONS/);
  const recognition = f.ocr.recognize('', options); await Promise.resolve();
  await assert.rejects(f.ocr.recognize('', options), /BUSY/);
  await assert.rejects(f.ocr.release(), /BUSY/);
  f.respond(); await recognition;
  const release = f.ocr.release();
  await assert.rejects(f.ocr.initialize(), /BUSY/);
  await assert.rejects(f.ocr.release(), /BUSY/);
  f.respond(); await release; await f.ocr.release();
});
test('worker crash terminates pending work and old callbacks cannot break a new worker', async () => {
  const f = fixture();
  let promise = f.ocr.initialize(); f.respond(); await promise;
  const old = f.workers[0];
  promise = f.ocr.recognize('', options); await Promise.resolve();
  old.fail(new Error('WORKER_FAILED')); await assert.rejects(promise, /WORKER_FAILED/);
  assert.equal(old.terminated, true);
  promise = f.ocr.initialize(); old.fail(new Error('STALE_EXIT')); f.respond(); await promise;
  promise = f.ocr.release(); f.respond(); await promise;
});
test('progress callbacks and decode failures do not strand the client', async () => {
  const f = fixture();
  let promise = f.ocr.initialize({ onProgress() { throw new Error('UI_FAILED'); } });
  f.workers[0].receive({ loaded: 1, total: 5 }); f.respond(); await promise;
  f.setDecode(Promise.reject(new Error('INVALID_IMAGE')));
  await assert.rejects(f.ocr.recognize('', options), /INVALID_IMAGE/);
  f.setDecode(undefined);
  promise = f.ocr.recognize('', options); await Promise.resolve(); f.respond(); await promise;
  promise = f.ocr.release(); f.respond(); await promise;
});

test('unresponsive worker times out and permits initialization again', async t => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const f = fixture();
  const pending = f.ocr.initialize();
  const rejection = assert.rejects(pending, /WORKER_TIMEOUT/);
  t.mock.timers.tick(300000);
  await rejection;
  assert.equal(f.workers[0].terminated, true);
  const retry = f.ocr.initialize(); f.respond(); await retry;
  const release = f.ocr.release(); f.respond(); await release;
});

import { modelFiles } from './model-integrity.js';

async function checkedBytes(response, entry) {
  if (!response.ok) throw new Error('MODEL_LOAD_FAILED');
  const bytes = new Uint8Array(await response.arrayBuffer());
  if (bytes.byteLength !== entry.bytes) throw new Error('MODEL_INTEGRITY_FAILED');
  const digest = await crypto.subtle.digest('SHA-256', bytes);
  const hash = Array.from(new Uint8Array(digest), byte => byte.toString(16).padStart(2, '0')).join('');
  if (hash !== entry.sha256) throw new Error('MODEL_INTEGRITY_FAILED');
  return bytes;
}
export async function loadModels(base, onProgress) {
  if (!globalThis.crypto?.subtle) throw new Error('SECURE_CONTEXT_REQUIRED');
  const files = [];
  let loaded = 0;
  for (const entry of modelFiles) {
    const url = new URL(entry.file, base);
    let bytes;
    try { bytes = await checkedBytes(await fetch(url), entry); }
    catch (error) {
      if (error.message !== 'MODEL_INTEGRITY_FAILED') throw new Error('MODEL_LOAD_FAILED');
      // A damaged local cache can recover while the network is available.
      try { bytes = await checkedBytes(await fetch(url, { cache: 'reload' }), entry); }
      catch { throw new Error('MODEL_INTEGRITY_FAILED'); }
    }
    files.push([entry.file, bytes]);
    onProgress({ loaded: ++loaded, total: modelFiles.length });
  }
  return files;
}

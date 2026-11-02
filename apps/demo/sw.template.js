const ASSETS = __ASSETS__;
const root = new URL('./', self.location.href);
const PREFIX = 'cardlens-offline-' + root.pathname + ':';
const CACHE = PREFIX + '__CACHE__';
const assets = new Map(ASSETS.map(asset => [new URL(asset.path, root).href, asset]));
async function verified(asset) {
  const response = await fetch(new URL(asset.path, root), { cache: 'reload' });
  if (!response.ok || response.type === 'opaque') throw new Error('OFFLINE_DOWNLOAD_FAILED');
  const buffer = await response.clone().arrayBuffer();
  const digest = await crypto.subtle.digest('SHA-256', buffer);
  const hash = Array.from(new Uint8Array(digest), byte => byte.toString(16).padStart(2, '0')).join('');
  if (hash !== asset.sha256) throw new Error('OFFLINE_INTEGRITY_FAILED');
  return response;
}
self.addEventListener('install', event => event.waitUntil((async () => {
  const cache = await caches.open(CACHE);
  try {
    for (const asset of ASSETS) await cache.put(new URL(asset.path, root), await verified(asset));
    // Existing tabs keep their current worker until they close, avoiding mixed assets.
  } catch (error) { await caches.delete(CACHE); throw error; }
})()));
self.addEventListener('activate', event => event.waitUntil((async () => {
  // Only remove caches belonging to this deployment path.
  for (const key of await caches.keys())
    if (key.startsWith(PREFIX) && key !== CACHE) await caches.delete(key);
  await self.clients.claim();
})()));
self.addEventListener('fetch', event => {
  if (event.request.method !== 'GET') return;
  const url = new URL(event.request.url);
  if (url.href === root.href) url.pathname += 'index.html';
  const asset = assets.get(url.href);
  if (!asset) return;
  event.respondWith((async () => {
    const cache = await caches.open(CACHE);
    const cached = await cache.match(new URL(asset.path, root));
    if (cached && event.request.cache !== 'reload') return cached;
    const response = await verified(asset);
    await cache.put(new URL(asset.path, root), response.clone());
    return response;
  })());
});

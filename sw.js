const ASSETS = [{"path":"app.js","sha256":"f2de682ed863a7a7b041d01b76a43436d3a5cbb6f20819cd5027bdae1913b2e0"},{"path":"icon.svg","sha256":"c00baafbd3a022f8099bb87ca7cd577018062d25983568a228e7a686ab8ef039"},{"path":"index.html","sha256":"2a7027398d20e26e365e5d54186230923b60b24b3955f33d6b774c4320ca1c26"},{"path":"offline.js","sha256":"c830e6b6894d1e0db3965d34490ac6a69a4a4d7d5f579c102615da5687e54545"},{"path":"results.js","sha256":"8b8aec8e08cfe87afbc7af342de045a931dec4e3cd40126989df9bfd657fb3a5"},{"path":"sdk/browser.js","sha256":"93842a965adb02e57e0b416371dfdec16efbc7bfa9258637045a4662ac04f177"},{"path":"sdk/client.js","sha256":"6222c7b2febbb4eed5564d45cf68488a2293445f15334e8126124608acf6a520"},{"path":"sdk/decode-browser.js","sha256":"c4997273b0627c43329f6b471354b2a8894ab592a824ddc1af2b26041dfbf376"},{"path":"sdk/image-info.js","sha256":"b34cbe5877eacd2ba37b1c9b44427b40ef2e1ecc82ad1f0d3a94fbc1ddae80c0"},{"path":"sdk/model-integrity.js","sha256":"d5755a35bd54bdf06e0ca38a117fce2342c1ec591612081029e6fa43730d2fb7"},{"path":"sdk/model-loader.js","sha256":"446dc4cec0e38e5e56f1f35660de8325fa7edf0d95a4bd392bad0d4223cf0e34"},{"path":"sdk/models/det.bin","sha256":"c7aa3ed7fe5791df29becbf4a8ffbee4ca20e4357fd4ce5114c29101dd2caa04"},{"path":"sdk/models/det.param","sha256":"e8edfc052be45735978adb8b7da9a03b1bac01bb8a6653a36993a402f6b38922"},{"path":"sdk/models/dictionary.txt","sha256":"d1979e9f794c464c0d2e0b70a7fe14dd978e9dc644c0e71f14158cdf8342af1b"},{"path":"sdk/models/rec.bin","sha256":"1d73b6e2ee6cbd02cabfac6ef9b6a48e62ae76ae02200ea23ac57b121e29697c"},{"path":"sdk/models/rec.param","sha256":"86f8c19a9b2ecf83143b0fdf0060ce07e092260ed5615fc5bb552900c3a5df91"},{"path":"sdk/runtime.js","sha256":"8f2bdd94ad89632b3f039655e0813b4dbf63ca233af520bdc4d3b5d4242bae50"},{"path":"sdk/wasm/cardlens.mjs","sha256":"8e32ded9b8fd49bbc7991c51acd854a7f298d5ea10590fd63c1c8c1d7e6ee125"},{"path":"sdk/wasm/cardlens.wasm","sha256":"b528eaa032bf309fd8bb19f4bc671bc58e0879e9bca73d7985c27da64ec0dc06"},{"path":"sdk/worker-browser.js","sha256":"9fe21205f660c0a63178413339f6ff111344bd1733b154354330b139b3579416"},{"path":"style.css","sha256":"4aa325621db26e5032238c70adfbdb79aa0d59fcab04bfba9521b7b14eb1dc6d"}];
const root = new URL('./', self.location.href);
const PREFIX = 'cardlens-offline-' + root.pathname + ':';
const CACHE = PREFIX + 'bab7d13b7910aeb6598e10291c18b321ad550dbad9f662ad47f235f43d4b1069';
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

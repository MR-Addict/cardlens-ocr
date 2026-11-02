import { cp, mkdir, writeFile, rm, readFile, readdir } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { resolve } from 'node:path';
const root = resolve(import.meta.dirname, '..');
const pkg = resolve(root, 'out/packages/cardlens-ocr');
await rm(pkg, { recursive: true, force: true });
await mkdir(pkg, { recursive: true });
await cp(resolve(root, 'sdk/javascript/src'), pkg, { recursive: true });
await cp(resolve(root, 'sdk/javascript/package.json'), resolve(pkg, 'package.json'));
const manifest = JSON.parse(await readFile(resolve(root, 'models/manifest.json'), 'utf8'));
await writeFile(resolve(pkg, 'model-integrity.js'), `export const modelFiles = ${JSON.stringify(manifest.files)};\n`);
await mkdir(resolve(pkg, 'wasm'));
for (const name of ['cardlens.mjs', 'cardlens.wasm']) {
  await cp(resolve(root, 'out/wasm', name), resolve(pkg, 'wasm', name));
}
await cp(resolve(root, 'models'), resolve(pkg, 'models'), { recursive: true });
await cp(resolve(root, 'LICENSE'), resolve(pkg, 'LICENSE'));
await cp(resolve(root, 'NOTICE'), resolve(pkg, 'NOTICE'));
await cp(resolve(root, 'licenses'), resolve(pkg, 'licenses'), { recursive: true });
const dist = resolve(root, 'out/site');
await rm(dist, { recursive: true, force: true });
await cp(resolve(root, 'apps/demo'), dist, { recursive: true });
await cp(pkg, resolve(dist, 'sdk'), { recursive: true });
await rm(resolve(dist, 'sw.template.js'));
const assets = [];
async function collect(directory = '') {
  for (const entry of await readdir(resolve(dist, directory), { withFileTypes: true })) {
    const path = directory ? `${directory}/${entry.name}` : entry.name;
    if (entry.isDirectory()) await collect(path);
    else if (/\.(?:html|css|js|mjs|wasm|bin|param|txt|svg)$/.test(path) && !/^(?:sdk\/licenses\/|sdk\/(?:node|worker-node)\.js)/.test(path)) {
      const sha256 = createHash('sha256').update(await readFile(resolve(dist, path))).digest('hex');
      assets.push({ path, sha256 });
    }
  }
}
await collect();
assets.sort((a, b) => a.path.localeCompare(b.path));
const digest = createHash('sha256').update(JSON.stringify(assets)).digest('hex');
const template = await readFile(resolve(root, 'apps/demo/sw.template.js'), 'utf8');
await writeFile(resolve(dist, 'sw.js'), template.replace('__ASSETS__', JSON.stringify(assets)).replace('__CACHE__', digest));
await writeFile(resolve(dist, '.nojekyll'), '');
console.log(`Built JS SDK and demo: ${dist}`);

import { cp, mkdir, writeFile, rm } from 'node:fs/promises';
import { resolve } from 'node:path';
const root = resolve(import.meta.dirname, '..');
const pkg = resolve(root, 'out/packages/cardlens-ocr');
await rm(pkg, { recursive: true, force: true });
await mkdir(pkg, { recursive: true });
await cp(resolve(root, 'sdk/javascript/src'), pkg, { recursive: true });
await cp(resolve(root, 'sdk/javascript/package.json'), resolve(pkg, 'package.json'));
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
await writeFile(resolve(dist, '.nojekyll'), '');
console.log(`Built JS SDK and demo: ${dist}`);

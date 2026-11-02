import { test, expect } from '@playwright/test';
import sharp from 'sharp';
import { readFile, writeFile, mkdir } from 'node:fs/promises';

test('recognizes four document sides locally and exports JSON', async ({ page }) => {
  const errors = [], uploads = [], timings = [];
  page.on('pageerror', error => errors.push(error.message));
  page.on('request', request => { if (!['GET', 'HEAD'].includes(request.method())) uploads.push(request.url()); });
  await page.goto('./');
  await expect(page.getByRole('heading', { name: /从一张证件/ })).toBeVisible();
  const gold = JSON.parse(await readFile('tests/gold.json', 'utf8'));
  for (const item of gold.cases) {
    if (await page.locator('#type').inputValue() !== item.type) {
      await page.locator('#type').selectOption(item.type);
      await expect(page.locator('#type')).toBeEnabled({ timeout: 150000 });
    }
    if (item.type === 'id_card' && await page.locator('#side').inputValue() !== item.side) {
      await page.locator('#side').selectOption(item.side);
      await expect(page.locator('#side')).toBeEnabled({ timeout: 150000 });
    }
    await page.locator('#file').setInputFiles(`out/testdata/${item.file}`);
    await expect(page.locator('#type')).toBeEnabled({ timeout: 150000 });
    await expect(page.locator('#message')).toHaveText('识别完成，请核对字段内容', { timeout: Number(process.env.CARDLENS_TEST_TIMEOUT_MS || 150000) });
    for (const value of Object.values(item.expected)) await expect(page.locator('#fields')).toContainText(value);
    const downloadEvent = page.waitForEvent('download');
    await page.locator('#download').click();
    const download = await downloadEvent;
    const result = JSON.parse(await readFile(await download.path(), 'utf8'));
    const warm = await page.evaluate(async ({ file, type, side }) => {
      const { default: ocr } = await import('./sdk/browser.js');
      const bytes = Uint8Array.from(atob(file), char => char.charCodeAt(0));
      const start = performance.now();
      const result = await ocr.recognize(bytes, { documentType: type, side });
      return { seconds: (performance.now() - start) / 1000, result };
    }, { file: (await readFile(`out/testdata/${item.file}`)).toString('base64'), type: item.type, side: item.side });
    for (const [key, value] of Object.entries(item.expected)) expect(warm.result.fields[key]?.value).toBe(value);
    timings.push({ file: item.file, firstSeconds: Number((await page.locator('#timing').textContent()).replace(' s', '')), warmSeconds: warm.seconds });
    expect(result.documentType).toBe(item.type);
    for (const [key, value] of Object.entries(item.expected)) expect(result.fields[key].value).toBe(value);
    await page.locator('#clear').click();
  }
  await mkdir('out/validation', { recursive: true });
  const initializationSeconds = await page.evaluate(() => performance.getEntriesByName('cardlens-initialize')[0].duration / 1000);
  const warm = timings.map(item => item.warmSeconds).sort((a, b) => a - b);
  await writeFile('out/validation/browser-performance.json', JSON.stringify({ runtime: 'browser-wasm',
    resourceLimits: process.env.CARDLENS_LIMITED_BUILD === '1' ? { cpuCores: .25, memoryBytes: 2147483648 } : null,
    initializationSeconds, peakProcessResidentBytes: null,
    memoryNote: 'Browser process peak memory is not exposed by this test.',
    warmP95Seconds: warm[Math.ceil(warm.length * .95) - 1], cases: timings }, null, 2));
  expect(uploads).toEqual([]);
  expect(errors).toEqual([]);
});

test('field overlay stays inside the preview throughout its entrance animation', async ({ page }) => {
  for (const width of [1280, 390]) {
    await page.setViewportSize({ width, height: 900 });
    await page.goto('./');
    const frames = await page.evaluate(() => {
      document.getElementById('dropzone').hidden = true;
      const preview = document.getElementById('preview'), overlay = document.getElementById('field-focus');
      preview.hidden = false; overlay.hidden = false;
      const animations = overlay.getAnimations();
      return [0, .5, 1].map(progress => {
        for (const animation of animations) {
          animation.pause(); animation.currentTime = animation.effect.getTiming().duration * progress;
        }
        const container = preview.getBoundingClientRect(), bounds = overlay.getBoundingClientRect();
        return { left: bounds.left - container.left, top: bounds.top - container.top,
          right: container.right - bounds.right, bottom: container.bottom - bounds.bottom };
      });
    });
    for (const frame of frames) {
      for (const distance of Object.values(frame)) expect(distance).toBeGreaterThanOrEqual(-1);
    }
  }
});

test('responsive layout and image validation', async ({ page }) => {
  await page.setViewportSize({ width: 390, height: 844 });
  await page.goto('./');
  await expect(page.locator('#dropzone')).toBeVisible();
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);
  await page.locator('#file').setInputFiles({ name: 'bad.txt', mimeType: 'text/plain', buffer: Buffer.from('invalid') });
  await expect(page.locator('#message')).toHaveClass(/error/);
  await expect(page.locator('#recognize')).toHaveCount(0);
});


test('uploads and option changes recognize automatically without a separate button', async ({ page }) => {
  await page.goto('./');
  await expect(page.locator('#controls')).toBeVisible();
  await expect(page.locator('#recognize')).toHaveCount(0);
  await page.locator('#file').setInputFiles('out/testdata/id-front.jpg');
  await expect(page.locator('#controls')).toBeVisible();
  await expect(page.locator('#preview')).toBeVisible();
  await expect(page.locator('#side-control')).toBeVisible();
  await expect(page.locator('#type')).toBeEnabled({ timeout: 150000 });
  await expect(page.locator('#fields')).toContainText('刘洋');
  await page.locator('#side').selectOption('emblem');
  await expect(page.locator('#side')).toBeEnabled({ timeout: 150000 });
  for (const type of ['driver_license', 'vehicle_license']) {
    await page.locator('#type').selectOption(type);
    await expect(page.locator('#side-control')).toBeHidden();
    await expect(page.locator('#side')).toHaveValue('main');
    await expect(page.locator('#type')).toBeEnabled({ timeout: 150000 });
  }
  await page.locator('#type').selectOption('id_card');
  await expect(page.locator('#side')).toHaveValue('portrait');
  await expect(page.locator('#side-control')).toBeVisible();
  await expect(page.locator('#type')).toBeEnabled({ timeout: 150000 });
  await page.locator('#file').setInputFiles('out/testdata/id-back.jpg');
  await expect(page.locator('#filename')).toHaveText('id-back.jpg');
  await expect(page.locator('#type')).toBeEnabled({ timeout: 150000 });
  await page.locator('#side').selectOption('emblem');
  await expect(page.locator('#type')).toBeEnabled({ timeout: 150000 });
  await expect(page.locator('#fields')).toContainText('2018.08.12-2038.08.12');
});

test('footer reaches viewport bottom and layout fits different screen sizes', async ({ page }) => {
  for (const [width, height] of [[1440, 1000], [1085, 1186], [768, 1024], [390, 844]]) {
    await page.setViewportSize({ width, height });
    await page.goto('./');
    expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);
    const footer = await page.locator('footer').boundingBox();
    const contentHeight = await page.evaluate(() => document.documentElement.scrollHeight);
    expect(Math.abs(footer.y + footer.height - Math.max(height, contentHeight))).toBeLessThan(2);
  }
});


test('pasting a clipboard image recognizes automatically and ignores images while busy', async ({ page, context }) => {
  await context.grantPermissions(['clipboard-read', 'clipboard-write']);
  await page.goto('./');
  const image = (await readFile('out/testdata/id-front.jpg')).toString('base64');
  await page.evaluate(async base64 => {
    const bytes = Uint8Array.from(atob(base64), char => char.charCodeAt(0));
    const bitmap = await createImageBitmap(new Blob([bytes], { type: 'image/jpeg' }));
    const canvas = document.createElement('canvas');
    canvas.width = bitmap.width; canvas.height = bitmap.height;
    canvas.getContext('2d').drawImage(bitmap, 0, 0);
    bitmap.close();
    const png = await new Promise(resolve => canvas.toBlob(resolve, 'image/png'));
    await navigator.clipboard.write([new ClipboardItem({ 'image/png': png })]);
  }, image);
  await page.keyboard.press('ControlOrMeta+V');
  await expect(page.locator('#preview')).toBeVisible();
  await expect(page.locator('#type')).toBeDisabled();
  const filename = await page.locator('#filename').textContent();
  await page.evaluate(() => {
    const clipboardData = new DataTransfer();
    clipboardData.items.add(new File(['invalid'], 'busy.png', { type: 'image/png' }));
    document.dispatchEvent(new ClipboardEvent('paste', { clipboardData, bubbles: true, cancelable: true }));
  });
  await expect(page.locator('#filename')).toHaveText(filename);
  await expect(page.locator('#message')).toHaveText('识别完成，请核对字段内容', { timeout: 150000 });
  await expect(page.locator('#fields')).toContainText('刘洋');
  await expect(page.locator('#fields')).toContainText('440305199505132561');
});

test('pasting leaves plain text alone and validates clipboard images', async ({ page }) => {
  await page.goto('./');
  const allowed = await page.evaluate(() => {
    const clipboardData = new DataTransfer();
    clipboardData.setData('text/plain', '证件信息');
    return document.dispatchEvent(new ClipboardEvent('paste', { clipboardData, bubbles: true, cancelable: true }));
  });
  expect(allowed).toBe(true);
  await expect(page.locator('#preview')).toBeHidden();
  await expect(page.locator('#message')).toBeHidden();
  await page.evaluate(() => {
    const clipboardData = new DataTransfer();
    clipboardData.items.add(new File(['invalid'], 'unsupported.gif', { type: 'image/gif' }));
    document.dispatchEvent(new ClipboardEvent('paste', { clipboardData, bubbles: true, cancelable: true }));
  });
  await expect(page.locator('#message')).toHaveText('请选择 20 MB 以内的 JPEG 或 PNG 图片');
  await expect(page.locator('#preview')).toBeHidden();
  await page.evaluate(async () => {
    const canvas = document.createElement('canvas'); canvas.width = 10; canvas.height = 10;
    const blob = await new Promise(resolve => canvas.toBlob(resolve, 'image/png'));
    const clipboardData = new DataTransfer();
    clipboardData.items.add(new File([blob], 'small.png', { type: 'image/png' }));
    document.dispatchEvent(new ClipboardEvent('paste', { clipboardData, bubbles: true, cancelable: true }));
  });
  await expect(page.locator('#message')).toHaveText('图片无效，请选择 64–8192 像素、2400 万像素以内的图片');
  await expect(page.locator('#preview')).toBeHidden();
});

for (const width of [1280, 390]) test(`preselects a document, links fields to image crops, copies and clears results (${width}px)`, async ({ page, context }) => {
  await page.setViewportSize({ width, height: 900 });
  await context.grantPermissions(['clipboard-read', 'clipboard-write']);
  await page.goto('./');
  const panels = page.locator('.workspace .panel');
  const initialHeights = await panels.evaluateAll(nodes => nodes.map(node => node.getBoundingClientRect().height));
  const stablePanels = async () => expect(await panels.evaluateAll(nodes => nodes.map(node => node.getBoundingClientRect().height))).toEqual(initialHeights);
  await page.locator('#type').selectOption('driver_license');
  await expect(page.locator('#side-control')).toBeHidden();
  await expect(page.locator('#message')).toBeHidden();
  await page.locator('#file').setInputFiles(width === 390
    ? { name: 'driver-rotated.jpg', mimeType: 'image/jpeg', buffer: await sharp('out/testdata/driver.jpg').rotate(90).toBuffer() }
    : 'out/testdata/driver.jpg');
  await expect(page.locator('#output')).toBeVisible({ timeout: 150000 });
  await expect(page.locator('#fields > div')).toHaveCount(9);
  await stablePanels();
  await expect(page.locator('#fields')).not.toContainText('置信分数');
  await page.getByRole('button', { name: '姓名', exact: true }).click();
  await expect(page.locator('#field-focus')).toBeVisible();
  await expect(page.locator('#focus-label')).toContainText('姓名');
  await expect(page.getByRole('button', { name: '姓名', exact: true })).toHaveAttribute('aria-pressed', 'true');
  await stablePanels();
  await page.locator('#field-focus').evaluate(element => Promise.all(element.getAnimations({ subtree: true }).map(animation => animation.finished)));
  const preview = await page.locator('#preview').boundingBox(), overlay = await page.locator('#field-focus').boundingBox();
  expect(overlay.x).toBeGreaterThanOrEqual(preview.x);
  expect(overlay.y).toBeGreaterThanOrEqual(preview.y);
  expect(overlay.y + overlay.height).toBeLessThanOrEqual(preview.y + preview.height + 1);
  await expect(page.locator('#focus-close')).toBeFocused();
  const hint = await page.locator('#focus-hint').boundingBox();
  expect(hint.y + hint.height).toBeLessThanOrEqual(preview.y + preview.height);
  const zoom = await page.locator('#focus-canvas').boundingBox();
  expect(zoom.y + zoom.height).toBeLessThanOrEqual(hint.y);
  await page.screenshot({ path: `out/validation/field-overlay-${width}.png`, fullPage: true });
  await page.getByRole('button', { name: '姓名', exact: true }).click();
  await expect(page.locator('#field-focus')).toBeHidden();
  await expect(page.getByRole('button', { name: '姓名', exact: true })).toHaveAttribute('aria-pressed', 'false');
  await page.getByRole('button', { name: '姓名', exact: true }).click();
  await page.getByRole('button', { name: '性别', exact: true }).click();
  await expect(page.locator('#focus-label')).toContainText('性别');
  await page.locator('#focus-close').click();
  await expect(page.locator('#field-focus')).toBeHidden();
  await expect(page.getByRole('button', { name: '性别', exact: true })).toBeFocused();
  await page.getByRole('button', { name: '姓名', exact: true }).click();
  await page.keyboard.press('Escape');
  await expect(page.locator('#field-focus')).toBeHidden();
  await expect(page.getByRole('button', { name: '姓名', exact: true })).toBeFocused();
  await page.getByRole('button', { name: '姓名', exact: true }).click();
  await page.locator('#field-focus').click({ position: { x: 3, y: 3 } });
  await expect(page.locator('#field-focus')).toBeHidden();
  await page.locator('#fields details summary').first().click();
  await stablePanels();
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);

  await page.getByRole('button', { name: '复制姓名', exact: true }).click();
  expect(await page.evaluate(() => navigator.clipboard.readText())).toBe('刘洋');
  await page.locator('#copy-all').click();
  expect(await page.evaluate(() => navigator.clipboard.readText())).toContain('证号：440305198305101408');
  await page.screenshot({ path: 'out/validation/field-review.png', fullPage: true });
  await page.locator('#clear').click();
  await expect(page.locator('#preview')).toBeHidden();
  await expect(page.locator('#field-focus')).toBeHidden();
  await expect(page.locator('#output')).toBeHidden();
  await expect(page.locator('#fields')).toBeEmpty();
  await stablePanels();
});

test('shows missing fields and translates reasons, with a document correction action', async ({ page }) => {
  await page.goto('./');
  await page.locator('#type').selectOption('vehicle_license');
  await page.locator('#file').setInputFiles('out/testdata/id-front.jpg');
  await expect(page.locator('#output')).toBeVisible({ timeout: 150000 });
  await expect(page.locator('#fields > div')).toHaveCount(10);
  await expect(page.locator('#fields')).toContainText('未识别');
  await expect(page.locator('#warnings')).toContainText('图片与所选证件类型或页面不符');
  await expect(page.locator('#switch-type')).toContainText('身份证人像面');
  await page.locator('#switch-type').click();
  await expect(page.locator('#fields')).toContainText('440305199505132561', { timeout: 150000 });
});

test('reloads and recognizes with the network disconnected after offline preparation', async ({ page, context }) => {
  await page.goto('./');
  await expect(page.locator('#offline-state')).toHaveText('资源已缓存，可断网使用', { timeout: 150000 });
  await page.waitForFunction(() => Boolean(navigator.serviceWorker.controller));
  await page.evaluate(async () => {
    await navigator.serviceWorker.ready;
    const name = (await caches.keys()).find(name => name.startsWith('cardlens-offline-'));
    const cache = await caches.open(name);
    await cache.put(new URL('./sdk/models/det.bin', location.href), new Response('damaged'));
    const { loadModels } = await import('./sdk/model-loader.js');
    await loadModels(new URL('./sdk/models/', location.href), () => {});
  });
  await context.setOffline(true);
  try {
    await page.reload();
    await expect(page.locator('#controls')).toBeVisible();
    await page.locator('#side').selectOption('emblem');
    await page.locator('#file').setInputFiles('out/testdata/id-back.jpg');
    await expect(page.locator('#fields')).toContainText('2018.08.12-2038.08.12', { timeout: 150000 });
    await expect(page.locator('#message')).not.toHaveClass(/error/);
  } finally { await context.setOffline(false); }
});

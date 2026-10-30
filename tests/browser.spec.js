import { test, expect } from '@playwright/test';
import { readFile } from 'node:fs/promises';

test('recognizes four document sides locally and exports JSON', async ({ page }) => {
  const errors = [], uploads = [];
  page.on('pageerror', error => errors.push(error.message));
  page.on('request', request => { if (!['GET', 'HEAD'].includes(request.method())) uploads.push(request.url()); });
  await page.goto('./');
  await expect(page.getByRole('heading', { name: '从一张证件，到清晰的信息。' })).toBeVisible();
  const gold = JSON.parse(await readFile('tests/gold.json', 'utf8'));
  for (const item of gold.cases) {
    await page.locator('#type').selectOption(item.type);
    await page.locator('#side').selectOption(item.side);
    await page.locator('#file').setInputFiles(`out/testdata/${item.file}`);
    await page.locator('#recognize').click();
    await expect(page.locator('#message')).toHaveText('识别完成，请核对字段内容', { timeout: 150000 });
    for (const value of Object.values(item.expected)) await expect(page.locator('#fields')).toContainText(value);
    const downloadEvent = page.waitForEvent('download');
    await page.locator('#download').click();
    const download = await downloadEvent;
    const result = JSON.parse(await readFile(await download.path(), 'utf8'));
    expect(result.documentType).toBe(item.type);
    for (const [key, value] of Object.entries(item.expected)) expect(result.fields[key].value).toBe(value);
  }
  expect(uploads).toEqual([]);
  expect(errors).toEqual([]);
});

test('responsive layout and image validation', async ({ page }) => {
  await page.setViewportSize({ width: 390, height: 844 });
  await page.goto('./');
  await expect(page.locator('#dropzone')).toBeVisible();
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);
  await page.locator('#file').setInputFiles({ name: 'bad.txt', mimeType: 'text/plain', buffer: Buffer.from('invalid') });
  await expect(page.locator('#message')).toHaveClass(/error/);
  await expect(page.locator('#recognize')).toBeDisabled();
});

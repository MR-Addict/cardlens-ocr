import { defineConfig } from '@playwright/test';
export default defineConfig({
  testDir: './tests', testMatch: 'browser.spec.js', timeout: 180000,
  outputDir: 'out/browser-results', workers: 1,
  use: { baseURL: process.env.CARDLENS_DEMO_URL || 'http://127.0.0.1:4173', headless: true },
  webServer: process.env.CARDLENS_DEMO_URL ? undefined : {
    command: 'python3 -m http.server 4173 --bind 127.0.0.1 --directory out/site',
    url: 'http://127.0.0.1:4173', reuseExistingServer: false
  }
});

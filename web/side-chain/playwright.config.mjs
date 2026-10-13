// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Side-Chain's page in a real browser, at the two widths the manager gives
 * it: a desktop's 760 -- the window's own design width -- and a phone's 400,
 * where the window is scaled to fit.
 *
 *   npx playwright test --config web/side-chain/playwright.config.mjs
 *
 * TWO SERVERS, EACH ON THIS EDITOR'S OWN PORT (vite.config.js):
 *
 *   47343  vite dev, for harness.html: the editor against a fake Move over
 *          the real engine (e2e/editor.spec.mjs)
 *   47344  vite preview of build/web/side-chain, the page as it ships
 *          (e2e/built.spec.mjs) -- so `scripts/build-web.sh side-chain`
 *          comes first, and preview refuses to start without its output
 *
 * Google Chrome, not the bundled Chromium: the browser on the desk is the one
 * the manager is opened in. Results go under build/, which is ignored.
 */
import { defineConfig } from '@playwright/test';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
const ROOT = join(HERE, '..', '..');
const CONFIG = '--config web/side-chain/vite.config.js --logLevel warn';

export default defineConfig({
  testDir: './e2e',
  outputDir: join(ROOT, 'build', 'e2e', 'side-chain'),
  reporter: 'list',
  timeout: 30_000,
  expect: { timeout: 5_000 },
  use: {
    channel: 'chrome',
    baseURL: 'http://localhost:47343',
  },
  projects: [
    { name: 'desktop', use: { viewport: { width: 760, height: 900 } } },
    { name: 'phone', use: { viewport: { width: 400, height: 860 }, hasTouch: true } },
  ],
  webServer: [
    {
      command: `npx vite ${CONFIG}`,
      cwd: ROOT,
      url: 'http://localhost:47343/harness.html',
      reuseExistingServer: true,
    },
    {
      command: `npx vite preview ${CONFIG}`,
      cwd: ROOT,
      url: 'http://localhost:47344/web_ui.html',
      reuseExistingServer: true,
    },
  ],
});

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Trance Gate's page, end to end, in Chrome.
 *
 *   npx playwright test --config web/trance-gate/playwright.config.mjs
 *
 * Two servers, on this editor's own ports so no other agent's or editor's
 * collide with them:
 *
 *   47341  `vite`, the development server: harness.html, the editor against
 *          the fake Move with the real engine (e2e/editor.spec.mjs)
 *   47342  `vite preview` of the BUILT page, build/web/trance-gate -- so
 *          scripts/build-web.sh trance-gate comes first (e2e/built.spec.mjs)
 *
 * Two windows: a desktop browser, and a phone's 400 px, which is how a Move's
 * owner most often opens the manager.
 */
import { defineConfig } from '@playwright/test';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const HERE = dirname(fileURLToPath(import.meta.url));
const ROOT = join(HERE, '..', '..');
const CONFIG = 'web/trance-gate/vite.config.js';

export default defineConfig({
  testDir: join(HERE, 'e2e'),
  outputDir: join(ROOT, 'build', 'test-results', 'web-trance-gate'),
  snapshotPathTemplate: '{testDir}/__screenshots__/{testFilePath}/{arg}-{projectName}{ext}',
  fullyParallel: true,
  reporter: [['list'], ['html', { outputFolder: join(ROOT, 'build', 'playwright-report', 'web-trance-gate'), open: 'never' }]],
  use: {
    channel: 'chrome',
    baseURL: 'http://localhost:47341',
  },
  expect: {
    toHaveScreenshot: { maxDiffPixels: 60, animations: 'disabled' },
  },
  projects: [
    { name: 'desktop', use: { viewport: { width: 1100, height: 1000 } } },
    { name: 'phone', use: { viewport: { width: 400, height: 860 }, deviceScaleFactor: 2, hasTouch: true } },
  ],
  webServer: [
    {
      command: `npx vite --config ${CONFIG} --logLevel warn`,
      cwd: ROOT,
      url: 'http://localhost:47341/harness.html',
      reuseExistingServer: !process.env.CI,
    },
    {
      command: `npx vite preview --config ${CONFIG} --logLevel warn`,
      cwd: ROOT,
      url: 'http://localhost:47342/web_ui.html',
      reuseExistingServer: !process.env.CI,
    },
  ],
});

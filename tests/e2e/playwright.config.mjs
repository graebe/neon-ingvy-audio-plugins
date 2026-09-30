/*
 * The editors, end to end, in a real browser against their mock hosts.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * Full tier only (docs/tech/testing.md). Run it with
 *
 *   npm run test:e2e                 or   ctest --test-dir build -L e2e
 *
 * THE INSTALLED GOOGLE CHROME, NOT A DOWNLOADED ONE. `channel: 'chrome'` drives
 * the Chrome already on the machine, so nothing is fetched into
 * ~/Library/Caches and `npx playwright install` is never needed. A machine
 * without Chrome fails here with Playwright's own message naming the channel.
 *
 * DETERMINISTIC BY CONSTRUCTION, not by retries: `retries` is 0 and every test
 * waits on a condition (a message the mock recorded, an attribute) rather than
 * on a sleep. The screenshots run under Playwright's fake clock with Motion
 * off, so the pixels do not depend on how fast the machine is.
 */
import { defineConfig } from '@playwright/test';

const PORT = Number(process.env.NI_E2E_PORT ?? 47219);

export default defineConfig({
  testDir: '.',
  testMatch: '*.spec.mjs',
  globalSetup: './build-harnesses.mjs',
  outputDir: '../../build/e2e/results',
  /* Baselines beside the specs, committed; one set per platform, since fonts
   * rasterise differently between macOS and Linux. */
  snapshotPathTemplate: '{testDir}/__screenshots__/{testFilePath}/{arg}-{platform}{ext}',
  fullyParallel: true,
  workers: 4,
  retries: 0,
  forbidOnly: !!process.env.CI,
  timeout: 30_000,
  expect: {
    timeout: 5_000,
    toHaveScreenshot: {
      /* Small, and a ratio: anti-aliasing may move a handful of pixels on a
       * font-rendering update; a moved control moves thousands. */
      maxDiffPixelRatio: 0.002,
      threshold: 0.1,
      animations: 'disabled',
      caret: 'hide',
    },
  },
  reporter: [
    ['list'],
    ['html', { outputFolder: '../../build/e2e/report', open: 'never' }],
    ['junit', { outputFile: '../../build/e2e/junit.xml' }],
  ],
  use: {
    baseURL: `http://127.0.0.1:${PORT}`,
    channel: 'chrome',
    headless: true,
    deviceScaleFactor: 1,
    trace: 'retain-on-failure',
  },
  webServer: {
    command: `node tests/e2e/serve.mjs ${PORT}`,
    cwd: '../..',
    url: `http://127.0.0.1:${PORT}/tests/e2e/serve.mjs`,
    /* Never someone else's server on this port: fail instead. */
    reuseExistingServer: false,
    timeout: 10_000,
  },
});

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The BUILT page, build/web/trance-gate/web_ui.html, as `vite preview` serves
 * it -- what the module's tarball carries. It loads the manager's bridge from
 * /static/schwung-remote-api.js, which no server here has; the route below
 * serves a small one in its place, with the module's own declaration, so the
 * page's every request but that one is the build's.
 */
import { test, expect } from '@playwright/test';
import { readChainParams } from '../../kit/vite.editor.mjs';

const PAGE = 'http://localhost:47342/web_ui.html';

/* A bridge as the manager's iframe API has it: values pushed under the
 * component, chain_params on request, writes recorded in window.__writes. */
const bridge = (chain) => `
window.__writes = [];
window.schwungRemote = {
  component: 'fx1',
  onParamChange(fn) {
    fn({
      'fx1:ui': '5555:0:16:0.000:125.00:0:0:${'FF'.repeat(16)}:${'01'.repeat(16)}:0',
      'fx1:params': '0:0:0:0:1/16:15:1:1:1.60000002:16:1:16:125:1:0:0:8,16,32,64',
    });
  },
  setParam(key, value) { window.__writes.push([key, String(value)]); },
  getParam: async () => null,
  getChainParams: async () => ${JSON.stringify(chain)},
  getHierarchy: async () => null,
};`;

test('the built page paints on the Move\'s bridge, and a write reaches it', async ({ page }) => {
  const errors = [];
  page.on('pageerror', (e) => errors.push(e.message));
  await page.route('**/static/schwung-remote-api.js', (route) => route.fulfill({
    contentType: 'text/javascript', body: bridge(readChainParams('tg-move')),
  }));
  await page.goto(PAGE);
  await expect(page).toHaveTitle('NI Trance Gate');
  await expect(page.locator('.pad')).toHaveCount(16);
  await expect(page.locator('.pad.on')).toHaveCount(8);
  await expect(page.getByRole('slider', { name: 'Rate' })).toHaveAttribute('aria-valuetext', '1/16');

  await page.locator('.pad[data-step="1"]').click();
  await expect.poll(() => page.evaluate(() => window.__writes))
    .toEqual([['fx1:cursor', '1'], ['fx1:step', 'On'],
              /* A step switched on comes back at its full amount (steps.js). */
              ['fx1:cursor', '1'], ['fx1:step_amount', '1.0000']]);
  expect(errors).toEqual([]);
});

test('without the Move\'s bridge the built page says where it runs', async ({ page }) => {
  await page.route('**/static/schwung-remote-api.js', (route) => route.fulfill({
    contentType: 'text/javascript', body: '',
  }));
  await page.goto(PAGE);
  await expect(page.locator('.no-bridge')).toContainText('runs on a Move');
});

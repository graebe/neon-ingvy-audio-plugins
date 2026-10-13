// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * THE PAGE AS IT SHIPS: build/web/side-chain, served by `vite preview`, with
 * the manager's bridge script answered by the test. The harness proves the
 * editor; this proves the build -- that web_ui.html loads the bridge before
 * itself, that the bundle and its stylesheet are where the page asks for them,
 * and that a control's write leaves through window.schwungRemote.
 *
 * The bridge is a small stand-in for /static/schwung-remote-api.js: the same
 * five calls, the declaration the device would serve, and a fixed set of
 * values and feeds, as the manager pushes them on subscribing. Writes are kept
 * on window.__writes.
 */
import { test, expect } from '@playwright/test';
import { readChainParams } from '../../kit/vite.editor.mjs';

const PAGE = 'http://localhost:47344/web_ui.html';
const BRIDGE_URL = '**/static/schwung-remote-api.js';

const bridgeScript = (chainParams) => `
(function () {
  var chain = ${JSON.stringify(chainParams)};
  var pushed = {
    'fx1:source': '0', 'fx1:rate': '4', 'fx1:depth': '1', 'fx1:delay': '0',
    'fx1:attack': '2', 'fx1:hold': '8', 'fx1:release': '35', 'fx1:curve': '1',
    'fx1:time_mode': '0', 'fx1:channel': '1', 'fx1:trigger_note': '36',
    'fx1:midi_mode': '0', 'fx1:vel_sens': '0',
    'fx1:ui': '0:4:500.000:0.100000:1:3:0.0000:0.00000:0:0:0.100000',
    'fx1:params': '0:4:0:0:2:8:35:1:1:1:36:0:0:-60:0',
    'fx1:stage_ms': '0.000:10.000:40.000:175.000'
  };
  window.__writes = [];
  window.schwungRemote = {
    component: 'fx1',
    onParamChange: function (cb) { setTimeout(function () { cb(pushed); }, 0); },
    setParam: function (key, value) { window.__writes.push([key, String(value)]); },
    getParam: function (key) { return Promise.resolve(pushed[key]); },
    getChainParams: function () { return Promise.resolve(chain); },
    getHierarchy: function () { return Promise.resolve(null); }
  };
})();`;

test('the built page paints from the bridge, and a knob\'s write reaches it', async ({ page }) => {
  const script = bridgeScript(readChainParams('sc-move'));
  await page.route(BRIDGE_URL, (route) => route.fulfill({ contentType: 'text/javascript', body: script }));
  await page.goto(PAGE);

  await expect(page).toHaveTitle('NI Side-Chain');
  await expect(page.locator('.well-caption')).toHaveText('SHAPE   ONE CYCLE, 500 MS   STAGES 225 MS');
  await expect(page.locator('.state')).toContainText('Cycle 1/4');
  await expect(page.locator('.readout').first()).toHaveText('100%');

  const depth = page.locator('.knob-card', { has: page.locator('.knob-label', { hasText: /^Depth$/ }) })
    .locator('.knob');
  const box = await depth.boundingBox();
  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
  await page.mouse.down();
  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2 + 50, { steps: 5 });
  await page.mouse.up();

  const writes = await page.evaluate(() => window.__writes);
  const last = writes.filter(([k]) => k === 'fx1:depth').pop();
  expect(last, 'no write to fx1:depth').toBeTruthy();
  expect(Number(last[1])).toBeCloseTo(0.75, 2);
});

test('without the manager\'s bridge the page says where it works', async ({ page }) => {
  await page.route(BRIDGE_URL, (route) => route.fulfill({ status: 404, body: '' }));
  await page.goto(PAGE);
  await expect(page.locator('.no-bridge')).toContainText('Schwung\'s web manager');
  await expect(page.locator('.window')).toHaveCount(0);
});

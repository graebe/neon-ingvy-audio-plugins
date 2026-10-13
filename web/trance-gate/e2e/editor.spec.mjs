// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The editor on the harness: the page against the kit's fake Move, with the
 * module's real engine behind it (harness.html, src/harness.jsx). What a user
 * does on the page reaches the engine as the module's own writes, and what
 * the Move does reaches the page.
 *
 * `window.__move` is the harness's handle: device(key, value) is a write from
 * the Move's side, `writes` every write the page made, play()/stop() the
 * transport.
 */
import { test, expect } from '@playwright/test';

const writes = (page) => page.evaluate(() => window.__move.writes.slice());
const device = (page, key, value) =>
  page.evaluate(([k, v]) => window.__move.device(k, v), [key, value]);
/* Longer than the manager's slowest push: the heartbeat, every 500 ms. */
const FEED_MS = 700;

/* Every error the page logs, per page: none is allowed. */
const errorsOf = new WeakMap();

test.beforeEach(async ({ page }) => {
  const errors = [];
  errorsOf.set(page, errors);
  page.on('pageerror', (e) => errors.push(e.message));
  page.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
  await page.goto('/harness.html');
  await expect(page.locator('.pad')).toHaveCount(16);
  /* The values have arrived when the Rate reads one. */
  await expect(page.getByRole('slider', { name: 'Rate' })).toHaveAttribute('aria-valuetext', '1/16');
});

test.afterEach(async ({ page }) => {
  expect(errorsOf.get(page)).toEqual([]);
});

test('it paints: the ring, the panels, the plots and the pads', async ({ page }) => {
  await expect(page.locator('svg.ring')).toBeVisible();
  await expect(page.locator('.panel')).toHaveCount(3);
  await expect(page.locator('svg.plot')).toHaveCount(2);
  await expect(page.locator('.ring-centre')).toHaveText('16');
  /* The engine's default pattern: every other step. */
  await expect(page.locator('.pad.on')).toHaveCount(8);
  await expect(page.getByRole('slider', { name: 'Attack' })).toHaveAttribute('aria-valuetext', /ms$/);
});

test('a click on a step writes the cursor and the step, and the step lights', async ({ page }) => {
  const pad = page.locator('.pad[data-step="1"]');
  await expect(pad).not.toHaveClass(/\bon\b/);
  await pad.click();
  await expect(pad).toHaveClass(/\bon\b/);
  expect(await writes(page)).toEqual(expect.arrayContaining([['fx1:cursor', '1'], ['fx1:step', 'On']]));
  const w = await writes(page);
  const at = w.findIndex(([k, v]) => k === 'fx1:step' && v === 'On');
  expect(w[at - 1]).toEqual(['fx1:cursor', '1']);
  /* And the engine has it: the readout keeps it lit. */
  await page.waitForTimeout(FEED_MS);
  await expect(pad).toHaveClass(/\bon\b/);
  expect(await page.evaluate(() => window.__move.get('pattern'))).toBe('5557');
});

test('a knob writes the module\'s value', async ({ page }) => {
  const amount = page.getByRole('slider', { name: 'Amount' });
  await amount.focus();
  await amount.press('ArrowDown');
  await expect.poll(() => writes(page)).toContainEqual(['fx1:amount', '0.99']);
  await expect(amount).toHaveAttribute('aria-valuetext', '99 %');
});

test('an arrow key steps Rate one division, as the plugin\'s knob does', async ({ page }) => {
  const rate = page.getByRole('slider', { name: 'Rate' });
  await rate.focus();
  await rate.press('ArrowRight');
  await expect.poll(() => writes(page)).toContainEqual(['fx1:rate', '1/16T']);
  await expect(rate).toHaveAttribute('aria-valuetext', '1/16T');
});

test('a digit on a numbered pad opens its arrival number with that digit', async ({ page }) => {
  /* A fade under 100 % numbers the steps it introduces. */
  await device(page, 'fade', '0.5');
  const pad = page.locator('.pad[data-step="0"]');
  await expect(pad.locator('.pad-order')).toBeVisible({ timeout: FEED_MS * 2 });
  await pad.focus();
  await page.keyboard.press('Digit3');
  const field = page.getByRole('textbox', { name: 'Arrival of step 1' });
  await expect(field).toHaveValue('3');
  await page.keyboard.press('Enter');
  await expect.poll(() => writes(page)).toEqual(expect.arrayContaining([['fx1:cursor', '0'], ['fx1:step_order', '3']]));
});

test('a slot recalled on the Move brings its own sound, and the editor follows', async ({ page }) => {
  /* Slot 3 gets its own Rate and pattern from the Move's side. */
  await device(page, 'slot', '2');
  await device(page, 'rate', '1/4');
  await device(page, 'pattern', 'F');
  await device(page, 'slot', '0');
  await page.waitForTimeout(FEED_MS);
  const rate = page.getByRole('slider', { name: 'Rate' });
  await expect(rate).toHaveAttribute('aria-valuetext', '1/16');

  await device(page, 'slot', '2');
  await expect(page.getByRole('combobox', { name: 'Slot' })).toHaveValue('2');
  await expect(rate).toHaveAttribute('aria-valuetext', '1/4');
  await expect(page.locator('.pad.on')).toHaveCount(4);
});

test('Randomize rolls the slot through the Random knob, and leaves it at rest', async ({ page }) => {
  const before = await page.evaluate(() => window.__move.get('pattern'));
  await page.getByRole('button', { name: 'Randomize' }).click();
  const w = await writes(page);
  expect(w.slice(-2)).toEqual([['fx1:randomize', 'Roll'], ['fx1:randomize', 'Hold']]);
  await expect.poll(() => page.evaluate(() => window.__move.get('pattern'))).not.toBe(before);
});

test('the playhead moves between the readouts', async ({ page }) => {
  const step = () => page.locator('.pad.at').getAttribute('data-step');
  const seen = new Set();
  for (let i = 0; i < 8; i++) {
    seen.add(await step());
    await page.waitForTimeout(90);
  }
  /* 125 ms a step: in 0.7 s the playhead crosses five or six pads, and the
   * readouts come every 0.5 s -- so most of what it crossed was carried
   * forward on the clock. */
  expect(seen.size).toBeGreaterThanOrEqual(4);
  await page.evaluate(() => window.__move.stop());
  await page.waitForTimeout(FEED_MS);
  await expect(page.locator('.pad.at')).toHaveCount(0);
});

test('the pattern plot draws the curve the engine rendered', async ({ page }) => {
  const curve = page.locator('.band path.curve');
  await expect(curve).toHaveCount(1);
  const points = async () => ((await curve.getAttribute('d')) ?? '').split(/[ML]/).length - 1;
  /* One cycle of sixteen steps at 64 samples a step. */
  await expect.poll(points).toBe(16 * 64);
  const before = await curve.getAttribute('d');
  await page.locator('.pad[data-step="1"]').click();
  await expect.poll(() => curve.getAttribute('d')).not.toBe(before);
});

test('nothing reaches past the page\'s width', async ({ page }) => {
  const m = await page.evaluate(() => ({
    doc: document.documentElement.scrollWidth, body: document.body.scrollWidth, inner: innerWidth,
  }));
  expect(m.doc).toBeLessThanOrEqual(m.inner);
  expect(m.body).toBeLessThanOrEqual(m.inner);
});

test('the page is as tall as the window is drawn, so the manager sizes it right', async ({ page }) => {
  const m = await page.evaluate(() => ({
    body: document.body.scrollHeight,
    drawn: Math.ceil(document.querySelector('main.window').getBoundingClientRect().height),
  }));
  expect(Math.abs(m.body - m.drawn)).toBeLessThanOrEqual(1);
  /* A longer pattern grows a row, and the page with it. */
  await device(page, 'length', '31');
  await expect(page.locator('.pad')).toHaveCount(32);
  const grown = await page.evaluate(() => document.body.scrollHeight);
  expect(grown).toBeGreaterThan(m.body);
});

test('the window, at rest', async ({ page }) => {
  await page.evaluate(() => window.__move.stop());
  await page.mouse.move(0, 0);
  await page.waitForTimeout(FEED_MS);
  /* The page as the manager's frame shows it: `.page` is the height it reports. */
  await expect(page.locator('.page')).toHaveScreenshot('editor.png');
});

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The editor against a Move: harness.html, where the kit's fake manager runs
 * the real engine as wasm. Every assertion is about what crosses the bridge
 * -- a write the page makes, a value the device pushes -- or what the page
 * draws from it, never about the page's own state.
 */
import { test, expect } from '@playwright/test';

const DESIGN_W = 760;
const DESIGN_H = 604;

/** The harness, once the module's feeds have painted it. */
async function open(page, query = '') {
  await page.goto(`/harness.html${query}`);
  await expect(page.locator('.well-caption')).toContainText('ONE CYCLE, 500 MS');
  await page.evaluate(() => document.fonts.ready);
}

const knob = (page, label) => page.locator('.knob-card', {
  has: page.locator('.knob-label', { hasText: new RegExp(`^${label}$`) }),
});
const select = (page, label) => page.locator('.select-group', {
  has: page.locator('.select-label', { hasText: new RegExp(`^${label}$`) }),
}).locator('select');
const writesOf = (page, key) => page.evaluate((k) => window.__move.writes.filter(([w]) => w === k), key);
const engine = (page, key) => page.evaluate((k) => window.__move.get(k), key);

/* A press, a move and a release, from the centre of `locator`. */
async function drag(page, locator, dx, dy) {
  const box = await locator.boundingBox();
  const x = box.x + box.width / 2;
  const y = box.y + box.height / 2;
  await page.mouse.move(x, y);
  await page.mouse.down();
  await page.mouse.move(x + dx, y + dy, { steps: 8 });
  await page.mouse.up();
}

test('the editor paints from the fake Move', async ({ page }) => {
  await open(page);
  /* The engine's defaults, as the module declares them. */
  await expect(page.locator('.state')).toContainText('Cycle 1/4');
  await expect(select(page, 'Source')).toHaveValue('0');
  await expect(page.locator('.select-value')).toHaveText(['Cycle', '1/4', 'Exponential']);
  await expect(knob(page, 'Depth').locator('.readout')).toHaveText('100%');
  /* Time reads ms, from the engine's own stage_ms: 2 % of 500 ms is 10. */
  await expect(knob(page, 'Attack').locator('.readout')).toHaveText('10ms');
  await expect(knob(page, 'Release').locator('.readout')).toHaveText('175ms');
  await expect(page.locator('.well-caption')).toHaveText('SHAPE   ONE CYCLE, 500 MS   STAGES 225 MS');
  /* The shape is drawn, and the Move has no Threshold or Lockout to show. */
  expect(await page.locator('.shaper path.intended').getAttribute('d')).toMatch(/^M /);
  await expect(page.locator('.knob-label')).toHaveText(['Depth', 'Delay', 'Attack', 'Hold', 'Release']);
});

test('dragging the bottom handle writes Attack, and its readout follows', async ({ page }) => {
  await open(page);
  const before = Number(await engine(page, 'attack'));
  await drag(page, page.locator('[data-handle="bottom"] circle').nth(1), 40, 0);

  expect((await writesOf(page, 'fx1:attack')).length).toBeGreaterThan(0);
  await expect.poll(async () => Number(await engine(page, 'attack'))).toBeGreaterThan(before);
  /* The readout is the engine's, through the stage_ms feed. */
  const ms = Math.round((Number(await engine(page, 'attack')) / 100) * 500);
  await expect(knob(page, 'Attack').locator('.readout')).toHaveText(`${ms}ms`);
});

test('a knob writes', async ({ page }) => {
  await open(page);
  await drag(page, knob(page, 'Depth').locator('.knob'), 0, 50);
  const writes = await writesOf(page, 'fx1:depth');
  expect(writes.length).toBeGreaterThan(0);
  /* Down is less: 50 of the knob's 200 px travel is a quarter of the range. */
  expect(Number(writes[writes.length - 1][1])).toBeCloseTo(0.75, 2);
  await expect(knob(page, 'Depth').locator('.readout')).toHaveText('75%');
  expect(Number(await engine(page, 'depth'))).toBeCloseTo(0.75, 2);
});

test('a knob turned on the Move moves the knob on the page', async ({ page }) => {
  await open(page);
  await page.evaluate(() => window.__move.device('depth', '0.25'));
  await expect(knob(page, 'Depth').locator('.readout')).toHaveText('25%');
  await expect(knob(page, 'Depth').locator('.knob')).toHaveAttribute('aria-valuenow', '0.25');
  /* A device write is not one of the page's. */
  expect(await writesOf(page, 'fx1:depth')).toEqual([]);
});

test('switching the source to MIDI shows the MIDI controls', async ({ page }) => {
  await open(page);
  await select(page, 'Source').selectOption('1');

  expect(await writesOf(page, 'fx1:source')).toEqual([['fx1:source', '1']]);
  expect(await engine(page, 'source')).toBe('1');
  await expect(page.locator('.knob-label')).toHaveText(
    ['Depth', 'Delay', 'Attack', 'Hold', 'Release', 'Note', 'Channel', 'Velocity']);
  await expect(knob(page, 'Note').locator('.readout')).toHaveText('C1');
  await expect(knob(page, 'Channel').locator('.readout')).toHaveText('1');
  await expect(page.locator('.switch-label', { hasText: /^Gate$/ })).toBeVisible();
  await expect(select(page, 'Rate')).toHaveCount(0);
  await expect(page.locator('.state')).toContainText('MIDI');
  /* No note has come: after most of a second, the window says so. */
  await expect(page.locator('.warn')).toHaveText('no midi');
});

test('an arrow on the Channel knob is the next channel', async ({ page }) => {
  await open(page);
  await select(page, 'Source').selectOption('1');
  await knob(page, 'Channel').locator('.knob').focus();
  await page.keyboard.press('ArrowUp');
  await expect(knob(page, 'Channel').locator('.readout')).toHaveText('2');
  expect(await engine(page, 'channel')).toBe('2');
});

test('the playhead advances with the transport', async ({ page }) => {
  await open(page);
  const sweep = page.locator('.shaper .sweep');
  const first = await sweep.getAttribute('x1');
  await expect.poll(() => sweep.getAttribute('x1')).not.toBe(first);
  const second = await sweep.getAttribute('x1');
  await expect.poll(() => sweep.getAttribute('x1')).not.toBe(second);
});

test('a stopped transport is the window\'s one warning', async ({ page }) => {
  await open(page);
  await page.evaluate(() => window.__move.transport({ running: false }));
  await expect(page.locator('.warn')).toHaveText('transport stopped');
  await page.evaluate(() => window.__move.transport({ running: true }));
  await expect(page.locator('.warn')).toHaveCount(0);
});

test('nothing overflows sideways, and the page is as tall as the window it shows', async ({ page }) => {
  await open(page);
  const m = await page.evaluate(() => ({
    width: window.innerWidth,
    docScroll: document.documentElement.scrollWidth,
    docClient: document.documentElement.clientWidth,
    bodyScroll: document.body.scrollWidth,
    /* What the manager's bridge measures and reports as the frame's height. */
    bodyHeight: document.body.scrollHeight,
  }));
  expect(m.docScroll).toBeLessThanOrEqual(m.docClient);
  expect(m.bodyScroll).toBeLessThanOrEqual(m.docClient);
  expect(m.bodyHeight).toBe(Math.ceil(DESIGN_H * (m.width / DESIGN_W)));
});

test('the window, as it looks', async ({ page }) => {
  /* Stopped at the top of the song: nothing moves, so the picture is fixed. */
  await open(page, '?stopped');
  await expect(page.locator('.warn')).toHaveText('transport stopped');
  /* The window's box, which is what the manager's frame shows of the page. */
  await expect(page.locator('.fit')).toHaveScreenshot('editor.png', { maxDiffPixels: 150 });
});

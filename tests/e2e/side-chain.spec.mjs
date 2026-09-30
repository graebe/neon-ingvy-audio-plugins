/*
 * NI Side-Chain's editor, end to end against its mock host.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * The mock's shape is Delay 4, Attack 8, Hold 14, Release 45 (percent of the
 * cycle) at Depth 0.85. Unlike the Trance Gate's, this mock echoes an edit back
 * as the engine's `params` readout, so a dragged handle is seen to move the
 * drawing -- which is what makes the handle tests below meaningful.
 */
import {
  test, expect, open, openForScreenshot, sent, texts, writes, clearSent,
  pushParam, dragVertically, SHELL,
} from './harness.mjs';

const W = 760, H = 604;
/* lib/msg.js */
const P = { delay: 3, attack: 4, hold: 5, release: 6, depth: 7 };

test.use({ viewport: { width: W, height: H } });

const knob = (page, name) => page.locator(`svg.knob[aria-label="${name}"]`);
const handle = (page, name) => page.locator(`g.handle[aria-label="${name}"]`);

/* The messages about some parameters, in order, as "<kind> <idx>". */
const gestures = async (page, idxs) => (await sent(page))
  .filter((m) => idxs.includes(m.paramIdx))
  .map((m) => `${m.msg} ${m.paramIdx}`);

test('loads without a console error and completes the ready handshake', async ({ page, errors }) => {
  await open(page, 'side-chain');
  await expect(knob(page, 'Depth')).toHaveAttribute('aria-valuetext', '85.0 %');
  await expect(handle(page, 'Release')).toHaveAttribute('aria-valuetext', '45.0 %');
  await expect(page.locator('.state')).toContainText('Cycle 1/4');
  expect((await sent(page)).filter((m) => m.msgTag === SHELL.ready)).toHaveLength(1);
  expect(errors).toEqual([]);
});

test('a double-click resets a knob to the default the plugin sent', async ({ page }) => {
  await open(page, 'side-chain');
  await clearSent(page);
  /* The second press of a double-click as WebKit reports it: see the Trance
   * Gate's spec -- Chrome leaves pointerdown's `detail` at 0. */
  await knob(page, 'Depth').dispatchEvent('pointerdown', { detail: 2, bubbles: true, pointerType: 'mouse' });
  expect(await writes(page, P.depth)).toEqual([1]);
});

test('a knob drag is one gesture, and a host echo mid-drag does not move it', async ({ page }) => {
  await open(page, 'side-chain');
  const depth = knob(page, 'Depth');
  await clearSent(page);
  await dragVertically(page, depth, 20, {
    during: () => pushParam(page, P.depth, 0.1),
  });
  const kinds = (await sent(page)).filter((m) => m.paramIdx === P.depth).map((m) => m.msg);
  expect(kinds[0]).toBe('BPCFUI');
  expect(kinds.at(-1)).toBe('EPCFUI');
  /* 20 px down of 200: 0.85 -> 0.75, and the echo of 0.1 did not land. */
  expect((await writes(page, P.depth)).at(-1)).toBeCloseTo(0.75, 5);
  expect(Number(await depth.getAttribute('aria-valuenow'))).toBeCloseTo(0.75, 5);
  await pushParam(page, P.depth, 0.2);
  await expect(depth).toHaveAttribute('aria-valuenow', '0.2');
});

test('the readout: one click edits, Enter commits via setText, Escape sends nothing', async ({ page }) => {
  await open(page, 'side-chain');
  const readout = page.getByRole('button', { name: 'Hold value' });
  const field = page.getByRole('textbox', { name: 'Hold' });
  await clearSent(page);

  await readout.click();
  await field.fill('20');
  await field.press('Escape');
  await expect(field).toHaveCount(0);
  expect(await texts(page, SHELL.setText)).toEqual([]);

  await readout.click();
  await field.fill('20');
  await field.press('Enter');
  expect(await texts(page, SHELL.setText)).toEqual([`${P.hold}:20`]);
});

test('the shaper handles are sliders to the keyboard', async ({ page }) => {
  await open(page, 'side-chain');
  await clearSent(page);

  await handle(page, 'Release').focus();
  await page.keyboard.press('ArrowRight');        /* 1 % of the range later */
  await handle(page, 'Attack and depth').focus();
  await page.keyboard.press('ArrowUp');           /* shallower: less depth */
  await page.keyboard.press('ArrowRight');        /* and a longer attack */
  await handle(page, 'Delay').focus();
  await page.keyboard.press('Home');

  const release = await writes(page, P.release);
  expect(release).toHaveLength(1);
  expect(release[0]).toBeCloseTo(45 / 200 + 0.01, 6);
  expect((await writes(page, P.depth))[0]).toBeCloseTo(0.84, 6);
  expect((await writes(page, P.attack))[0]).toBeCloseTo(8 / 200 + 0.01, 6);
  expect(await writes(page, P.delay)).toEqual([0]);
  /* Each key is its own committed edit. */
  expect(await gestures(page, [P.release])).toEqual(['BPCFUI 6', 'SPVFUI 6', 'EPCFUI 6']);
});

test('dragging a handle moves its own stage, as one gesture', async ({ page }) => {
  await open(page, 'side-chain');
  await clearSent(page);
  const hold = handle(page, 'Hold');
  const box = await hold.boundingBox();
  const x = box.x + box.width / 2, y = box.y + box.height / 2;
  await page.mouse.move(x, y);
  await page.mouse.down();
  await page.mouse.move(x + 60, y, { steps: 6 });
  await page.mouse.up();

  const holds = await writes(page, P.hold);
  expect(holds.length).toBeGreaterThan(1);
  expect(holds.at(-1)).toBeGreaterThan(14 / 200);
  /* A handle moves ITS stage: nothing else was written. */
  expect(await writes(page, P.attack)).toEqual([]);
  expect(await writes(page, P.release)).toEqual([]);
  const kinds = await gestures(page, [P.hold]);
  expect(kinds[0]).toBe('BPCFUI 5');
  expect(kinds.at(-1)).toBe('EPCFUI 5');
  /* The mock echoes the engine value, so the drawing followed the pointer. */
  const after = await hold.boundingBox();
  expect(after.x).toBeGreaterThan(box.x + 20);
});

test('the two-axis handle opens and closes both gestures, and resets both', async ({ page }) => {
  await open(page, 'side-chain');
  await clearSent(page);
  const bottom = handle(page, 'Attack and depth');
  const box = await bottom.boundingBox();
  const x = box.x + box.width / 2, y = box.y + box.height / 2;
  await page.mouse.move(x, y);
  await page.mouse.down();
  await page.mouse.move(x + 30, y - 30, { steps: 6 });
  await page.mouse.up();
  const kinds = (await gestures(page, [P.attack, P.depth])).filter((k) => !k.startsWith('SPVFUI'));
  expect(kinds).toEqual(['BPCFUI 4', 'BPCFUI 7', 'EPCFUI 4', 'EPCFUI 7']);
  expect((await writes(page, P.attack)).at(-1)).toBeGreaterThan(8 / 200);

  await clearSent(page);
  await bottom.dispatchEvent('pointerdown', { detail: 2, bubbles: true, pointerType: 'mouse' });
  /* Params.cpp's defaults, as the mock sends them: Attack 2 %, Depth 100 %. */
  expect(await writes(page, P.attack)).toEqual([2 / 200]);
  expect(await writes(page, P.depth)).toEqual([1]);
});

test('the height is re-sent after a viewport resize', async ({ page }) => {
  await open(page, 'side-chain');
  await expect.poll(() => texts(page, SHELL.height)).toEqual([String(H)]);
  await page.setViewportSize({ width: W * 0.75, height: H });
  await expect.poll(() => texts(page, SHELL.height))
    .toEqual([String(H), String(Math.ceil(H * 0.75))]);
});

test('looks as designed in its default state', async ({ page }) => {
  await openForScreenshot(page, 'side-chain', 'side-chain');
  await expect(page).toHaveScreenshot('side-chain.png');
});

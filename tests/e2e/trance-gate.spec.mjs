/*
 * The Trance Gate's editor, end to end against its mock host.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * The mock's pattern is the 5555 mask -- every other step on, steps 2 and 6
 * tied, step 4 at half amount -- so each gesture below starts from a known
 * step. Every assertion is on what the editor TOLD THE PLUGIN, decoded from the
 * mock's record, or on what the page shows.
 */
import {
  test, expect, open, openForScreenshot, sent, texts, writes, clearSent,
  pushParam, dragVertically, SHELL,
} from './harness.mjs';

const W = 824, H = 752;
/* lib/msg.js */
const MSG = { patch: 67, setStep: 96, setDepth: 97, requestPatch: 99 };
const P = { length: 1, timeMode: 4, amount: 6, width: 7, attack: 8 };

test.use({ viewport: { width: W, height: H } });

const pad = (page, i) => page.locator(`.grid [data-step="${i}"]`);

test('loads without a console error and completes the ready handshake', async ({ page, errors }) => {
  await open(page, 'trance-gate');
  /* Values, texts and the pattern all arrive in the reply to `ready`. */
  await expect(page.getByRole('slider', { name: 'Width' })).toHaveAttribute('aria-valuenow', '0.75');
  await expect(page.getByRole('slider', { name: 'Width' })).toHaveAttribute('aria-valuetext', '75.00 %');
  await expect(page.locator('svg.ring')).toHaveAttribute('aria-valuenow', '16');
  await expect(pad(page, 0)).toHaveAttribute('aria-label', 'Step 1, on, 100 %');
  await expect(pad(page, 1)).toHaveAttribute('aria-label', 'Step 2, off');
  await expect(pad(page, 2)).toHaveAttribute('aria-label', 'Step 3, tie, 100 %');
  expect((await sent(page)).filter((m) => m.msgTag === SHELL.ready)).toHaveLength(1);
  expect(errors).toEqual([]);
});

test('a double-click resets a knob to the default the plugin sent', async ({ page }) => {
  await open(page, 'trance-gate');
  const amount = page.getByRole('slider', { name: 'Amount' });
  await expect(amount).toHaveAttribute('aria-valuenow', '0.9');
  await clearSent(page);
  /*
   * THE SECOND PRESS OF A DOUBLE-CLICK, AS THE PLUGIN'S WEBVIEW DELIVERS IT.
   * The knob reads the click count from pointerdown's `detail`, which WebKit
   * (WKWebView, the plugin's editor) fills in and Chrome leaves at 0 -- so a
   * real dblclick in Chrome would reset nothing. See the report on this suite.
   */
  await amount.dispatchEvent('pointerdown', { detail: 2, bubbles: true, pointerType: 'mouse' });
  /* The mock's defaults (tag 113) say Amount is 1 -- not 0, not the session's 0.9. */
  expect(await writes(page, P.amount)).toEqual([1]);
  await expect(amount).toHaveAttribute('aria-valuenow', '1');
});

test('a knob drag is one gesture, and a host echo mid-drag does not move it', async ({ page }) => {
  await open(page, 'trance-gate');
  const width = page.getByRole('slider', { name: 'Width' });
  await clearSent(page);

  let during;
  await dragVertically(page, width, -20, {
    during: async () => {
      during = Number(await width.getAttribute('aria-valuenow'));
      /* The host echoing an OLDER value while the pointer is still down. */
      await pushParam(page, P.width, 0.1);
    },
  });
  const msgs = (await sent(page)).filter((m) => m.paramIdx === P.width);
  expect(msgs[0].msg).toBe('BPCFUI');
  expect(msgs.at(-1).msg).toBe('EPCFUI');
  const values = msgs.filter((m) => m.msg === 'SPVFUI').map((m) => m.value);
  expect(values.length).toBeGreaterThan(1);
  /* 20 px up of the knob's 200 px travel: 0.75 -> 0.85. */
  expect(values.at(-1)).toBeCloseTo(0.85, 5);
  expect(during).toBeGreaterThan(0.75);
  /* The echo was ignored: the knob sits where the drag left it. */
  expect(Number(await width.getAttribute('aria-valuenow'))).toBeCloseTo(0.85, 5);

  /* After the gesture the host is authoritative again. */
  await pushParam(page, P.width, 0.2);
  await expect(width).toHaveAttribute('aria-valuenow', '0.2');
});

test('the readout: one click edits, Enter commits via setText, Escape sends nothing', async ({ page }) => {
  await open(page, 'trance-gate');
  const readout = page.getByRole('button', { name: 'Width value' });
  const field = page.getByRole('textbox', { name: 'Width' });

  await clearSent(page);
  await readout.click();
  await expect(field).toBeFocused();
  await field.fill('40 %');
  await field.press('Escape');
  await expect(field).toHaveCount(0);
  expect(await texts(page, SHELL.setText)).toEqual([]);

  await readout.click();
  await field.fill('40 %');
  await field.press('Enter');
  await expect(field).toHaveCount(0);
  /* "<paramIdx>:<text>" -- the PLUGIN parses it, with the unit. */
  expect(await texts(page, SHELL.setText)).toEqual([`${P.width}:40 %`]);
});

test('the pads: click toggles, shift-click ties, a drag sets the amount', async ({ page }) => {
  await open(page, 'trance-gate');
  await clearSent(page);

  await pad(page, 1).click();                       /* off -> on, at full amount */
  await pad(page, 0).click({ modifiers: ['Shift'] });   /* on -> tie */
  await pad(page, 8).click();                       /* on -> off */
  expect(await texts(page, MSG.setStep)).toEqual(['1:1', '0:2', '8:0']);
  expect(await texts(page, MSG.setDepth)).toEqual(['1:1.0000']);

  /* A vertical drag inside a pad is its amount, measured against the pad. */
  await clearSent(page);
  const box = await pad(page, 3).boundingBox();
  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
  await page.mouse.down();
  await page.mouse.move(box.x + box.width / 2, box.y + box.height * 0.7, { steps: 6 });
  await page.mouse.up();
  const depths = await texts(page, MSG.setDepth);
  const [step, amount] = depths.at(-1).split(':');
  expect(step).toBe('3');
  expect(Number(amount)).toBeCloseTo(0.3, 1);
});

test('the pads from the keyboard: arrows move, Space toggles, Alt+Up raises the amount', async ({ page }) => {
  await open(page, 'trance-gate');
  await clearSent(page);
  await pad(page, 0).focus();
  await page.keyboard.press('ArrowRight');
  await expect(pad(page, 1)).toBeFocused();
  await page.keyboard.press('Space');
  await page.keyboard.press('ArrowDown');           /* one row of 16: nowhere to go */
  await expect(pad(page, 1)).toBeFocused();
  await page.keyboard.press('ArrowRight');
  await page.keyboard.press('ArrowRight');
  await page.keyboard.press('ArrowRight');
  await expect(pad(page, 4)).toBeFocused();
  /* Step 4 is at 0x80 of 255; Alt+Up is a tenth more. */
  await page.keyboard.press('Alt+ArrowUp');
  expect(await texts(page, MSG.setStep)).toEqual(['1:1']);
  expect(await texts(page, MSG.setDepth)).toEqual(['1:1.0000', '4:0.6020']);
});

test('the ring is the Length slider to the keyboard', async ({ page }) => {
  await open(page, 'trance-gate');
  await clearSent(page);
  const ring = page.locator('svg.ring');
  await ring.focus();
  await page.keyboard.press('ArrowUp');
  await page.keyboard.press('PageDown');
  /* 17 steps, then (the mock does not echo) 16 - 4 = 12. */
  expect(await writes(page, P.length)).toEqual([16 / 127, 11 / 127]);
});

test('copy and paste carry the pattern through the clipboard', async ({ page, context }) => {
  await context.grantPermissions(['clipboard-read', 'clipboard-write']);
  await open(page, 'trance-gate');
  await clearSent(page);
  await page.getByRole('button', { name: 'Copy gate config' }).click();
  expect(await texts(page, MSG.requestPatch)).toEqual(['']);
  /* The mock answers with the patch; the editor puts it on the clipboard. */
  await expect.poll(() => page.evaluate(() => navigator.clipboard.readText()))
    .toBe('tg1:slot=0:len=16:steps=5555');

  await page.getByRole('button', { name: 'Paste gate config' }).click();
  await expect.poll(() => texts(page, MSG.patch)).toEqual(['tg1:slot=0:len=16:steps=5555']);
});

test('paste falls back to a field when the clipboard cannot be read', async ({ page }) => {
  await open(page, 'trance-gate');
  await clearSent(page);
  await page.getByRole('button', { name: 'Paste gate config' }).click();
  const field = page.locator('.paste-field');
  await expect(field).toBeFocused();
  await field.evaluate((el) => {
    const data = new DataTransfer();
    data.setData('text', 'tg1:slot=2:len=8:steps=00FF');
    el.dispatchEvent(new ClipboardEvent('paste', { clipboardData: data, bubbles: true, cancelable: true }));
  });
  await expect(field).toHaveCount(0);
  expect(await texts(page, MSG.patch)).toEqual(['tg1:slot=2:len=8:steps=00FF']);
});

test('the stage readouts follow Env Time between ms and %', async ({ page }) => {
  await open(page, 'trance-gate');
  const attack = page.getByRole('button', { name: 'Attack value' });
  await expect(attack.locator('.num')).toHaveText('1.5');
  await expect(attack.locator('.unit')).toHaveText('ms');

  await page.getByRole('combobox', { name: 'Time' }).selectOption({ value: '1' });
  expect(await writes(page, P.timeMode)).toEqual([1]);
  await expect(attack.locator('.num')).toHaveText('1.60');
  await expect(attack.locator('.unit')).toHaveText('%');

  await page.getByRole('combobox', { name: 'Time' }).selectOption({ value: '0' });
  await expect(attack.locator('.unit')).toHaveText('ms');
});

test('the height is re-sent after a viewport resize', async ({ page }) => {
  await open(page, 'trance-gate');
  await expect.poll(() => texts(page, SHELL.height)).toEqual([String(H)]);
  await page.setViewportSize({ width: W * 0.75, height: H });
  await expect.poll(() => texts(page, SHELL.height))
    .toEqual([String(H), String(Math.ceil(H * 0.75))]);
});

test('looks as designed in its default state', async ({ page }) => {
  await openForScreenshot(page, 'trance-gate', 'trance-gate');
  await expect(page).toHaveScreenshot('trance-gate.png');
});

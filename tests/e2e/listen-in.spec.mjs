// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Listen-In's editor, end to end against its mock host.
 *
 * Two controls and a meter -- and the window every editor shares (the height it
 * asks for, the Motion switch, the ground), which is tested here in its
 * simplest setting.
 */
import {
  test, expect, open, openForScreenshot, sent, texts, writes, clearSent,
  pushText, groundHash, freezeClock, SHELL,
} from './harness.mjs';

const W = 360, H = 232;
const MSG = { state: 64, label: 96 };

test.use({ viewport: { width: W, height: H } });

test('loads without a console error and completes the ready handshake', async ({ page, errors }) => {
  await open(page, 'listen-in');
  /* The reply to `ready` is what delivers the state: the status word comes from it. */
  await expect(page.locator('.status')).toHaveText('listening');
  await expect(page.getByRole('combobox', { name: 'Bus' })).toHaveValue('2');
  await expect(page.getByRole('textbox', { name: 'Bus name' })).toHaveValue('Bass');
  /* Exactly once: a second `ready` would make the plugin push everything twice. */
  expect((await sent(page)).filter((m) => m.msgTag === SHELL.ready)).toHaveLength(1);
  expect(errors).toEqual([]);
});

test('the Bus select writes the parameter, normalised over sixteen slots', async ({ page }) => {
  await open(page, 'listen-in');
  await clearSent(page);
  await page.getByRole('combobox', { name: 'Bus' }).selectOption({ value: '9' });   /* slot 10 */
  /* One committed edit: begin, value, end -- one undo step in the host. */
  const kinds = (await sent(page)).map((m) => m.msg);
  expect(kinds).toEqual(['BPCFUI', 'SPVFUI', 'EPCFUI']);
  expect(await writes(page, 0)).toEqual([9 / 15]);
});

test('the name field commits on Enter and abandons on Escape', async ({ page }) => {
  await open(page, 'listen-in');
  const name = page.getByRole('textbox', { name: 'Bus name' });

  await clearSent(page);
  await name.fill('Kick');
  await name.press('Escape');
  await expect(name).toHaveValue('Bass');
  expect(await texts(page, MSG.label)).toEqual([]);

  await name.fill('Kick');
  await name.press('Enter');
  await expect.poll(() => texts(page, MSG.label)).toEqual(['Kick']);
  await expect(name).not.toBeFocused();
});

test('the status is what the plugin reports, and anything but live is a warning', async ({ page }) => {
  await open(page, 'listen-in', '?status=2');
  await expect(page.locator('.status')).toHaveText('slot taken');
  await expect(page.locator('.status')).toHaveClass(/status-warn/);
  await open(page, 'listen-in', '?status=1');
  await expect(page.locator('.status')).toHaveText('listening');
  await expect(page.locator('.status')).not.toHaveClass(/status-warn/);
});

test('the height is re-sent after a viewport resize', async ({ page }) => {
  await open(page, 'listen-in');
  await expect.poll(() => texts(page, SHELL.height)).toEqual([String(H)]);
  /* Three quarters of the design width: the window scales, and asks for a
   * window three quarters as tall. */
  await page.setViewportSize({ width: W * 0.75, height: H });
  await expect.poll(() => texts(page, SHELL.height))
    .toEqual([String(H), String(Math.ceil(H * 0.75))]);
});

test.describe('the ground', () => {
  /* Fake time, so "it did not move" is a statement about a fixed interval
   * rather than about how long the test happened to wait -- and the harness's
   * transport stopped, so the one ring below is the only one
   * (tests/e2e/ground.spec.mjs is the transport's own test). */
  test.beforeEach(async ({ page }) => {
    await freezeClock(page);
    await page.goto('/plugins/listen-in/ui/test/harness/index.html?stopped');
    await page.clock.runFor(500);
  });

  const ring = (page) => pushText(page, SHELL.ground, '1.000');
  const RING_MS = 600;

  test('a ring moves it, and the Motion switch stops it and is remembered', async ({ page }) => {
    const motion = page.getByRole('switch', { name: 'Motion' });
    await expect(motion).toHaveAttribute('aria-checked', 'true');
    const rest = await groundHash(page);

    /* A ring takes a few hundred ms to swell into view: the wavelet's peak is
     * not its onset. */
    await ring(page);
    await page.clock.runFor(RING_MS);
    expect(await groundHash(page)).not.toBe(rest);

    await motion.click();
    await expect(motion).toHaveAttribute('aria-checked', 'false');
    /* Off flattens at once, back to the static design. */
    expect(await groundHash(page)).toBe(rest);
    await ring(page);
    await page.clock.runFor(RING_MS);
    expect(await groundHash(page)).toBe(rest);
    expect(await page.evaluate(() => localStorage.getItem('ultraviolet.motion.listen-in'))).toBe('0');
  });

  test('it keeps moving while the document is hidden', async ({ page }) => {
    /* A host's WebView reports the editor hidden for as long as it is open
     * (tests/e2e/ground.spec.mjs has the whole of that); the ground used to
     * pause on it and so never moved in Live. */
    await page.evaluate(() => {
      Object.defineProperty(document, 'hidden', { configurable: true, get: () => true });
      Object.defineProperty(document, 'visibilityState', { configurable: true, get: () => 'hidden' });
      document.dispatchEvent(new Event('visibilitychange'));
    });
    const before = await groundHash(page);
    await ring(page);
    await page.clock.runFor(RING_MS);
    const during = await groundHash(page);
    expect(during).not.toBe(before);
    await page.clock.runFor(RING_MS);
    expect(await groundHash(page)).not.toBe(during);
  });
});

test('looks as designed in its default state', async ({ page }) => {
  await openForScreenshot(page, 'listen-in', 'listen-in');
  await expect(page).toHaveScreenshot('listen-in.png');
});

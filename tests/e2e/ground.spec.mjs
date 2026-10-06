/*
 * The animated ground keeps the song's time, in every editor.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * Each harness plays a host transport for its ground (ui-kit/harness/beat.js):
 * a ring a beat at the query's tempo, "1.000" on each downbeat and "0.400"
 * otherwise -- the messages the plugin sends from its beat clock. So each
 * editor is held to the three things a person sees:
 *
 *   - with the transport playing, the ground moves over time;
 *   - with it stopped, the ground is still, and T starts it;
 *   - with Motion off, it is still whatever the transport does.
 *
 * Under Playwright's paused clock, so "it did not move" is a statement about a
 * fixed interval of simulated time rather than about how long a test waited.
 * The plugin's half -- that a playing host really produces these messages,
 * silent track or not -- is tests/au_ground.m, against the built bundles.
 */
import { test, expect, harnessUrl, groundHash, freezeClock } from './harness.mjs';

const EDITORS = [
  { plugin: 'trance-gate', W: 824, H: 752 },
  { plugin: 'side-chain', W: 760, H: 604 },
  { plugin: 'spectrogram', W: 720, H: 502 },
  { plugin: 'listen-in', W: 360, H: 232 },
];

const openAt = async (page, plugin, query) => {
  await freezeClock(page);
  await page.goto(harnessUrl(plugin, query));
  await page.clock.runFor(300);
};
const rings = (page) => page.evaluate(() => window.__transport.rings.slice());

for (const { plugin, W, H } of EDITORS) {
  test.describe(plugin, () => {
    test.use({ viewport: { width: W, height: H } });

    test('a playing transport rings every beat, the downbeat strongest, and the ground moves', async ({ page, errors }) => {
      await openAt(page, plugin, '?bpm=120');
      const before = await groundHash(page);
      await page.clock.runFor(1800);
      /* 0 .. 2.1 s at 120 BPM: beats 0 1 2 3 and bar 2's downbeat. */
      expect(await rings(page)).toEqual([1, 0.4, 0.4, 0.4, 1]);
      const after = await groundHash(page);
      expect(after).not.toBe(before);
      await page.clock.runFor(400);
      expect(await groundHash(page)).not.toBe(after);
      expect(errors).toEqual([]);
    });

    test('a stopped transport leaves the ground at rest, and starting it moves it', async ({ page }) => {
      await openAt(page, plugin, '?stopped');
      const rest = await groundHash(page);
      await page.clock.runFor(3000);
      expect(await rings(page)).toEqual([]);
      expect(await groundHash(page)).toBe(rest);

      await page.locator('body').press('t');
      await page.clock.runFor(800);
      expect((await rings(page))[0]).toBe(1);
      expect(await groundHash(page)).not.toBe(rest);
    });

    test('Motion off keeps the ground still while the transport plays', async ({ page }) => {
      await openAt(page, plugin, '?bpm=140');
      const motion = page.getByRole('switch', { name: 'Motion' });
      await motion.click();
      await expect(motion).toHaveAttribute('aria-checked', 'false');
      const still = await groundHash(page);
      await page.clock.runFor(3000);
      expect((await rings(page)).length).toBeGreaterThan(5);
      expect(await groundHash(page)).toBe(still);
    });
  });
}

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
 *   - with Motion off, it is still whatever the transport does;
 *   - and it moves on the plugin's frame ticks in a page that is hidden,
 *     gets no animation frame and has its timers throttled, which is how
 *     WebKit shows a plugin editor in a real host.
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

/*
 * WHAT A HOST'S WEBVIEW DOES TO THE PAGE, installed before it loads: the
 * document reports hidden, requestAnimationFrame never calls back, and the
 * page's own timers fire no more often than every 400 ms. In Live a plugin
 * editor is hidden for as long as it is open, gets about one animation frame
 * in three seconds and has its timers throttled to a few hertz -- while the
 * plugin's messages arrive at once. A ground clocked by frames or timers, or
 * one that paused while hidden, never moved there.
 */
const asAHostShowsIt = (page) => page.addInitScript(() => {
  window.requestAnimationFrame = () => 0;
  window.cancelAnimationFrame = () => {};
  Object.defineProperty(document, 'hidden', { configurable: true, get: () => true });
  Object.defineProperty(document, 'visibilityState', { configurable: true, get: () => 'hidden' });
  const throttle = (timer) => (fn, ms, ...rest) => timer(fn, Math.max(Number(ms) || 0, 400), ...rest);
  window.setInterval = throttle(window.setInterval.bind(window));
  window.setTimeout = throttle(window.setTimeout.bind(window));
});

/* What the plugin sends: one ring, and a frame tick every 20 ms -- from the
 * test, as a plugin's come from outside the page. */
const ring = (page, s) => page.evaluate((text) => globalThis.SAMFD(112, text.length, btoa(text)), s);
const frameTick = (page) => page.evaluate(() => globalThis.SAMFD(114, 0, ''));

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

    test('in a hidden, throttled page with no animation frames, the plugin\'s ticks move it', async ({ page, errors }) => {
      await asAHostShowsIt(page);
      await page.goto(harnessUrl(plugin, '?stopped'));
      expect(await page.evaluate(() => document.hidden)).toBe(true);
      await page.waitForFunction(() => document.readyState === 'complete');
      const rest = await groundHash(page);

      await ring(page, '1.000');
      /* The editor asks the plugin for frame ticks... */
      await expect.poll(() => page.evaluate(() => window.__transport.groundRunning)).toBe(true);
      /* Without them the field barely moves: its only other clock is the
       * page's timer, which this page fires every 400 ms at best. */
      let idle = await groundHash(page);
      let idleChanges = 0;
      for (let i = 0; i < 10; i++) {
        await page.waitForTimeout(20);
        const h = await groundHash(page);
        if (h !== idle) idleChanges++;
        idle = h;
      }
      expect(idleChanges).toBeLessThanOrEqual(2);
      /* ... and each tick moves the field. A ring takes a few frames to swell
       * into the dots' levels, so ten ticks first; then, over sixteen more,
       * the picture changes from tick to tick, where a 400 ms timer could have
       * stepped it once at most. */
      for (let i = 0; i < 10; i++) {
        await page.waitForTimeout(20);
        await frameTick(page);
      }
      let last = await groundHash(page);
      expect(last).not.toBe(rest);
      let changes = 0;
      for (let i = 0; i < 16; i++) {
        await page.waitForTimeout(20);
        await frameTick(page);
        const h = await groundHash(page);
        if (h !== last) changes++;
        last = h;
      }
      expect(changes).toBeGreaterThanOrEqual(12);
      expect(errors).toEqual([]);
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

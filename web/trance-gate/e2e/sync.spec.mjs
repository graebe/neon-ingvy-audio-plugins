// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The playhead through a network that delays every readout differently
 * (harness.html?jitter: 20 to 400 ms, as on a Move over WiFi). Sampled every
 * frame for three seconds at 1/16 and 120 bpm -- a step every 125 ms -- the
 * lit pad must only ever move one step forward, never back and never in a
 * lurch, and stay on the engine's own step. The engine stamps each readout
 * with its time (`devms`) and the page carries the playhead from that, not
 * from when the readout arrived.
 */
import { test, expect } from '@playwright/test';

test('through a jittery network the playhead steps forward, one pad at a time, on the beat', async ({ page }) => {
  await page.goto('/harness.html?jitter');
  await expect(page.getByRole('slider', { name: 'Rate' })).toHaveAttribute('aria-valuetext', '1/16', { timeout: 10000 });
  await page.waitForTimeout(3000);              /* a few readouts: the clocks agree */
  const samples = await page.evaluate(() => new Promise((done) => {
    const out = [];
    const end = performance.now() + 3000;
    const frame = (now) => {
      const at = document.querySelector('.pad.at');
      const ui = window.__move.get('ui').split(':');
      out.push([at ? Number(at.dataset.step) : -1, Number(ui[3]), Number(ui[2]), now, Number(ui[4])]);
      if (now < end) requestAnimationFrame(frame); else done(out);
    };
    requestAnimationFrame(frame);
  }));
  expect(samples.length).toBeGreaterThan(100);
  const length = samples[0][2];
  let lurches = 0;
  let off = 0;
  for (let i = 1; i < samples.length; i++) {
    const [prev, , , before] = samples[i - 1];
    const [step, phase, , now, msStep] = samples[i];
    const d = (step - prev + length) % length;
    /* Forward only, and no further than the time since the last frame
     * allows: a page the machine starved of frames for longer than a step
     * moves two pads, which is time passing, not a late readout. */
    const allowed = Math.max(1, Math.ceil((now - before) / msStep));
    expect(d <= allowed, `pad ${prev} -> ${step} at frame ${i}, ${(now - before).toFixed(0)} ms later`).toBe(true);
    if (d > allowed) lurches++;
    /* Against the engine's own step at the same moment, give or take the
     * frame either side of a step edge. */
    const truth = Math.floor(phase) % length;
    const e = Math.min((step - truth + length) % length, (truth - step + length) % length);
    if (e > 1) off++;
    expect(e, `pad ${step}, the engine on ${truth}, at frame ${i}`).toBeLessThanOrEqual(1);
  }
  expect(lurches).toBe(0);
  expect(off).toBe(0);
});

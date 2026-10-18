// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The sweep through a network that delays every readout differently
 * (harness.html?jitter: 20 to 400 ms, as on a Move over WiFi). Sampled every
 * frame for three seconds, the playhead line must only move right -- except
 * where the cycle wraps -- and move evenly: no frame's step far off what the
 * time since the last frame accounts for at the sweep's typical speed, which
 * is what a readout arriving late used to cause. The
 * engine stamps each readout with its time (`devms`) and the page carries the
 * sweep from that, not from when the readout arrived.
 */
import { test, expect } from '@playwright/test';

test('through a jittery network the sweep moves evenly and never steps back', async ({ page }) => {
  await page.goto('/harness.html?jitter');
  const sweep = page.locator('.shaper .sweep');
  await expect(sweep).toHaveCount(1, { timeout: 10000 });
  await page.waitForTimeout(3000);              /* a few readouts: the clocks agree */
  const samples = await page.evaluate(() => new Promise((done) => {
    const line = document.querySelector('.shaper .sweep');
    const out = [];
    const end = performance.now() + 3000;
    const frame = (now) => {
      out.push([Number(line.getAttribute('x1')), now]);
      if (now < end) requestAnimationFrame(frame); else done(out);
    };
    requestAnimationFrame(frame);
  }));
  expect(samples.length).toBeGreaterThan(100);
  const xs = samples.map(([x]) => x);
  const width = Math.max(...xs) - Math.min(...xs);
  /* Each step is timed from when the line was first drawn where it left
   * from: the clock re-renders on a timer, not the frame callback (kit
   * lib/clock.js), so a machine that starves the timer holds the line for a
   * few frames and the next draw catches up. That is time passing too. */
  const steps = [];
  let shownAt = samples[0][1];
  for (let i = 1; i < samples.length; i++) {
    const d = samples[i][0] - samples[i - 1][0];
    const dt = samples[i][1] - shownAt;
    if (d !== 0) shownAt = samples[i][1];
    if (d < -width / 2) continue;               /* the cycle wrapped */
    expect(d, `stepped back ${(-d).toFixed(1)} px at frame ${i}`).toBeGreaterThanOrEqual(-0.5);
    if (d !== 0) steps.push([d, dt, i]);
  }
  const speeds = steps.filter(([d, dt]) => d > 0 && dt > 0).map(([d, dt]) => d / dt).sort((a, b) => a - b);
  const speed = speeds[speeds.length >> 1];     /* px per ms */
  /* A step may be up to three times what its time accounts for -- a page
   * the machine starved of frames moves further -- never the jump a late
   * readout made. */
  for (const [d, dt, i] of steps) {
    expect(d, `a ${d.toFixed(1)} px lurch at frame ${i}, ${dt.toFixed(0)} ms later, against ${(speed * dt).toFixed(1)} px`)
      .toBeLessThanOrEqual(speed * dt * 3 + 1);
  }
});

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The sweep through a network that delays every readout differently
 * (harness.html?jitter: 20 to 400 ms, as on a Move over WiFi). Sampled every
 * frame for three seconds, the playhead line must only move right -- except
 * where the cycle wraps -- and move evenly: no frame's step far off the
 * typical one, which is what a readout arriving late used to cause. The
 * engine stamps each readout with its time (`devms`) and the page carries the
 * sweep from that, not from when the readout arrived.
 */
import { test, expect } from '@playwright/test';

test('through a jittery network the sweep moves evenly and never steps back', async ({ page }) => {
  await page.goto('/harness.html?jitter');
  const sweep = page.locator('.shaper .sweep');
  await expect(sweep).toHaveCount(1, { timeout: 10000 });
  await page.waitForTimeout(3000);              /* a few readouts: the clocks agree */
  const xs = await page.evaluate(() => new Promise((done) => {
    const line = document.querySelector('.shaper .sweep');
    const out = [];
    const end = performance.now() + 3000;
    const frame = (now) => {
      out.push(Number(line.getAttribute('x1')));
      if (now < end) requestAnimationFrame(frame); else done(out);
    };
    requestAnimationFrame(frame);
  }));
  expect(xs.length).toBeGreaterThan(100);
  const width = Math.max(...xs) - Math.min(...xs);
  const steps = [];
  for (let i = 1; i < xs.length; i++) {
    const d = xs[i] - xs[i - 1];
    if (d < -width / 2) continue;               /* the cycle wrapped */
    expect(d, `stepped back ${(-d).toFixed(1)} px at frame ${i}`).toBeGreaterThanOrEqual(-0.5);
    steps.push(d);
  }
  const sorted = steps.filter((d) => d > 0).sort((a, b) => a - b);
  const typical = sorted[sorted.length >> 1];
  const lurch = Math.max(...steps);
  /* A frame's step may be a frame's worth more when the display skipped one,
   * never the jump a late readout made. */
  expect(lurch, `a ${lurch.toFixed(1)} px lurch against a typical ${typical.toFixed(1)} px`)
    .toBeLessThanOrEqual(typical * 3 + 1);
});

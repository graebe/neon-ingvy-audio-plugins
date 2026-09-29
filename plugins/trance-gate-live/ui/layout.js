/*
 * The grid's layout and hit-testing, on their own so they can be tested.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * WHY THIS IS NOT IN grid.js, and it is the same argument wire.c makes on
 * the C side: everything in grid.js is a v8ui callback -- paint(), onclick(),
 * ondrag() -- reachable only by loading Max around it. A test that needs Live
 * is a test nobody runs. What moved here is only the arithmetic, and no Max
 * object appears below.
 *
 * THE CONSTRAINT THAT SHAPES ALL OF IT: A LIVE DEVICE IS 169 PIXELS TALL.
 * Fixed, for every device in the chain. The WebView editor is 856 x 660 and
 * lays its pads out 16 to a row at 40px with the ring and both plots beside
 * them; none of that survives the move, so this is a new layout rather than
 * a scaled one.
 *
 * The pads therefore run in ONE ROW across whatever width the device is
 * given, and the pad width falls out of the step count rather than being
 * fixed. A 32-step pattern gets narrower pads, not a second row -- a second
 * row does not fit, and horizontal scrolling is what the M4L production
 * guidelines tell you to spend width on last.
 */

/* The pad strip's own box inside the device, in pixels. Everything else in
 * the patcher is a live.* object and lays itself out. */
export const PADS = { x: 0, y: 0, h: 44, gap: 2, minW: 6 };

/**
 * Where each pad sits, given the strip's width and the pattern's length.
 *
 * THE LAST PAD ENDS ON THE RIGHT EDGE. Dividing the width by the count and
 * rounding each pad to whole pixels leaves a remainder -- up to `length - 1`
 * pixels of it -- and spending that remainder on nothing puts a ragged gap at
 * the right that reads as a rendering bug. So each pad's edges are computed
 * from the exact fraction and rounded, which distributes the remainder across
 * the row and makes the last edge land exactly on `width`.
 */
export function padBoxes(width, length) {
  const n = Math.max(1, length | 0);
  const w = Math.max(0, width);
  const boxes = [];
  for (let i = 0; i < n; i++) {
    const a = Math.round((w * i) / n);
    const b = Math.round((w * (i + 1)) / n);
    boxes.push({ x: a, w: Math.max(PADS.minW, b - a - PADS.gap) });
  }
  return boxes;
}

/**
 * Which step is under `x`, or -1 when none is.
 *
 * COMPUTED FROM THE FRACTION, NOT BY SEARCHING padBoxes. The gap between
 * pads belongs to the step on its left for hit-testing purposes: a click that
 * lands in a 2px gutter should toggle the pad you were aiming at, not be
 * swallowed. Searching the boxes would return -1 there, which reads as the
 * click half-failing -- the exact fault steps.js was written to stop.
 */
export function stepAt(x, width, length) {
  const n = Math.max(1, length | 0);
  if (!(width > 0) || x < 0 || x >= width) return -1;
  const i = Math.floor((x / width) * n);
  return i < 0 ? 0 : i >= n ? n - 1 : i;
}

/**
 * A vertical drag to a depth, 0..1.
 *
 * The pad is lit from the BOTTOM, so a drag upward raises the value and y
 * counts downward -- which is the inversion every step sequencer has and
 * every port gets wrong once.
 *
 * NEVER ZERO. A step dragged to the floor still sounds, at 1/255, because
 * depth 0 and "step off" are different states and a drag must not silently
 * perform the other one. Turning a step off is a click.
 */
export function depthAt(y, h) {
  if (!(h > 0)) return 1;
  const v = 1 - y / h;
  return Math.min(1, Math.max(1 / 255, v));
}

/**
 * The playhead's step, interpolated between readouts.
 *
 * WHY INTERPOLATE AT ALL. The readout is pulled by a qmetro, and a qmetro
 * fast enough to look smooth is a qmetro serialising a 4KB string through
 * the scheduler at that rate. So the anchor is re-read whenever one arrives
 * and the position is carried forward by wall time in between -- the same
 * arrangement App.jsx uses, and for the same reason.
 *
 * `msStep` of 0 means the engine has no tempo yet; the anchor is then the
 * only honest answer.
 */
export function playheadAt(anchor, nowMs) {
  if (!anchor || !anchor.moving) return -1;
  const { phase, msStep, length, at } = anchor;
  const n = Math.max(1, length | 0);
  if (!(msStep > 0)) return Math.floor(phase) % n;
  const steps = (nowMs - at) / msStep;
  const p = (phase + steps) % n;
  return Math.floor(p < 0 ? p + n : p);
}

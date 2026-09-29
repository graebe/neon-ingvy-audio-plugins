/*
 * The fade-in's weights, mirroring the engine exactly.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * A PORT OF Instance::recalc_fade IN crates/tg-core/src/lib.rs, AND THE ONLY
 * COPY IN THE UI.
 *
 * Plain JS, no JSX, no imports -- the same rule curves.js follows and for the
 * same reason: it must agree with the DSP to the digit, so `node --test` has to
 * be able to load it without a build step. See ui/test/fade.test.mjs, which
 * holds it against a table the ENGINE generates.
 *
 * WHY THERE IS A SECOND COPY AT ALL. The editor has to draw what you are about
 * to hear -- which pads have arrived, and how far the one in flight has got --
 * and that is three lines of arithmetic against a table the plugin would
 * otherwise have to push thirty times a second. The rule this codebase actually
 * holds is not "never mirror the DSP", it is "never mirror it unpinned": the
 * envelope is mirrored too, and an oracle generated from the engine is what
 * keeps both honest.
 */

/**
 * The weight of one arrival, 0..1.
 *
 *     w(r) = clamp(f*n - (r-1), 0, 1)        soft
 *     w(r) = w_soft(r) >= 1 ? 1 : 0          hard
 *
 * `rank` is the step's place in the arrival order, 1..n. `n` is how many steps
 * sound in the cycle. `fade` is the knob, 0..1.
 *
 * Rank 0 means a step that is off: it has no place in the order, so it has no
 * weight either, and the caller never has to special-case a gap.
 *
 * THE EPSILON MATCHES THE ENGINE'S. A host can hand over the float nearest 1.0
 * from beneath; without the slack the last arrival would stay silent at the top
 * of the knob, and the picture would say so too.
 */
export function fadeWeight(rank, n, fade, soft) {
  if (!(rank >= 1) || !(n >= 1)) return 0;
  const v = Math.min(1, Math.max(0, fade * n - (rank - 1)));
  if (soft) return v;
  return v >= 1 - 1e-6 ? 1 : 0;
}

/**
 * Every step's weight, indexed by step. `orders` is the `ui` readout's rank per
 * step -- 0 where the step is off -- so the count of arrivals comes out of it
 * rather than being passed in and possibly disagreeing.
 */
export function fadeWeights(orders, length, fade, soft) {
  const n = Math.max(1, length | 0);
  let hits = 0;
  for (let i = 0; i < n; i++) if ((orders?.[i] ?? 0) >= 1) hits++;
  const w = new Array(n).fill(0);
  for (let i = 0; i < n; i++)
    w[i] = fadeWeight(orders?.[i] ?? 0, hits, fade, soft);
  return w;
}

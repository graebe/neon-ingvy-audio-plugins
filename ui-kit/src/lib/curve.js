/*
 * The envelope curves, as the engines shape a stage -- the one copy in the UI.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * A PORT OF ni_dsp::curve (engines/shared), which both engines use. It was two
 * copies -- the Trance Gate's curves.js and Side-Chain's shape.js -- and it is
 * one now, still pinned to the engines' OWN output: each editor's test compares
 * it with the table its engine generates (tg_shape_table, sc_shape_table).
 * Side-Chain's shaper draws with it; the Trance Gate draws engine-rendered
 * curves and needs none.
 *
 * Plain JavaScript with no imports, so `node --test` can load it.
 */

/* The bend, and the divisor. `DENOM` is `1 - exp(-3)` SPELLED OUT, exactly as
 * the Rust and the C spell it, so the division is the same division; the
 * fixtures are compared at 1e-12. */
const K = 3.0;
const DENOM = 0.95021293163213605;

/* Fast, then easing into the target. */
const curveExp = (t) => (1 - Math.exp(-K * t)) / DENOM;

/** 0 Linear, 1 Exponential, 2 S-Curve -- `Curve::LABELS`' order. */
export const CURVES = ['Linear', 'Exponential', 'S-Curve'];

/*
 * THE SHAPE IS A WARP ON TIME: every stage is f(t) with t running 0..1 across
 * it. shape(0) = 0, shape(1) = 1, monotonic -- a stage starts and ends where it
 * did and takes the time it was given; only the path between changes.
 */
export function shape(curve, t) {
  /* `!(t > 0)` so a NaN lands here rather than returning 1 -- the engine's guard. */
  if (!(t > 0)) return 0;
  if (t >= 1) return 1;
  switch (curve) {
    case 1: return curveExp(t);
    /*
     * TWO EXPONENTIALS, JOINED -- and the first one is MIRRORED, so the pair is
     * slow-fast-slow and meets in the middle at the same slope. `0.5 *
     * curveExp(2t)` on both halves left the floor and hit the ceiling
     * vertically, the opposite of an S-curve, for as long as it was written so.
     */
    case 2: return t < 0.5 ? 0.5 * (1 - curveExp(1 - 2 * t))
                           : 0.5 + 0.5 * curveExp(2 * t - 1);
    default: return t;
  }
}

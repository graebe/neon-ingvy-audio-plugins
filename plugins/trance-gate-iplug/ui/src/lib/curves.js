/*
 * The envelope's maths, mirroring the engine exactly.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * A PORT OF crates/tg-core/src/envelope.rs, AND THE ONLY COPY IN THE UI.
 *
 * This is plain JS with no JSX and no imports on purpose: it is the one piece
 * of the editor that must agree with the DSP to the digit, so it has to be
 * loadable by `node --test` without a build step. See ui/test/curves.test.mjs,
 * which checks it against the engine's OWN tg_test_shape() over a fine sweep
 * -- a picture that merely looks plausible is how the S-curve below was wrong
 * for as long as it was.
 *
 * It lived inside Plots.jsx before, where nothing could reach it.
 */

/* The bend, and the divisor. `DENOM` is `1 - exp(-3)` SPELLED OUT, exactly as
 * the Rust and the C spell it, so the division is the same division -- the
 * engine's own comment, and the reason this is not `1 - Math.exp(-K)`. */
const K = 3.0;
const DENOM = 0.95021293163213605;

/* Fast, then easing into the target -- what "exponential envelope" means on
 * hardware. */
const curveExp = (t) => (1 - Math.exp(-K * t)) / DENOM;

const curveExpInv = (w) => {
  const x = 1 - w * DENOM;
  return x <= 1e-12 ? 1 : -Math.log(x) / K;
};

/*
 * THE SHAPE IS A WARP ON TIME: every stage is f(t) with t running 0..1 across
 * it, so a curve is not three new formulas but one function substituted in.
 * shape(0) = 0, shape(1) = 1, monotonic -- a stage starts and ends where it
 * did and takes the time it was given; only the path between changes.
 */
export function shape(curve, t) {
  if (!(t > 0)) return 0;        /* NaN included: the engine's `t <= 0` */
  if (t >= 1) return 1;
  switch (curve) {
    case 1: return curveExp(t);
    /*
     * TWO EXPONENTIALS, JOINED -- and the first one is MIRRORED.
     *
     * This is the line that was wrong. It read `0.5 * curveExp(2t)`, which is
     * the exponential the right way up on both halves: slope K/DENOM ~ 3.16
     * at t=0, so the curve left the floor vertically and hit the ceiling
     * vertically. An S-curve does the opposite of that at both ends.
     *
     * Reversed, the first half is slow-then-accelerating and the pair is
     * slow-fast-slow, meeting in the middle at the same slope -- Einv'(1) is
     * E'(0). A corner there would be a kink in the gain, audible as surely as
     * a step, which is why the join is not simply two arcs.
     */
    case 2: return t < 0.5 ? 0.5 * (1 - curveExp(1 - 2 * t))
                           : 0.5 + 0.5 * curveExp(2 * t - 1);
    default: return t;
  }
}

/* The inverse, which is what lets the curve change mid-gate without a click.
 * Monotonic and analytic for all three. */
export function shapeInv(curve, w) {
  if (!(w > 0)) return 0;
  if (w >= 1) return 1;
  switch (curve) {
    case 1: return curveExpInv(w);
    case 2: return w < 0.5 ? 0.5 * (1 - curveExpInv(1 - 2 * w))
                           : 0.5 + 0.5 * curveExpInv(2 * w - 1);
    default: return w;
  }
}

/*
 * ONE GATE'S LEVEL AT TIME t, t being a fraction of the STEP.
 *
 * The stage machine's three expressions (envelope.rs, Env::advance) with the
 * plot's own starting conditions: an isolated gate, so attack rises from
 * silence (att_from = 0) and release falls from sustain (rel_from = sustain).
 * The engine carries both forward between adjacent steps, which is what stops
 * the click at a step boundary -- a single gate drawn on its own has neither.
 *
 *   ATTACK   att_from + (1 - att_from) * w
 *   DECAY    1        - (1 - sustain)  * w
 *   RELEASE  rel_from * (1 - w)
 *
 * Stage lengths are percentages OF THE GATE'S WIDTH, not of the step.
 */
export function gateAt(p, t) {
  const a = Math.max(0, p.attack) / 100 * p.width;
  const d = Math.max(0, p.decay) / 100 * p.width;
  const r = Math.max(0, p.release) / 100 * p.width;
  const shut = Math.max(a + d, p.width);
  if (t < a)        return shape(p.curve, a > 0 ? t / a : 1);
  if (t < a + d)    return 1 - (1 - p.sustain) * shape(p.curve, d > 0 ? (t - a) / d : 1);
  if (t < shut)     return p.sustain;
  if (t < shut + r) return p.sustain * (1 - shape(p.curve, r > 0 ? (t - shut) / r : 1));
  return 0;
}

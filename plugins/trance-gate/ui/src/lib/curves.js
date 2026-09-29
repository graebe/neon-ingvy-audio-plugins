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
 * THE STAGE MACHINE, ONCE.
 *
 * This existed twice in the UI and the copies disagreed: EnvelopePlot had one
 * in milliseconds that was right, and gateAt had one in step-fractions that
 * was wrong in two ways at once. Both now call these.
 *
 * `e` carries every length in ONE unit, whichever the caller finds convenient
 * -- milliseconds for the envelope plot, fractions of a step for the pattern
 * plot -- because the machine does not care which, only that they agree:
 *
 *     { curve, sustain, attack, decay, release, gate }
 *
 * Verified against the ENGINE ITSELF, not against a reading of it: see
 * tests/envelope_table.c, which drives the real DSP with a DC input at
 * amount 1 so the output sample IS the envelope, and ui/test/envelope.test.mjs,
 * which holds this to it over sixty parameter combinations.
 */

/**
 * Attack, decay, sustain — the envelope AS DIALLED, with no gate close.
 *
 * `e.from` is the engine's `att_from`: the level the envelope was ALREADY at
 * when this gate opened, which the attack ramps away from rather than jumping
 * over. It is 0 for a gate that starts from silence, and that is the common
 * case — but not the only one. When the release is long enough to outlive its
 * step, the previous gate is still sounding when the next one strikes, and the
 * engine picks up from there:
 *
 *     level = att_from + (1 - att_from) * w
 *
 * which is what stops a step boundary being a click. The envelope oracle found
 * this: five of sixty measured cases started at up to 0.1 of full scale while
 * the model insisted on 0, and every one of them had Release at 200%.
 *
 * The attack still REACHES 1 and still takes the time it was given; only where
 * it starts moves. (att_from can exceed 1 in the engine, after a loud step
 * followed by a quiet one — the lerp then ramps DOWN to the new level, which
 * this expresses without a special case.)
 */
export function stageLevel(e, x) {
  const a = Math.max(0, e.attack), d = Math.max(0, e.decay);
  const s = Math.min(1, Math.max(0, e.sustain));
  const from = e.from ?? 0;
  if (x < a) return from + (1 - from) * shape(e.curve, a > 0 ? x / a : 1);
  if (x < a + d) return 1 - (1 - s) * shape(e.curve, d > 0 ? (x - a) / d : 1);
  return s;
}

/**
 * The same machine with the gate closing over it — the gain you actually hear.
 *
 * THE GATE SHUTS AT `gate`, WHATEVER STAGE IS RUNNING. That is the engine's
 * rule (`if r.frac >= self.hold { env.enter(Release) }` in next_gain), and it
 * has no interest in whether the attack or the decay has finished. The old
 * model waited for attack+decay first — so with a long attack against a narrow
 * Width, which the knobs reach easily because a stage runs to 200% of the
 * width, the picture held the gate open past where the engine shut it.
 *
 * AND THE RELEASE FALLS FROM WHEREVER THE MACHINE HAD GOT TO, not from
 * sustain: `enter(Stage::Release)` latches `rel_from = self.level`. Shut the
 * gate half way up a long attack and the release starts from half way up.
 * The old model always released from sustain, so it drew a jump.
 *
 * At Width 100% the engine's in-step release never runs at all and the close
 * happens at the step boundary instead — which is this function with
 * `gate = 1`, needing no special case.
 */
export function envLevel(e, x) {
  const g = e.gate, r = Math.max(0, e.release);
  if (x < g) return stageLevel(e, x);
  const from = stageLevel(e, g);
  if (!(r > 0)) return 0;                  /* a zero-length release is a cut */
  return x < g + r ? from * (1 - shape(e.curve, (x - g) / r)) : 0;
}

/**
 * One gate's level at time `t`, `t` being a fraction of the STEP.
 *
 * The stage percentages are OF THE GATE'S WIDTH, not of the step — the
 * engine's `stage_samples` is `pct * 0.01 * width_ms * ...` — and the width is
 * itself a fraction of the step, so both divisions land here.
 */
export function gateAt(p, t, from = 0) {
  const w = Math.min(1, Math.max(0, p.width));
  const k = w / 100;
  return envLevel({
    curve: p.curve,
    sustain: p.sustain,
    attack: Math.max(0, p.attack) * k,
    decay: Math.max(0, p.decay) * k,
    release: Math.max(0, p.release) * k,
    gate: w,
    from,
  }, t);
}

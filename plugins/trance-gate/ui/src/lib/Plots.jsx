/*
 * The plots. Copyright (c) 2026 Torben Gräber. MIT.
 * Ported from UvComponents.cpp: well(), drawGate(), ScopeView::paint.
 *
 * THE VIEWBOX IS THE PIXEL SIZE, 1:1, and that is not a detail. A fixed
 * viewBox stretched to fit with preserveAspectRatio="none" scales the TEXT
 * too -- the captions came out 2.3x wide and unreadable, which looked like a
 * font problem and was a geometry one.
 */
import { For, createMemo } from 'solid-js';
import { gateAt, envLevel, stageLevel } from './curves.js';


/*
 * plot::inset and plot::captionH, named here as they are named there.
 */
const INSET = 6, CAPTION = 14;

/* plot::well -- bg-000 ground, a line-100 hairline, and the caption in hint
 * style. The content area is inset all round with CAPTION off the top. */
function Well(props) {
  return (
    <svg class="plot" width={props.w} height={props.h}
         viewBox={`0 0 ${props.w} ${props.h}`}>
      <rect x="0.5" y="0.5" width={props.w - 1} height={props.h - 1}
            fill="var(--bg-000)" stroke="var(--line-100)" />
      {props.children}
      {/* `well-caption` as well as `plot-caption`, so this one is reachable on its
        * own: the Axis's tick labels carry plot-caption too and come EARLIER in
        * document order, so a querySelector for the caption found a tick. */}
      {props.caption &&
        <text class="plot-caption well-caption t-hint" x={INSET} y="11">{props.caption}</text>}
    </svg>
  );
}

/*
 * plot::steps, BOTH LAYERS, and they are separate because they are painted at
 * different times: the rules sit UNDER the audio so a transient is never
 * hidden by a rule, and the numbers go over it "so a number is never
 * swallowed by the audio".
 *
 * A rule marks the BOUNDARY between steps -- x = left + w*i for i = 1..n-1 --
 * which is why dividing the whole well by the step count put them in the
 * wrong place.
 */
const RULE_MIN = 6;      /* below this a rule per step is a comb */
const NUMBER_MIN = 14;   /* below this the numbers touch */

function StepRules(props) {
  const cells = () => {
    const n = props.count;
    if (!(n >= 1) || props.w <= 0) return [];
    const w = (props.w - 2 * INSET) / n;
    const perStep = w >= RULE_MIN;
    const out = [];
    for (let i = 1; i < n; i++) {
      const bar = i % 16 === 0, beat = i % 4 === 0;
      /* Bars and beats survive at any density -- they are what keeps a long
       * pattern readable. */
      if (!perStep && !bar && !beat) continue;
      out.push({ x: INSET + w * i, bar });
    }
    return out;
  };
  return (
    <For each={cells()}>{(c) => (
      <line x1={c.x} x2={c.x} y1={props.top} y2={props.bottom}
            stroke={c.bar ? 'var(--line-200)' : 'var(--line-100)'} />
    )}</For>
  );
}

function StepNumbers(props) {
  const cells = () => {
    const n = props.count;
    if (!(n >= 1) || props.w <= 0) return [];
    const w = (props.w - 2 * INSET) / n;
    if (w < NUMBER_MIN) return [];
    return Array.from({ length: n }, (_, i) => ({ i, x: INSET + w * i + 2 }));
  };
  return (
    <For each={cells()}>{(c) => (
      <text class="t-hint step-number" x={c.x} y={props.top + 10}>{c.i + 1}</text>
    )}</For>
  );
}


/*
 * ONE-SIDED, A FILLED REGION RISING FROM THE FLOOR.
 *
 * There were two renderers here: this one, and a MIRRORED `Gate` for drawing
 * the pattern's gate over the scope's waveform. The scope is a rolling window
 * of wall time now and carries no gate overlay, so the mirrored one is gone
 * with it.
 *
 * The distinction is worth keeping in mind if an overlay is ever wanted back:
 * using the mirrored renderer for the envelope is what once made the envelope
 * plot look like a waveform rather than an envelope.
 */

/*
 * plot Curve: `under` filled in uv-glow, the `hull` stroked in uv at rail
 * weight, and below `floor` everything is lit whatever the gate does --
 * because the gate never closes past Amount, and an envelope drawn as though
 * it reached zero would be lying about what you hear.
 */
function Curve(props) {
  const geom = () => {
    const v = props.values;
    if (!v || v.length < 2) return null;
    const top = props.top, bot = props.bottom;
    const floor = Math.min(1, Math.max(0, props.floor ?? 0));
    /* m = floor + (1 - floor)*g is affine in g, so the floor is a squash of
     * the unit box onto the part of the plot above it. */
    const h = (bot - top) * (1 - floor);
    const x = (i) => INSET + (props.w - 2 * INSET) * (i / (v.length - 1));
    const y = (g) => top + h * (1 - Math.min(1, Math.max(0, g)));
    const hull = v.map((g, i) => `${i ? 'L' : 'M'} ${x(i).toFixed(2)} ${y(g).toFixed(2)}`).join(' ');
    return {
      hull,
      under: `${hull} L ${x(v.length - 1).toFixed(2)} ${(top + h).toFixed(2)} L ${x(0).toFixed(2)} ${(top + h).toFixed(2)} Z`,
      floorY: top + h,
      hasFloor: floor > 0,
    };
  };
  return (
    <>
      {geom() && (
        <>
          {geom().hasFloor && (
            <rect x={INSET} y={geom().floorY} width={props.w - 2 * INSET}
                  height={Math.max(0, props.bottom - geom().floorY)}
                  fill="var(--uv-glow)" />
          )}
          <path d={geom().under} fill="var(--uv-glow)" stroke="none" />
          <path d={geom().hull} fill="none" stroke="var(--uv)" stroke-width="2" />
        </>
      )}
    </>
  );
}


/*
 * THE CURVE MATHS IS NOT HERE ANY MORE -- see lib/curves.js.
 *
 * It lived in this file as a private `shape()`, and its S-curve had the first
 * half un-mirrored for as long as it existed: every envelope the plugin drew
 * left the floor vertically and hit the ceiling vertically, which is the
 * opposite of what an S-curve does. Nothing caught it because a wrong curve is
 * still a curve, and nothing COULD catch it because the function was private
 * to a JSX file that no test runner can load.
 *
 * It is now plain JS in its own module, pinned to a table generated by the
 * engine itself -- see ui/test/curves.test.mjs and tests/shape_table.c.
 * gateAt is re-exported because App.jsx has always taken it from here, and
 * because both JS test files import it. PatternPlot no longer calls it: it
 * needs ONE envelope across a whole run of joined steps rather than one per
 * step, so it drives `envLevel` directly with x continuing across the boundary.
 */
export { gateAt };

/*
 * THE ENVELOPE, ON A MILLISECOND AXIS.
 *
 * Not "one step": the span is computed, and the envelope may run PAST the
 * step's edge -- which is the case the amber rule and the ghost curve exist
 * to explain. The arithmetic is TgEnvelopeShape::render's, and all of it is
 * derivable here: the scratch engine the JUCE editor ran was only ever needed
 * for the curve's samples, and env_shape is ported above.
 *
 *   widthMs = hold * msStep              the gate's open time
 *   stageMs = pct * widthMs / 100        a stage is a % OF THE WIDTH
 *   gateMs  = widthMs                    where the gate closes, always
 *   spanMs  = max(gate + r, a + d, msStep, 2) * 1.04
 *
 * The span holds BOTH curves: the gated one ends at gate + release, the ghost
 * runs on to attack + decay. It used to be `max(gate, a+d) + r`, which waits
 * for a release the real envelope never waits for.
 *
 * The 4% of air at the end guarantees the release always lands inside the
 * well rather than on its frame.
 */
export function EnvelopePlot(props) {
  const env = createMemo(() => {
    const p = props.params;
    if (!p || !(p.msStep > 0)) return null;
    const msStep = p.msStep;
    const hold = Math.min(1, Math.max(0, p.width));
    const widthMs = hold * msStep;
    const k = widthMs * 0.01;
    const attackMs = Math.max(0, p.attack) * k;
    const decayMs = Math.max(0, p.decay) * k;
    const releaseMs = Math.max(0, p.release) * k;
    const gateMs = widthMs;
    /*
     * THE AXIS HAS TO HOLD BOTH CURVES, and they end at different places.
     *
     * The gated one ends at gate + release -- the gate shuts at `gate`
     * whatever stage is running, so the release starts THERE and not after
     * attack+decay. The ghost, which is the envelope as dialled, runs on to
     * attack + decay.
     *
     * This was `max(gate, attack+decay) + release`, which is neither: it
     * stretched the axis by a release the real envelope never waits for, so
     * with a long attack against a narrow Width the whole picture shrank into
     * the left of a well that was mostly empty.
     */
    const spanMs = Math.max(gateMs + releaseMs, attackMs + decayMs,
                            msStep, 2) * 1.04;
    return {
      msStep, hold, attackMs, decayMs, releaseMs, gateMs, spanMs,
      sustain: Math.min(1, Math.max(0, p.sustain)), curve: p.curve,
      amount: Math.min(1, Math.max(0, p.amount ?? 1)),
      /* The release ran past the step's end -- the next gate will cut it. */
      truncated: gateMs + releaseMs > msStep + 1e-6,
      /* The gate closed before the decay finished, so the solid curve alone
       * would be a puzzle: you set a long decay and see a spike. */
      early: gateMs < attackMs + decayMs - 1e-6,
    };
  });

  /*
   * THE STAGE MACHINE IS curves.js's, NOT A SECOND ONE HERE.
   *
   * This file had its own copy, in milliseconds. It was the better of the two
   * -- it already released from the level actually reached -- but having two
   * at all is why the pattern plot and this one drew different shapes from the
   * same settings for as long as they did. There is one now, and it is held to
   * the engine's measured output by ui/test/envelope.test.mjs.
   *
   * The lengths here are milliseconds; the machine only asks that they agree
   * with each other.
   */
  const machine = (sh) => ({
    curve: sh.curve, sustain: sh.sustain, attack: sh.attackMs,
    decay: sh.decayMs, release: sh.releaseMs, gate: sh.gateMs,
  });
  /* `gated` false is the ghost: the envelope AS DIALLED, with no gate over it. */
  const levelAt = (sh, ms, gated = true) =>
    (gated ? envLevel : stageLevel)(machine(sh), ms);

  const samples = (gated) => {
    const sh = env();
    if (!sh) return [];
    return Array.from({ length: 240 }, (_, i) =>
      levelAt(sh, sh.spanMs * (i / 239), gated));
  };

  const caption = () => {
    const sh = env();
    return sh ? `ENVELOPE ${Math.round(sh.spanMs)} MS   STEP ${sh.msStep.toFixed(1)} MS` : '';
  };

  /* Two strips along the bottom -- the millisecond axis under the phase
   * letters -- so neither ever sits on top of the curve. */
  const LABEL_H = 12, AXIS_H = 14;
  const boxBottom = () => props.h - INSET - LABEL_H - AXIS_H;
  const xAt = (f) => INSET + (props.w - 2 * INSET) * Math.min(1, Math.max(0, f));

  return (
    <Well w={props.w} h={props.h} steps={0} caption={caption()}>
      {env() && (() => {
        const sh = env();
        const top = CAPTION, bot = boxBottom();
        /* One mapping for the curve, the ghost and the dots alike: they must
         * agree, or the markers float off the line they mark. */
        const yAt = (v) => top + hFloor * (1 - Math.min(1, Math.max(0, v)));
        const path = (vals, sign) => vals.map((v, i) =>
          `${i ? 'L' : 'M'} ${xAt(i / (vals.length - 1)).toFixed(2)} ${(mid - sign * half * Math.min(1, Math.max(0, v))).toFixed(2)}`).join(' ');

        const floor = 1 - Math.min(1, Math.max(0, sh.amount ?? 1));
        const hFloor = (bot - top) * (1 - floor);
        const aEnd = sh.attackMs, dEnd = sh.attackMs + sh.decayMs;
        const rEnd = sh.gateMs + sh.releaseMs;
        /* A stage the gate never reached did not happen, so it gets no
         * marker -- and two markers landing on the same millisecond are one
         * mark, not two drawn on top of each other. */
        const dots = [...new Set(
          [0, aEnd <= sh.gateMs ? aEnd : null,
           sh.decayMs > 0 && dEnd <= sh.gateMs ? dEnd : null,
           sh.gateMs,
           sh.releaseMs > 0 && rEnd <= sh.spanMs ? rEnd : null]
          .filter((m) => m !== null))];

        /*
         * THE LETTERS LABEL WHAT RAN, NOT WHAT WAS DIALLED.
         *
         * Each stage is clamped to the gate, because the gate shuts whatever
         * stage is running. Unclamped, a long attack against a narrow Width
         * printed "D" across the stretch AFTER the release had already taken
         * the envelope to zero -- a decay that never happened, labelled over a
         * curve that was doing the opposite. And "S" came out as a negative
         * span, which is the sustain fault as it actually appeared.
         *
         * Clamped, a stage the gate cut short collapses to zero width and the
         * skip below drops it, so the letters left are the stages you can hear.
         */
        const clamp = (ms) => Math.min(ms, sh.gateMs);
        const segs = [['A', 0, clamp(aEnd)],
                      ['D', clamp(aEnd), clamp(dEnd)],
                      ['S', clamp(dEnd), sh.gateMs],
                      ['R', sh.gateMs, rEnd]];
        const g = samples(true), gh = samples(false);
        const ghPath = () => gh.map((v, i) =>
          `${i ? 'L' : 'M'} ${(INSET + (props.w - 2 * INSET) * (i / (gh.length - 1))).toFixed(2)} ${(top + hFloor * (1 - Math.min(1, Math.max(0, v)))).toFixed(2)}`).join(' ');

        return (
          <>
            {/* Where the gate really closes: a rail-coloured hairline, since
              * it marks a position rather than a value. */}
            {sh.hold < 1 && (
              <line x1={xAt(sh.gateMs / sh.spanMs)} x2={xAt(sh.gateMs / sh.spanMs)}
                    y1={top} y2={bot} stroke="var(--line-200)" />
            )}
            {/* THE GHOST FIRST, UNDERNEATH: the envelope as dialled. The dim
              * line is the decay you asked for; the solid one is what the
              * gate leaves of it. */}
            {sh.early && (
              <path d={ghPath()} fill="none" stroke="var(--ink-dim)" stroke-width="1" />
            )}
            {/* ONE-SIDED, with the Amount floor: the gate never closes past
              * Amount, so everything below 1 - amount is lit whatever it
              * does. An envelope drawn as though it reached zero would be
              * lying about what you hear. */}
            <Curve values={g} w={props.w} top={top} bottom={bot}
                   floor={floor} />

            <For each={dots}>{(ms) => (
              <circle cx={xAt(ms / sh.spanMs)} cy={yAt(levelAt(sh, ms))} r="2.5"
                      fill="var(--on-uv)" />
            )}</For>

            {/* A/D/S/R under their segments -- skipped when the segment is
              * narrower than the letter, rather than shrunk. */}
            <For each={segs}>{([ch, from, to]) => {
              const x0 = xAt(from / sh.spanMs), x1 = xAt(to / sh.spanMs);
              return (to > from && x1 - x0 >= 12) ? (
                <text class="t-hint plot-caption" x={(x0 + x1) / 2} y={bot + LABEL_H - 1}
                      text-anchor="middle">{ch}</text>
              ) : null;
            }}</For>

            {/* The step edge, amber when the envelope runs past it. */}
            <line x1={xAt(sh.msStep / sh.spanMs)} x2={xAt(sh.msStep / sh.spanMs)}
                  y1={top} y2={bot} stroke-width="2"
                  stroke={sh.truncated ? 'var(--amber)' : 'var(--line-200)'} />

            <Axis w={props.w} y={props.h - INSET - AXIS_H} spanMs={sh.spanMs} markMs={sh.msStep} />
          </>
        );
      })()}
    </Well>
  );
}

/*
 * THE MILLISECOND RULER. The landmark first -- the step duration, with its
 * unit, because it is exact and everything else gives way to it -- then the
 * smallest ladder interval whose ticks stay ~38px apart.
 */
function Axis(props) {
  const ticks = createMemo(() => {
    const span = props.spanMs, w = props.w - 2 * INSET;
    if (!(span > 0) || w < 8) return [];
    const ladder = [1, 2, 5, 10, 20, 25, 50, 100, 200, 250, 500, 1000, 2000, 5000];
    const step = ladder.find((c) => w * (c / span) >= 38) ?? ladder[ladder.length - 1];
    const out = [];
    const mark = props.markMs;
    let taken = null;
    if (mark > 0 && mark <= span) {
      const whole = Math.abs(mark - Math.round(mark)) < 0.05;
      out.push({ ms: mark, text: `${mark.toFixed(whole ? 0 : 1)} ms` });
      taken = mark / span;
    }
    for (let ms = 0; ms <= span + 1e-6; ms += step) {
      const f = ms / span;
      /* Give way to the landmark rather than overprint it. */
      if (taken !== null && Math.abs(f - taken) * w < 34) continue;
      out.push({ ms, text: String(Math.round(ms)) });
    }
    return out;
  });
  const x = (ms) => INSET + (props.w - 2 * INSET) * (ms / props.spanMs);
  return (
    <>
      <line x1={INSET} x2={props.w - INSET} y1={props.y} y2={props.y} stroke="var(--line-200)" />
      <For each={ticks()}>{(t) => (
        <>
          <line x1={x(t.ms)} x2={x(t.ms)} y1={props.y} y2={props.y + 3} stroke="var(--line-200)" />
          {/* Nudged in at the ends so a label never hangs outside the well. */}
          <text class="t-hint plot-caption" y={props.y + 12}
                x={Math.min(props.w - INSET - t.text.length * 3, Math.max(INSET + t.text.length * 3, x(t.ms)))}
                text-anchor="middle">{t.text}</text>
        </>
      )}</For>
    </>
  );
}

/*
 * THE PATTERN: the gain the engine will apply across one CYCLE -- the same
 * envelope, once per lit step, scaled by that step's amount. Drawn as the
 * gate rather than as bars, because it is the same quantity the envelope plot
 * shows and reading them against each other is the point of the two views.
 */
export function PatternPlot(props) {
  const n = () => Math.max(1, props.length ?? 16);
  /*
   * THE CURVE IS THE ENGINE'S, NOT A DESCRIPTION OF IT.
   *
   * This used to walk the pattern here and rebuild the envelope from the
   * parameters -- a second implementation of the DSP, and the only one in this
   * editor with no oracle behind it, which is exactly why it was wrong. An OFF
   * step pushed hard zeros and threw the carry away, so a release outliving its
   * step was drawn as an instant cut at the pad edge and the next step's attack
   * started from silence instead of the tail it should have continued from.
   * Release runs to 200% of the gate's Width; that is most settings, not an
   * edge case.
   *
   * The plugin renders one cycle through a scratch engine with a DC input and
   * pushes the samples. There is no model left here to be wrong -- ties,
   * legato, the per-step level, the fade and a release crossing three steps all
   * arrive correct because they are not being reasoned about.
   *
   * AMOUNT IS THE ONE THING STILL APPLIED HERE, because it can be:
   *
   *     m = 1 - amount*(1 - g) = floor + (1 - floor)*g,   floor = 1 - amount
   *
   * is affine in g, so it is a floor under the curve rather than a reason to
   * ask the plugin for a new one. Dragging Amount costs a repaint and nothing
   * else, which is why the render leaves it at 1.
   */
  const values = createMemo(() => props.gate?.values ?? []);
  const top = CAPTION, bot = () => props.h - INSET;
  return (
    <Well w={props.w} h={props.h} caption="PATTERN   ONE CYCLE">
      {/* Rules UNDER the curve, so nothing is hidden by a rule. */}
      <StepRules count={n()} w={props.w} top={top} bottom={bot()} />
      <Curve values={values()} w={props.w} top={top} bottom={bot()}
             floor={1 - Math.min(1, Math.max(0, props.params?.amount ?? 1))} />
      {/* THE FRACTIONAL PHASE, so this glides rather than stepping.
        * `phase / n` and not `(step + 0.5) / n`: the phase already carries its
        * position WITHIN the step, where an integer step index had to be
        * centred in its cell to sit anywhere sensible. */}
      {props.moving && (props.phase ?? -1) >= 0 && (
        <line class="playhead" stroke="var(--ink)"
              x1={INSET + (props.w - 2 * INSET) * (props.phase / n())}
              x2={INSET + (props.w - 2 * INSET) * (props.phase / n())}
              y1={top} y2={bot()} />
      )}
      {/* Numbers LAST, so a number is never swallowed by what it labels. */}
      <StepNumbers count={n()} w={props.w} top={top} />
    </Well>
  );
}

/*
 * THE SIGNAL. Dry behind in grey, gated in front in uv, the envelope over both.
 *
 * The dry is CONTEXT, NOT THE SUBJECT, so it is drawn back at partial alpha:
 * a sustained input fills every column edge to edge, and at full strength it
 * is a solid slab with the gated trace fighting to be seen through it.
 */
export function Scope(props) {
  /*
   * A SWEEP OF THE PATTERN, ON A STATIC AXIS, FILLING LEFT TO RIGHT.
   *
   * Column k is pattern phase k/cols and is drawn at that x -- always, whatever
   * the sweep is doing -- so the axis stands still, the step rules line up with
   * the gate, and the ENVELOPE can be drawn over the trace it produced. `head`
   * is where the plugin is writing, so the picture reads as filling rather than
   * scrolling.
   *
   * THIS WENT BACK AND FORTH ONCE, so here is the part that matters. It was a
   * pattern sweep, became a rolling window of wall time, and is a pattern sweep
   * again. The complaint that moved it was real: a column was refreshed once per
   * CYCLE -- seconds apart at sixteen steps -- so turning the input up made the
   * picture creep after it, which reads as a filtered meter.
   *
   * But the mapping was never the cause. The plugin published a column only when
   * it was COMPLETE, so the newest data was up to a column old and, far worse, a
   * level change was invisible until the sweep came round. It publishes the
   * partial column every block now: current to within one buffer, overwritten
   * and never blended. Wall time solved the symptom and cost the alignment --
   * step numbers and the gate overlay both came off the plot with it -- where
   * fixing the publish keeps both.
   */
  const AXIS_H = 14;
  const top = CAPTION, bot = () => props.h - INSET - AXIS_H;
  const mid = () => (top + bot()) / 2;
  const cycleMs = () => props.windowMs ?? 1000;
  const n = () => Math.max(1, props.length ?? 16);
  const x01 = (t) => INSET + (props.w - 2 * INSET) * Math.max(0, Math.min(1, t));

  /* One screen pixel per column of output, min/max over every capture column
   * behind it -- plot::decimate's argument, and the reason a narrow gate
   * survives at 128 steps. */
  const band = (loIdx, hiIdx) => {
    const cols = props.scope;
    if (!cols || cols.length < 2) return '';
    const w = Math.max(1, Math.round(props.w - 2 * INSET));
    const half = (bot() - top) * 0.5;
    const y = (v) => mid() - half * Math.max(-1, Math.min(1, v));
    /* Every column is drawn, in place: the axis is the pattern, so there is no
     * rotation and no fill state. A column the sweep has not reached yet holds
     * whatever it held, which is what makes this read as filling. */
    const total = cols.length;
    const hi = [], lo = [];
    for (let x = 0; x < w; x++) {
      const a = Math.floor((x * total) / w);
      const b = Math.max(a + 1, Math.floor(((x + 1) * total) / w));
      let mn = Infinity, mx = -Infinity;
      for (let i = a; i < b && i < total; i++) {
        if (cols[i][loIdx] < mn) mn = cols[i][loIdx];
        if (cols[i][hiIdx] > mx) mx = cols[i][hiIdx];
      }
      if (mn === Infinity) break;
      const px = INSET + x;
      hi.push(`${hi.length ? 'L' : 'M'} ${px} ${y(mx).toFixed(1)}`);
      lo.push([px, y(mn)]);
    }
    if (!hi.length) return '';
    const back = lo.reverse().map(([px, py]) => `L ${px} ${py.toFixed(1)}`);
    return hi.join(' ') + ' ' + back.join(' ') + ' Z';
  };

  /*
   * THE ENVELOPE OVER THE TRACE, and it is drawn as the OUTLINE the gate cuts
   * rather than as a filled curve.
   *
   * The trace is already a filled band; a second fill over it is mud. So this is
   * the gate's shape mirrored about the zero line -- the envelope above and its
   * negative below, which is exactly the outline a symmetrical signal at full
   * level would fill. Laid over the real trace, the gap between them IS the
   * difference between what the gate asked for and what the audio did.
   *
   * It needs an axis that is the pattern to mean anything, which is why it was
   * taken off when the axis was wall time and why it can come back now.
   *
   * AND IT IS THE SAME RENDERED CURVE THE PATTERN TAB DRAWS. It used to be a
   * coarser re-derivation here -- `gateAt` per step with no carry-in at all --
   * so the two tabs could disagree about the same gate. One curve, from the
   * engine, drawn twice.
   */
  const gate = createMemo(() => {
    const g = props.gate;
    if (!g || !g.values?.length) return null;
    const half = (bot() - top) * 0.5;
    /* Amount floors the gate exactly as it does in the Pattern plot, and for
     * the same reason: the render leaves it at 1 because it is affine. */
    const floor = 1 - Math.min(1, Math.max(0, props.params?.amount ?? 1));
    const up = [], dn = [];
    const N = g.values.length;
    for (let i = 0; i < N; i++) {
      const v = floor + (1 - floor) * g.values[i];
      const px = x01(i / N);
      up.push(`${up.length ? 'L' : 'M'} ${px.toFixed(1)} ${(mid() - half * v).toFixed(1)}`);
      dn.push([px, mid() + half * v]);
    }
    if (!up.length) return null;
    return { up: up.join(' '),
             dn: 'M ' + dn.map(([px, py]) => `${px.toFixed(1)} ${py.toFixed(1)}`).join(' L ') };
  });

  return (
    <Well w={props.w} h={props.h}
          caption={`SIGNAL   ONE CYCLE, ${Math.round(cycleMs())} MS   DRY IN GREY, GATED IN FRONT`}>
      {/* The step boundaries, UNDER everything: the axis is the pattern again, so
        * a column has a step and the rules say which. */}
      <StepRules count={n()} w={props.w} top={top} bottom={bot()} />
      {/* The zero line, so a silent stretch reads as silence rather than as a
        * gap in the drawing. */}
      <line x1={INSET} x2={props.w - INSET} y1={mid()} y2={mid()} stroke="var(--line-100)" />
      {/* The dry is CONTEXT, NOT THE SUBJECT, so it is drawn back at partial
        * alpha: a sustained input fills every column edge to edge, and at
        * full strength it is a slab with the gated trace fighting to be seen
        * through it. */}
      <path d={band(0, 1)} fill="var(--scope-dry)" opacity="0.5" />
      {/*
        * THE GATED TRACE IS THE SUBJECT, so it gets the arc halo -- the same
        * 3px falloff the knob's value arc uses, and for the same reason: `uv`
        * is a near-white and a near-white drawn alone has no cast at all. The
        * 10px led halo is too wide here; on a trace that fills the well it
        * blooms into mud.
        */}
      <g class="glow-arc"><path d={band(2, 3)} fill="var(--uv)" /></g>
      {/* The envelope, as an outline above and below zero. `ink-dim` and a
        * hairline: it is the reference the trace is measured against, so it must
        * not compete with it. */}
      {gate() && <>
        <path d={gate().up} fill="none" stroke="var(--ink-dim)" stroke-width="1" />
        <path d={gate().dn} fill="none" stroke="var(--ink-dim)" stroke-width="1" />
      </>}
      {/*
        * WHERE THE SWEEP IS WRITING. Not a playhead -- it is the same position,
        * but what it means here is "the picture is current up to here and stale
        * after it", which is the one thing a static axis cannot say on its own.
        */}
      {props.moving && (props.head ?? -1) >= 0 && props.scope?.length > 1 && (
        <line class="sweep" stroke="var(--uv)" opacity="0.7"
              x1={x01(props.head / props.scope.length)}
              x2={x01(props.head / props.scope.length)}
              y1={top} y2={bot()} />
      )}
      {/* Milliseconds into the cycle, which is what the axis now is. The same
        * axis the envelope plot uses, so the two read alike. */}
      <Axis w={props.w} y={props.h - INSET - AXIS_H} spanMs={cycleMs()} markMs={0} />
      {/* Numbers LAST, so a number is never swallowed by what it labels. */}
      <StepNumbers count={n()} w={props.w} top={top} />
    </Well>
  );
}

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
      {props.caption &&
        <text class="plot-caption t-hint" x={INSET} y="11">{props.caption}</text>}
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
 * MIN/MAX PER SCREEN PIXEL, never a mean or a pick.
 *
 * A narrow gate is a fraction of a pixel at 128 steps, so sampling one value
 * per column would quietly report a signal that is not the one playing. This
 * is plot::decimate: every source value inside a pixel contributes, and what
 * survives is the pair that bounds them.
 */
function decimate(values, cols) {
  const n = values.length;
  if (n === 0 || cols <= 0) return [];
  const out = new Array(cols);
  for (let x = 0; x < cols; x++) {
    const a = Math.floor((x * n) / cols);
    const b = Math.max(a + 1, Math.floor(((x + 1) * n) / cols));
    let lo = Infinity, hi = -Infinity;
    for (let i = a; i < b && i < n; i++) {
      const v = values[i];
      if (v < lo) lo = v;
      if (v > hi) hi = v;
    }
    out[x] = lo === Infinity ? [0, 0] : [lo, hi];
  }
  return out;
}

/*
 * TWO RENDERERS, AND THE DIFFERENCE IS DELIBERATE.
 *
 * `Curve` is the envelope's: ONE-SIDED, a filled region rising from the
 * floor. `Gate` is the scope's: MIRRORED about the centreline with a keyline
 * behind it, because there it is drawn OVER a waveform and needs its own edge
 * against whatever it crosses.
 *
 * Using the mirrored one for both is what made the envelope plot look like a
 * waveform rather than an envelope.
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
 * plot drawGate: mirrored about the centreline at 0.94 of the half-height --
 * a gate drawn at 1.0 lands exactly on the well's frame and reads as the
 * border rather than as a value. A bg-000 keyline first, then the line, since
 * over the dry band there is no brightness left to separate them.
 *
 * The SCOPE only.
 */
function Gate(props) {
  const path = (sign) => {
    const v = props.values;
    if (!v || v.length < 2) return '';
    const mid = (props.top + props.bottom) / 2;
    const half = (props.bottom - props.top) * 0.5 * 0.94;
    return v.map((y, i) => {
      const x = INSET + (props.w - 2 * INSET) * (i / (v.length - 1));
      return `${i ? 'L' : 'M'} ${x.toFixed(2)} ${(mid - sign * half * Math.min(1, Math.max(0, y))).toFixed(2)}`;
    }).join(' ');
  };
  return (
    <>
      <path d={path(1)}  fill="none" stroke="var(--bg-000)" stroke-width="4" />
      <path d={path(-1)} fill="none" stroke="var(--bg-000)" stroke-width="4" />
      <path d={path(1)}  fill="none" stroke="var(--uv)" stroke-opacity="0.85" stroke-width="2" />
      <path d={path(-1)} fill="none" stroke="var(--uv)" stroke-opacity="0.85" stroke-width="2" />
    </>
  );
}

/* env_shape in the engine: linear, exponential, and an s-curve built from two
 * exponentials. */
const K = 3.0, DEN = 1 - Math.exp(-K);
const shape = (curve, t) => {
  const u = Math.min(1, Math.max(0, t));
  return curve === 1 ? (1 - Math.exp(-K * u)) / DEN
       : curve === 2 ? (u < 0.5 ? 0.5 * (1 - Math.exp(-K * 2 * u)) / DEN
                                : 1 - 0.5 * (1 - Math.exp(-K * 2 * (1 - u))) / DEN)
       : u;
};

/* One gate's gain over one STEP, 0..1 of the step. The stages are
 * percentages of the gate's WIDTH, which is how the engine stores them. */
export function gateAt(p, t) {
  const a = Math.max(0, p.attack) / 100 * p.width;
  const d = Math.max(0, p.decay) / 100 * p.width;
  const r = Math.max(0, p.release) / 100 * p.width;
  const shut = Math.max(a + d, p.width);
  if (t < a)      return shape(p.curve, a > 0 ? t / a : 1);
  if (t < a + d)  return 1 - (1 - p.sustain) * shape(p.curve, d > 0 ? (t - a) / d : 1);
  if (t < shut)   return p.sustain;
  if (t < shut + r) return p.sustain * (1 - shape(p.curve, r > 0 ? (t - shut) / r : 1));
  return 0;
}

/*
 * THE ENVELOPE, ON A MILLISECOND AXIS.
 *
 * Not "one step": the span is computed, and the envelope may run PAST the
 * step's edge -- which is the case the amber rule and the ghost curve exist
 * to explain. The arithmetic is TgEnvelopeShape::render's, and all of it is
 * derivable here: the scratch engine the JUCE editor ran was only ever needed
 * for the curve's samples, and env_shape is ported above.
 *
 *   widthMs     = hold * msStep          the gate's open time
 *   stageMs     = pct * widthMs / 100    a stage is a % OF THE WIDTH
 *   gateMs      = widthMs                where the gate closes
 *   releaseAtMs = max(gateMs, a + d)
 *   spanMs      = max(releaseAt + r, msStep, 2) * 1.04
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
    const releaseAtMs = Math.max(gateMs, attackMs + decayMs);
    const spanMs = Math.max(releaseAtMs + releaseMs, msStep, 2) * 1.04;
    return {
      msStep, hold, attackMs, decayMs, releaseMs, gateMs, releaseAtMs, spanMs,
      sustain: Math.min(1, Math.max(0, p.sustain)), curve: p.curve,
      amount: Math.min(1, Math.max(0, p.amount ?? 1)),
      /* The release ran past the step's end -- the next gate will cut it. */
      truncated: releaseAtMs + releaseMs > msStep + 1e-6,
      /* The gate closed before the decay finished, so the solid curve alone
       * would be a puzzle: you set a long decay and see a spike. */
      early: gateMs < attackMs + decayMs - 1e-6,
    };
  });

  /* The envelope's level at a time in ms. `gated` false is the ghost: the
   * envelope AS DIALLED, with no gate close. */
  const levelAt = (sh, ms, gated = true) => {
    const { attackMs: a, decayMs: d, releaseMs: r, gateMs: g, sustain, curve } = sh;
    const atGate = ms >= g;
    if (gated && atGate) {
      const lvl = levelAt(sh, g, false);
      if (r <= 0) return 0;
      return ms < g + r ? lvl * (1 - shape(curve, (ms - g) / r)) : 0;
    }
    if (ms < a) return a > 0 ? shape(curve, ms / a) : 1;
    if (ms < a + d) return 1 - (1 - sustain) * (d > 0 ? shape(curve, (ms - a) / d) : 1);
    return sustain;
  };

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
         * marker. */
        const dots = [0, aEnd <= sh.gateMs ? aEnd : null,
                      sh.decayMs > 0 && dEnd <= sh.gateMs ? dEnd : null,
                      sh.gateMs,
                      sh.releaseMs > 0 && rEnd <= sh.spanMs ? rEnd : null]
                     .filter((m) => m !== null);

        const segs = [['A', 0, aEnd], ['D', aEnd, dEnd],
                      ['S', dEnd, sh.gateMs], ['R', sh.gateMs, rEnd]];
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
  const values = createMemo(() => {
    const p = props.params;
    if (!p) return [];
    const perStep = 24, out = [];
    for (let s = 0; s < n(); s++) {
      const on = !!props.steps?.[s];
      const amt = on ? (props.depths?.[s] ?? 1) : 0;
      /* A tie holds through: the gate does not close at the step's edge. */
      const held = on && !!props.ties?.[s];
      for (let k = 0; k < perStep; k++)
        out.push(on ? (held ? amt : amt * gateAt(p, k / perStep)) : 0);
    }
    return out;
  });
  const top = CAPTION, bot = () => props.h - INSET;
  return (
    <Well w={props.w} h={props.h} caption="PATTERN   ONE CYCLE">
      {/* Rules UNDER the curve, so nothing is hidden by a rule. */}
      <StepRules count={n()} w={props.w} top={top} bottom={bot()} />
      <Curve values={values()} w={props.w} top={top} bottom={bot()}
             floor={1 - Math.min(1, Math.max(0, props.params?.amount ?? 1))} />
      {props.moving && (props.playhead ?? -1) >= 0 && (
        <line stroke="var(--ink)"
              x1={INSET + (props.w - 2 * INSET) * ((props.playhead + 0.5) / n())}
              x2={INSET + (props.w - 2 * INSET) * ((props.playhead + 0.5) / n())}
              y1={top} y2={bot()} />
      )}
      {/* Numbers LAST, so a number is never swallowed by what it labels. */}
      <StepNumbers count={n()} w={props.w} top={top} />
    </Well>
  );
}

/*
 * THE SIGNAL. Dry behind in ink-dim, gated in front in uv.
 *
 * The dry is CONTEXT, NOT THE SUBJECT, so it is drawn back at partial alpha:
 * a sustained input fills every column edge to edge, and at full strength it
 * is a solid slab with the gated trace fighting to be seen through it.
 */
export function Scope(props) {
  /*
   * THE AXIS IS FIXED, AND THAT IS THE WHOLE POINT OF A SWEEP. It used to
   * divide by cols.length, so a half-filled sweep was stretched across the
   * whole plot and the picture rescaled as it filled -- the axis moved under
   * the audio, which is the one thing a scope triggered by the pattern must
   * not do.
   */
  const TOTAL = 256;                 /* kScopeCols in TranceGate.h */
  const top = CAPTION, bot = () => props.h - INSET;
  const mid = () => (top + bot()) / 2;

  /* One screen pixel per column of output, min/max over every capture column
   * behind it -- plot::decimate's argument, and the reason a narrow gate
   * survives at 128 steps. */
  const band = (loIdx, hiIdx) => {
    const cols = props.scope;
    if (!cols || cols.length < 2) return '';
    const w = Math.max(1, Math.round(props.w - 2 * INSET));
    const half = (bot() - top) * 0.5;
    const y = (v) => mid() - half * Math.max(-1, Math.min(1, v));
    const filled = Math.min(cols.length, TOTAL);
    const hi = [], lo = [];
    for (let x = 0; x < w; x++) {
      const a = Math.floor((x * TOTAL) / w);
      const b = Math.max(a + 1, Math.floor(((x + 1) * TOTAL) / w));
      if (a >= filled) break;            /* stop where the sweep has reached */
      let mn = Infinity, mx = -Infinity;
      for (let i = a; i < b && i < filled; i++) {
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

  return (
    <Well w={props.w} h={props.h} caption="SIGNAL   DRY BEHIND, GATED IN FRONT">
      {/* The step grid sits UNDER the audio, so a transient is never hidden
        * by a rule. */}
      <StepRules count={props.length ?? 16} w={props.w} top={top} bottom={bot()} />
      {/* The zero line, so a silent stretch reads as silence rather than as a
        * gap in the drawing. */}
      <line x1={INSET} x2={props.w - INSET} y1={mid()} y2={mid()} stroke="var(--line-100)" />
      {/* The dry is CONTEXT, NOT THE SUBJECT, so it is drawn back at partial
        * alpha: a sustained input fills every column edge to edge, and at
        * full strength it is a slab with the gated trace fighting to be seen
        * through it. */}
      <path d={band(0, 1)} fill="var(--ink-dim)" opacity="0.45" />
      <path d={band(2, 3)} fill="var(--uv)" />
      {/* The gate over the audio -- mirrored, with its keyline. */}
      {props.gate && props.gate.length > 1 && (
        <Gate values={props.gate} w={props.w} top={top} bottom={bot()} />
      )}
      {/* Last of all, so a number is never swallowed by the audio. */}
      <StepNumbers count={props.length ?? 16} w={props.w} top={top} />
    </Well>
  );
}

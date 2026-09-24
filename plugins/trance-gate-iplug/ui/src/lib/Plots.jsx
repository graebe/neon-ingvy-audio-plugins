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

const INSET = 6, CAPTION = 14;

/* The well: bg-000 ground, a line-100 hairline, and the caption in hint style
 * at (inset, 12). The content area is inset all round with CAPTION off the
 * top. */
function Well(props) {
  const w = () => props.w, h = () => props.h;
  return (
    <svg class="plot" width={w()} height={h()} viewBox={`0 0 ${w()} ${h()}`}>
      <rect x="0.5" y="0.5" width={w() - 1} height={h() - 1}
            fill="var(--bg-000)" stroke="var(--line-100)" />
      {/* Step rules, thinning out as the count rises so they never crowd. */}
      <For each={Array.from({ length: Math.min(props.steps ?? 16, 64) }, (_, i) => i)}>{(i) => {
        const x = INSET + (w() - 2 * INSET) * (i / Math.max(1, props.steps ?? 16));
        return <line x1={x} y1={CAPTION} x2={x} y2={h() - INSET}
                     stroke="var(--line-100)" />;
      }}</For>
      {props.children}
      {props.caption &&
        <text class="plot-caption t-hint" x={INSET} y="11">{props.caption}</text>}
    </svg>
  );
}

/*
 * THE GATE, MIRRORED ABOUT THE CENTRELINE -- not a line rising from the
 * floor. `half` is 0.94 of the half-height on purpose: a gate drawn at 1.0
 * lands exactly on the well's frame and reads as the border rather than as a
 * value.
 *
 * A KEYLINE FIRST, THEN THE LINE. Over the scope's dry band there is no
 * brightness left to separate a gate at 1.0 from the audio it crosses -- at
 * hair weight it vanishes into the band's top edge.
 */
function Gate(props) {
  const path = (sign) => {
    const v = props.values;
    if (!v || v.length < 2) return '';
    const top = CAPTION, bot = props.h - INSET;
    const mid = (top + bot) / 2, half = (bot - top) * 0.5 * 0.94;
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
function gateAt(p, t) {
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
        const mid = (top + bot) / 2, half = (bot - top) * 0.5 * 0.94;
        const yAt = (v) => mid - half * Math.min(1, Math.max(0, v));
        const path = (vals, sign) => vals.map((v, i) =>
          `${i ? 'L' : 'M'} ${xAt(i / (vals.length - 1)).toFixed(2)} ${(mid - sign * half * Math.min(1, Math.max(0, v))).toFixed(2)}`).join(' ');

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
              <path d={path(gh, 1)} fill="none" stroke="var(--ink-dim)" stroke-width="1" />
            )}
            <path d={path(g, 1)}  fill="none" stroke="var(--bg-000)" stroke-width="4" />
            <path d={path(g, -1)} fill="none" stroke="var(--bg-000)" stroke-width="4" />
            <path d={path(g, 1)}  fill="none" stroke="var(--uv)" stroke-opacity="0.85" stroke-width="2" />
            <path d={path(g, -1)} fill="none" stroke="var(--uv)" stroke-opacity="0.85" stroke-width="2" />

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
  const values = createMemo(() => {
    const p = props.params;
    const n = Math.max(1, props.length ?? 16);
    if (!p) return [];
    const perStep = 24, out = [];
    for (let s = 0; s < n; s++) {
      const on = !!props.steps?.[s];
      const amt = on ? (props.depths?.[s] ?? 1) : 0;
      /* A tie holds through: the gate does not close at the step's edge. */
      const held = on && !!props.ties?.[s];
      for (let k = 0; k < perStep; k++)
        out.push(on ? (held ? amt : amt * gateAt(p, k / perStep)) : 0);
    }
    return out;
  });
  const n = () => Math.max(1, props.length ?? 16);
  return (
    <Well w={props.w} h={props.h} steps={n()} caption="PATTERN   ONE CYCLE">
      <Gate values={values()} w={props.w} h={props.h} />
      {props.moving && (props.playhead ?? -1) >= 0 && (
        <line stroke="var(--ink)"
              x1={INSET + (props.w - 2 * INSET) * ((props.playhead + 0.5) / n())}
              x2={INSET + (props.w - 2 * INSET) * ((props.playhead + 0.5) / n())}
              y1={CAPTION} y2={props.h - INSET} />
      )}
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
   * THE AXIS IS FIXED, AND THAT IS THE WHOLE POINT OF A SWEEP.
   *
   * It used to divide by cols.length, so a half-filled sweep was stretched
   * across the whole plot and the picture RESCALED as it filled -- the
   * x-axis moved under the audio, which is the one thing a scope triggered
   * by the pattern must not do.
   *
   * The span is always the capture's full width; only as much of it as has
   * arrived is drawn, so the trace fills left to right against a still axis.
   */
  const TOTAL = 256;                 /* kScopeCols in TranceGate.h */

  const band = (which) => {
    const cols = props.scope;
    if (!cols || cols.length < 2) return '';
    const top = CAPTION, bot = props.h - INSET;
    const mid = (top + bot) / 2, half = (bot - top) * 0.5;
    const x = (i) => INSET + (props.w - 2 * INSET) * (i / (TOTAL - 1));
    const y = (v) => mid - half * Math.max(-1, Math.min(1, v));
    const n = Math.min(cols.length, TOTAL);
    const hi = [];
    for (let i = 0; i < n; i++)
      hi.push(`${i ? 'L' : 'M'} ${x(i).toFixed(1)} ${y(cols[i][which * 2 + 1]).toFixed(1)}`);
    const lo = [];
    for (let i = n - 1; i >= 0; i--)
      lo.push(`L ${x(i).toFixed(1)} ${y(cols[i][which * 2]).toFixed(1)}`);
    return hi.join(' ') + ' ' + lo.join(' ') + ' Z';
  };

  return (
    <Well w={props.w} h={props.h} steps={props.length}
          caption="SIGNAL   DRY BEHIND, GATED IN FRONT">
      <line x1={INSET} x2={props.w - INSET}
            y1={(CAPTION + props.h - INSET) / 2} y2={(CAPTION + props.h - INSET) / 2}
            stroke="var(--line-100)" />
      {/* The dry is CONTEXT, NOT THE SUBJECT, so it is drawn back at partial
        * alpha: a sustained input fills every column edge to edge, and at full
        * strength it is a solid slab with the gated trace fighting to be seen
        * through it. */}
      <path d={band(0)} fill="var(--ink-dim)" opacity="0.45" />
      <path d={band(1)} fill="var(--uv)" />
    </Well>
  );
}

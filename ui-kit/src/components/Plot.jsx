/*
 * The plot primitives: a well, a millisecond ruler, and the min/max band a
 * waveform is drawn with.
 *
 * WHY THESE ARE IN THE KIT NOW. `index.js` states the rule: "a component with
 * one caller has no API yet -- only a shape. They move the day a second editor
 * wants one." NI Side-Chain is that second editor. All three of these were
 * `plugins/trance-gate/ui/src/lib/Plots.jsx`, and what is here is that file's
 * general half -- the half that knows nothing about steps, ties or slots.
 *
 * WHAT DID NOT MOVE, and must not: `Curve`, `EnvelopePlot`, `PatternPlot`,
 * `StepRules`, `StepNumbers`. Each of those knows what a step is, and a gate's
 * envelope is not a ducker's. Two editors drawing a curve is not two editors
 * drawing the SAME curve.
 *
 * THE VIEWBOX IS THE PIXEL SIZE, 1:1, and that is not a detail. A fixed viewBox
 * stretched to fit with preserveAspectRatio="none" scales the TEXT too -- the
 * Trance Gate's captions came out 2.3x wide and unreadable, which looked like a
 * font problem and was a geometry one.
 */
import { For, createMemo } from 'solid-js';

/** plot::inset and plot::captionH, named as the design system names them. */
export const INSET = 6;
export const CAPTION = 14;

/**
 * Every plot's frame: a `bg-000` ground, a `line-100` hairline, and the caption
 * in hint style. The content area is inset all round with CAPTION off the top.
 */
export function Well(props) {
  return (
    <svg class="plot" width={props.w} height={props.h}
         viewBox={`0 0 ${props.w} ${props.h}`}>
      <rect x="0.5" y="0.5" width={props.w - 1} height={props.h - 1}
            fill="var(--bg-000)" stroke="var(--line-100)" />
      {props.children}
      {/* `well-caption` as well as `plot-caption`, so the caption is reachable
        * on its own: an Axis's tick labels carry plot-caption too and come
        * EARLIER in document order. */}
      {props.caption
        && <text class="plot-caption well-caption t-hint" x={INSET} y="11">{props.caption}</text>}
    </svg>
  );
}

/**
 * A millisecond ruler.
 *
 * The landmark first -- `markMs`, with its unit, because it is exact and
 * everything else gives way to it -- then the smallest ladder interval whose
 * ticks stay about 38px apart.
 *
 * Props: `w`, `y`, `spanMs`, `markMs` (0 for none).
 */
export function Axis(props) {
  const ticks = createMemo(() => {
    const span = props.spanMs;
    const w = props.w - 2 * INSET;
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
      <line x1={INSET} x2={props.w - INSET} y1={props.y} y2={props.y}
            stroke="var(--line-200)" />
      <For each={ticks()}>{(t) => (
        <>
          <line x1={x(t.ms)} x2={x(t.ms)} y1={props.y} y2={props.y + 3}
                stroke="var(--line-200)" />
          {/* Nudged in at the ends so a label never hangs outside the well. */}
          <text class="t-hint plot-caption" y={props.y + 12}
                x={Math.min(props.w - INSET - t.text.length * 3,
                            Math.max(INSET + t.text.length * 3, x(t.ms)))}
                text-anchor="middle">{t.text}</text>
        </>
      )}</For>
    </>
  );
}

/**
 * ONE CLOSED MIN/MAX ENVELOPE PATH for a column-wise waveform capture.
 *
 * Walks the top edge forward and the bottom edge back, so the result is a
 * fillable region rather than two strokes. This is the primitive both the Trance
 * Gate's scope and NI Side-Chain's signal well are built from, and the reason it is
 * shared is that getting it wrong is invisible: a plausible waveform that is not
 * the one playing.
 *
 * MIN AND MAX, NEVER A MEAN -- a transient is a fraction of a column and
 * averaging quietly reports a signal nobody is hearing. That decision is made
 * where the capture is written, not here; this exists so the DRAWING cannot
 * accidentally average either. It decimates to one screen pixel per column by
 * taking the extremes of every capture column behind that pixel, which is what
 * keeps a narrow dip visible at 256 columns in 700px.
 *
 * `cap` is a capture as the editors decode one: `{ data, stride, count }`, a
 * Float32Array holding `count` columns of `stride` values; `loIdx` and `hiIdx`
 * index into a column. `seen`, when given, is a predicate on the column index:
 * columns it rejects are treated as a gap and break the path, so a picture
 * still filling reads as unfinished rather than as a signal that stopped.
 */
export function band(cap, loIdx, hiIdx, geom, seen) {
  if (!cap || !(cap.count >= 2)) return '';
  const { data, stride } = cap;
  const { x0, w, top, bottom } = geom;
  const mid = (top + bottom) / 2;
  const half = (bottom - top) * 0.5;
  const y = (v) => mid - half * Math.max(-1, Math.min(1, v));
  const total = cap.count;

  /* Runs of consecutive drawn columns, so a gap is a gap and not a line across
   * one. Each run becomes its own closed subpath in the same `d`. */
  const runs = [];
  let hi = [];
  let lo = [];
  const flush = () => {
    if (hi.length > 1) runs.push({ hi: [...hi], lo: [...lo] });
    hi = [];
    lo = [];
  };

  for (let px = 0; px < w; px++) {
    const a = Math.floor((px * total) / w);
    const b = Math.max(a + 1, Math.floor(((px + 1) * total) / w));
    let mn = Infinity;
    let mx = -Infinity;
    for (let i = a; i < b && i < total; i++) {
      if (seen && !seen(i)) continue;
      const lo = data[i * stride + loIdx];
      const hi = data[i * stride + hiIdx];
      if (lo < mn) mn = lo;
      if (hi > mx) mx = hi;
    }
    if (mn === Infinity) {
      flush();
      continue;
    }
    hi.push(`${hi.length ? 'L' : 'M'} ${x0 + px} ${y(mx).toFixed(1)}`);
    lo.push([x0 + px, y(mn)]);
  }
  flush();

  return runs
    .map(({ hi: h, lo: l }) => `${h.join(' ')} ${l.reverse()
      .map(([px, py]) => `L ${px} ${py.toFixed(1)}`)
      .join(' ')} Z`)
    .join(' ');
}

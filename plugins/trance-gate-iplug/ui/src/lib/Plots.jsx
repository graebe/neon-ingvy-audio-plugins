/*
 * The three plots. Copyright (c) 2026 Torben Gräber. MIT.
 *
 * All three share one x-axis convention -- left to right across ONE PATTERN
 * CYCLE -- so switching between them compares like with like, which is the
 * whole reason they are tabs rather than three panels.
 */
import { For, createMemo } from 'solid-js';

const W = 560, H = 150, PAD = 10;

/* The well every plot sits in: a panel, a hairline, and the zero line that
 * makes a silent stretch read as silence rather than as a gap. */
function Well(props) {
  return (
    <svg class="plot" viewBox={`0 0 ${W} ${H}`} preserveAspectRatio="none">
      <rect x="0" y="0" width={W} height={H} fill="var(--bg-200)" stroke="var(--line-100)" />
      {/* step rules, thinning out as the count rises so they never crowd */}
      <For each={Array.from({ length: Math.min(props.steps ?? 16, 64) }, (_, i) => i)}>{(i) => {
        const x = PAD + (W - 2 * PAD) * (i / Math.max(1, props.steps ?? 16));
        return <line x1={x} y1="0" x2={x} y2={H} stroke="var(--line-100)" />;
      }}</For>
      {props.children}
      {props.caption && <text x={PAD} y="14" class="plot-caption">{props.caption}</text>}
    </svg>
  );
}

/*
 * THE ENVELOPE, one gate's worth.
 *
 * Drawn from the stage percentages against the gate's WIDTH, which is the
 * unit the engine stores -- a stage is `value/100 * width_ms`, so the shape
 * survives a change of rate instead of being cut off by it.
 */
export function EnvelopePlot(props) {
  const path = createMemo(() => {
    const p = props.params;
    if (!p) return '';
    const { attack, decay, sustain, release, width, curve } = p;
    /* Each stage as a fraction of the whole step; width is how much of the
     * step the gate is open at all. */
    const a = Math.max(0, attack) / 100 * width;
    const d = Math.max(0, decay) / 100 * width;
    const r = Math.max(0, release) / 100 * width;
    const x = (t) => PAD + (W - 2 * PAD) * Math.min(1, Math.max(0, t));
    const y = (v) => H - PAD - (H - 2 * PAD) * Math.min(1, Math.max(0, v));

    /* The curve's warp, matching env_shape in the engine: linear, exponential,
     * and an s-curve built from two exponentials. */
    const K = 3.0, DEN = 1 - Math.exp(-K);
    const shape = (t) =>
      curve === 1 ? (1 - Math.exp(-K * t)) / DEN
      : curve === 2 ? (t < 0.5
          ? 0.5 * (1 - Math.exp(-K * 2 * t)) / DEN
          : 1 - 0.5 * (1 - Math.exp(-K * 2 * (1 - t))) / DEN)
      : t;

    const pts = [];
    const seg = (t0, t1, v0, v1, n = 24) => {
      for (let i = 0; i <= n; i++) {
        const u = i / n;
        pts.push([x(t0 + (t1 - t0) * u), y(v0 + (v1 - v0) * shape(u))]);
      }
    };
    let t = 0;
    seg(t, t + a, 0, 1); t += a;
    seg(t, t + d, 1, sustain); t += d;
    const holdTo = Math.max(t, width);
    pts.push([x(holdTo), y(sustain)]);
    seg(holdTo, holdTo + r, sustain, 0);
    pts.push([x(1), y(0)]);
    return pts.map(([px, py], i) => `${i ? 'L' : 'M'} ${px.toFixed(2)} ${py.toFixed(2)}`).join(' ');
  });

  return (
    <Well steps={props.steps} caption="ENVELOPE   ONE GATE">
      <path d={path()} fill="none" stroke="var(--uv)" stroke-width="2" />
    </Well>
  );
}

/* THE PATTERN, as the gain the engine will actually apply across one cycle. */
export function PatternPlot(props) {
  const bars = createMemo(() => {
    const n = Math.max(1, props.length ?? 16);
    return Array.from({ length: n }, (_, i) => ({
      i,
      on: !!props.steps?.[i],
      tie: !!props.ties?.[i],
      amt: props.steps?.[i] ? (props.depths?.[i] ?? 1) : 0,
    }));
  });
  const n = () => Math.max(1, props.length ?? 16);
  const bw = () => (W - 2 * PAD) / n();

  return (
    <Well steps={props.length} caption="PATTERN   ONE CYCLE">
      <For each={bars()}>{(b) => (
        <rect
          x={PAD + b.i * bw() + 1} width={Math.max(1, bw() - 2)}
          y={H - PAD - (H - 2 * PAD) * b.amt}
          height={(H - 2 * PAD) * b.amt}
          fill={b.tie ? 'var(--uv-deep)' : 'var(--uv)'}
        />
      )}</For>
      {props.moving && (props.playhead ?? -1) >= 0 && (
        <line
          x1={PAD + (props.playhead + 0.5) * bw()} y1="0"
          x2={PAD + (props.playhead + 0.5) * bw()} y2={H}
          stroke="var(--ink)" stroke-width="1"
        />
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
  const band = (which) => {
    const cols = props.scope;
    if (!cols || !cols.length) return '';
    const mid = H / 2, half = (H - 2 * PAD) / 2;
    const x = (i) => PAD + (W - 2 * PAD) * (i / Math.max(1, cols.length - 1));
    const y = (v) => mid - half * Math.max(-1, Math.min(1, v));
    const hi = cols.map((c, i) => `${i ? 'L' : 'M'} ${x(i).toFixed(1)} ${y(c[which * 2 + 1]).toFixed(1)}`);
    const lo = cols.slice().reverse()
      .map((c, k) => `L ${x(cols.length - 1 - k).toFixed(1)} ${y(c[which * 2]).toFixed(1)}`);
    return hi.join(' ') + ' ' + lo.join(' ') + ' Z';
  };

  return (
    <Well steps={props.length} caption="SIGNAL   DRY BEHIND, GATED IN FRONT">
      <line x1={PAD} y1={H / 2} x2={W - PAD} y2={H / 2} stroke="var(--line-100)" />
      <path d={band(0)} fill="var(--ink-dim)" opacity="0.45" />
      <path d={band(1)} fill="var(--uv)" />
    </Well>
  );
}

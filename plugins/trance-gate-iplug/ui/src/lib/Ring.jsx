/*
 * The pattern as a ring. Copyright (c) 2026 Torben Gräber. MIT.
 *
 * ANNULAR SECTORS, NOT STROKED ARCS — a segment is a wedge you can read at a
 * glance rather than a tick. Every radius is a fraction of 240 so the same
 * drawing works at any size, exactly as the JUCE original did.
 */
import { For } from 'solid-js';

const TAU = Math.PI * 2;

export default function Ring(props) {
  const d = () => props.size ?? 240;
  const n = () => Math.max(1, props.length ?? 16);

  const geom = () => {
    const D = d(), c = D / 2;
    const rOuter = D * (114 / 240);
    /*
     * STROKE FALLS AWAY AS THE COUNT RISES. At 128 steps the slot is 2.8
     * degrees, and a fixed twelve pixels of depth on a 108px radius is wider
     * than the slot -- the segments merge into a solid disc and the pattern
     * stops being readable, which is the one thing the ring is for.
     */
    const depth = Math.min(34, Math.max(8, 900 / n())) * (D / 240);
    const rInner = Math.max(0, rOuter - depth);
    return { c, rOuter, rInner, slot: TAU / n(), fill: (TAU / n()) * 0.8 };
  };

  /* An annular sector as a path: out along the outer edge, back along the
   * inner one. */
  const sector = (i) => {
    const { c, rOuter, rInner, slot, fill } = geom();
    /* -90deg so step 0 sits at the top, where a pattern starts. */
    const a0 = i * slot + (slot - fill) / 2 - Math.PI / 2;
    const a1 = a0 + fill;
    const p = (r, a) => [c + r * Math.cos(a), c + r * Math.sin(a)];
    const [x0, y0] = p(rOuter, a0), [x1, y1] = p(rOuter, a1);
    const [x2, y2] = p(rInner, a1), [x3, y3] = p(rInner, a0);
    const large = fill > Math.PI ? 1 : 0;
    return `M ${x0} ${y0} A ${rOuter} ${rOuter} 0 ${large} 1 ${x1} ${y1}
            L ${x2} ${y2} A ${rInner} ${rInner} 0 ${large} 0 ${x3} ${y3} Z`;
  };

  const head = () => {
    const { c, rOuter, rInner, slot } = geom();
    const a = ((props.playhead ?? 0) + 0.5) * slot - Math.PI / 2;
    const r = (rOuter + rInner) / 2;
    return { x: c + r * Math.cos(a), y: c + r * Math.sin(a), s: d() * (7 / 240) / 2 };
  };

  return (
    <svg class="ring" width={d()} height={d()} viewBox={`0 0 ${d()} ${d()}`}>
      <For each={Array.from({ length: n() }, (_, i) => i)}>{(i) => (
        <path
          d={sector(i)}
          /* uv is THE SIGNAL and is used for literally that: the step is on.
           * A tie is the same wedge at the halo colour, so a held step reads
           * as related to an on one rather than as a third unrelated state. */
          fill={props.steps?.[i]
            ? (props.ties?.[i] ? 'var(--uv-deep)' : 'var(--uv)')
            : 'var(--line-200)'}
          opacity={props.steps?.[i] ? (0.35 + 0.65 * (props.depths?.[i] ?? 1)) : 1}
        />
      )}</For>
      {props.moving && (props.playhead ?? -1) >= 0 && (
        <circle cx={head().x} cy={head().y} r={head().s} fill="var(--ink)" />
      )}
    </svg>
  );
}

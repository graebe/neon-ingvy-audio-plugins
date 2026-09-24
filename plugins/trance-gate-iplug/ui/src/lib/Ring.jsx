/*
 * The pattern as a ring. Copyright (c) 2026 Torben Gräber. MIT.
 * Ported from RingDisplay::paint.
 *
 * ANNULAR SECTORS, NOT STROKED ARCS -- a segment is a wedge you can read at a
 * glance rather than a tick. Radii are fractions of 240 so the same drawing
 * works at any size.
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
     * THE BAND NARROWS AS THE COUNT RISES, FROM THE INSIDE.
     *
     * A fat wedge is the point at 16 steps. At 128 the slot is 2.8 degrees
     * and the gap between wedges is about a pixel, so a 34px band would read
     * as one solid annulus and the pattern -- the only thing the ring is for
     * -- would be gone. Pulling the INNER edge back keeps the outer circle
     * fixed, so the ring does not appear to change size with Length.
     */
    const depth = Math.min(34, Math.max(8, 900 / n())) * (D / 240);
    const rInner = Math.max(0, rOuter - depth);
    const slot = TAU / n();
    return { c, rOuter, rInner, slot, fill: slot * 0.8 };
  };

  /* -90deg because SVG trig puts 0 at three o'clock, where JUCE's
   * addPieSegment already starts at top-centre. */
  const sector = (i) => {
    const { c, rOuter, rInner, slot, fill } = geom();
    const a0 = i * slot + (slot - fill) / 2 - Math.PI / 2;
    const a1 = a0 + fill;
    const p = (r, a) => [c + r * Math.cos(a), c + r * Math.sin(a)];
    const [x0, y0] = p(rOuter, a0), [x1, y1] = p(rOuter, a1);
    const [x2, y2] = p(rInner, a1), [x3, y3] = p(rInner, a0);
    const lg = fill > Math.PI ? 1 : 0;
    return `M ${x0} ${y0} A ${rOuter} ${rOuter} 0 ${lg} 1 ${x1} ${y1} L ${x2} ${y2} A ${rInner} ${rInner} 0 ${lg} 0 ${x3} ${y3} Z`;
  };

  /*
   * THE PLAYHEAD, AS ITS OWN MARK. A dot on the ring's INNER edge at the step
   * being played -- ink, because it is "here" rather than "on", and the one
   * place the ring spends the brightest token.
   *
   * It is separate because uv and ink are 1.34:1 by construction: shading the
   * wedge worked only while the signal was dim enough for ink to be visibly
   * brighter, and it is not any more.
   */
  const head = () => {
    const { c, rOuter, rInner, slot } = geom();
    const a = ((props.playhead ?? 0) + 0.5) * slot - Math.PI / 2;
    const sz = d() * (7 / 240);
    const r = rInner - sz;
    return { x: c + Math.cos(a) * r, y: c + Math.sin(a) * r, r: sz / 2 };
  };

  return (
    <svg class="ring" width={d()} height={d()} viewBox={`0 0 ${d()} ${d()}`}>
      {/* THE WEDGE SAYS ON OR OFF, AND NOTHING ELSE. An unlit sector is the
        * rail, not dim text: line-200 is the token for "the unlit part of an
        * arc", which is exactly what this is. */}
      <For each={Array.from({ length: n() }, (_, i) => i)}>{(i) => (
        <path d={sector(i)} fill={props.steps?.[i] ? 'var(--uv)' : 'var(--line-200)'} />
      )}</For>

      {props.moving && (props.playhead ?? -1) >= 0 && (
        <circle class="glow-led" cx={head().x} cy={head().y} r={head().r} fill="var(--ink)" />
      )}

      {/* The window's one readout-size number, with a label under it. */}
      <text class="t-readout ring-centre" x={d() / 2} y={d() / 2}
            text-anchor="middle" dominant-baseline="middle">{props.centre}</text>
      <text class="t-label ring-label" x={d() / 2} y={d() / 2 + 22}
            text-anchor="middle" dominant-baseline="hanging">{props.label}</text>
    </svg>
  );
}

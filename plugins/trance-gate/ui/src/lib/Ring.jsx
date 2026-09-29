/*
 * The pattern as a ring. Copyright (c) 2026 Torben Gräber. MIT.
 * Ported from RingDisplay::paint, and then given the two things it never had.
 *
 * ANNULAR SECTORS, NOT STROKED ARCS -- a segment is a wedge you can read at a
 * glance rather than a tick. Radii are fractions of 240 so the same drawing
 * works at any size.
 *
 * IT EDITS NOW. RingDisplay had `paint` and nothing else -- the only mouse
 * handlers in the JUCE editor were the grid, the tabs and the knob -- so this
 * is new behaviour rather than a port. The rules come from steps.js, shared
 * with the pads, because they are the same sixteen steps and two copies of
 * "what a click does to a step" would not stay the same for long.
 */
import { For, Show } from 'solid-js';
import { ringGesture } from './steps.js';

const TAU = Math.PI * 2;

export default function Ring(props) {
  let el;
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
    return { c, rOuter, rInner, band: rOuter - rInner, slot, fill: slot * 0.8 };
  };

  /*
   * One wedge, between two radii. `-90deg` because SVG trig puts 0 at three
   * o'clock, where JUCE's addPieSegment already starts at top-centre.
   *
   * `r0`/`r1` are absolute radii rather than the band's edges, which is what
   * lets a step's AMOUNT be a partial wedge -- see below.
   */
  const sector = (i, r0, r1) => {
    const { c, slot, fill } = geom();
    const a0 = i * slot + (slot - fill) / 2 - Math.PI / 2;
    const a1 = a0 + fill;
    const p = (r, a) => [c + r * Math.cos(a), c + r * Math.sin(a)];
    const [x0, y0] = p(r1, a0), [x1, y1] = p(r1, a1);
    const [x2, y2] = p(r0, a1), [x3, y3] = p(r0, a0);
    const lg = fill > Math.PI ? 1 : 0;
    return `M ${x0} ${y0} A ${r1} ${r1} 0 ${lg} 1 ${x1} ${y1} L ${x2} ${y2} A ${r0} ${r0} 0 ${lg} 0 ${x3} ${y3} Z`;
  };

  const full = (i) => { const g = geom(); return sector(i, g.rInner, g.rOuter); };

  /*
   * THE AMOUNT IS THE LIT DEPTH, GROWING OUTWARD FROM THE INNER EDGE -- the
   * ring's version of the pads' lit height growing from the bottom. Floored at
   * 5% for the same reason `pad-lit` is: a step that is ON must be visible as
   * on however quiet it is.
   */
  const lit = (i) => {
    const g = geom();
    /* THE FADE SCALES THE BAND, exactly as it scales a pad's lit height and for
     * the same reason: the engine scales the step's level by this number, so a
     * step part way in reads as part way in. */
    const w = props.weights?.[i] ?? 1;
    const a = Math.max(0.05, Math.min(1, (props.depths?.[i] ?? 1) * w));
    return sector(i, g.rInner, g.rInner + g.band * a);
  };

  /* An ON step the fade has not reached yet is a HOLLOW arc: it is in the
   * pattern (so not a gap) and it is not sounding (so not a fill). */
  const pending = (i) => !!props.steps?.[i] && (props.weights?.[i] ?? 1) <= 0;

  const state = (i) => {
    const on = !!props.steps?.[i], tie = !!props.ties?.[i];
    if (pending(i)) return { on: false, tie: false };
    return { on: on && !tie, tie };
  };
  const idx = () => Array.from({ length: n() }, (_, i) => i);

  /*
   * POINTER -> STEP.
   *
   * Through the element's own rect rather than the event's offsetX/Y, because
   * `main` carries a CSS scale transform to fit whatever width the WebView
   * hands us: offsets would be in scaled pixels and the viewBox is not. The
   * ratio below converts back.
   *
   * The radial band is given a few pixels of slack either side -- a 1px-tall
   * wedge at 128 steps is otherwise unhittable -- while the hole in the middle
   * stays dead, so the readout is not a button.
   */
  const stepAt = (ev) => {
    if (!el) return -1;
    const r = el.getBoundingClientRect();
    if (!r.width || !r.height) return -1;
    const D = d(), g = geom();
    const x = (ev.clientX - r.left) * (D / r.width) - g.c;
    const y = (ev.clientY - r.top) * (D / r.height) - g.c;
    const rad = Math.hypot(x, y);
    if (rad < g.rInner - 6 || rad > g.rOuter + 6) return -1;
    let a = Math.atan2(y, x) + Math.PI / 2;
    if (a < 0) a += TAU;
    const i = Math.floor(a / g.slot);
    return i >= 0 && i < n() ? i : -1;
  };

  const onDown = (e) => {
    const i = stepAt(e);
    if (i < 0) return;
    ringGesture(i, e, props, stepAt);
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
    const { c, rInner, slot } = geom();
    const a = ((props.playhead ?? 0) + 0.5) * slot - Math.PI / 2;
    const sz = d() * (7 / 240);
    const r = rInner - sz;
    return { x: c + Math.cos(a) * r, y: c + Math.sin(a) * r, r: sz / 2 };
  };

  return (
    <svg ref={el} class="ring" width={d()} height={d()} viewBox={`0 0 ${d()} ${d()}`}
         onPointerDown={onDown}>
      {/* THE RAIL FIRST, UNDER EVERYTHING: line-200 is the system's token for
        * "the unlit part of an arc", which is exactly what an off step is. It
        * is drawn for every step including the lit ones, so a partial amount
        * has a track to sit in. */}
      <For each={idx()}>{(i) => (
        <path d={full(i)} fill="var(--line-200)" />
      )}</For>

      {/*
        * THE HALO, AND WHY IT IS HERE AT ALL.
        *
        * RingDisplay left the wedges bare, with a note saying a constant-alpha
        * stroke "reads as a BORDER around every lit wedge rather than as light
        * coming off one" -- and that the answer was a FALLOFF rather than a
        * stroke, which JUCE could not afford: a real blur there cost an
        * offscreen buffer per frame.
        *
        * An SVG drop-shadow IS that falloff, and it is free here. So this is
        * the one place the port deliberately exceeds the original instead of
        * matching it.
        *
        * ONE FILTER FOR THE WHOLE GROUP, not one per wedge: at 128 steps that
        * is the difference between a single blur pass and 128 of them.
        */}
      <g class="glow-led">
        {/*
          * <Show>, NOT `state(i).on && ...`, AND THE DIFFERENCE IS NOT STYLE.
          *
          * A <For> callback's RETURN VALUE is evaluated once per item -- For
          * memoises per item and only re-runs the callback when that item's
          * identity changes, which an index never does. So a bare `cond &&
          * <path/>` there is read exactly once, at first render, when `steps`
          * is still the empty array the signal starts as: every wedge resolved
          * to false and none of them ever came back. The ring drew its rail and
          * nothing else.
          *
          * (The rail loop above is fine because its content is unconditional
          * and only its ATTRIBUTES are dynamic, and attributes are reactive.
          * The pads get away with the same `&&` because theirs sits in a
          * CHILDREN position, which Solid wraps in an effect.)
          *
          * <Show> is a reactive boundary, so the condition is tracked.
          */}
        <For each={idx()}>{(i) => (
          <Show when={state(i).on}>
            <path d={lit(i)} fill="var(--uv)" />
          </Show>
        )}</For>
        {/* A tie is a hollow uv outline with a bar across it: the step holds
          * through, so it is not a fill. The bar is an arc here rather than a
          * straight line, because the wedge's "middle" is a radius. */}
        <For each={idx()}>{(i) => (
          <Show when={state(i).tie}>
            <path d={full(i)} fill="none" stroke="var(--uv)" stroke-width="1" />
            <path d={sector(i, geom().rInner + geom().band * 0.45,
                               geom().rInner + geom().band * 0.55)}
                  fill="var(--uv)" />
          </Show>
        )}</For>
      </g>

      {/* The cursor -- the step the knobs edit. OUTSIDE the band, as the pad's
        * outline is outside its well, so it cannot be read as a lit step. */}
      {props.cursor >= 0 && props.cursor < n() && (
        <path class="ring-cursor" d={sector(props.cursor, geom().rInner - 3, geom().rOuter + 3)}
              fill="none" stroke="var(--ink)" stroke-width="1" />
      )}

      {props.moving && (props.playhead ?? -1) >= 0 && (
        <circle class="glow-led ring-head" cx={head().x} cy={head().y} r={head().r} fill="var(--ink)" />
      )}

      {/* The window's one readout-size number, with a label under it. */}
      <text class="t-readout ring-centre" x={d() / 2} y={d() / 2}
            text-anchor="middle" dominant-baseline="middle">{props.centre}</text>
      <text class="t-label ring-label" x={d() / 2} y={d() / 2 + 22}
            text-anchor="middle" dominant-baseline="hanging">{props.label}</text>
    </svg>
  );
}

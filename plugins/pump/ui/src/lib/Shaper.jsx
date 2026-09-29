/*
 * THE SHAPE EDITOR: what you asked for, drawn over what actually happened.
 *
 * Two curves in one well, and the difference between them is the point:
 *
 *   the MEASURED trace   a filled region, from the gain the plugin actually
 *                        applied, captured phase-locked so it sits on this same
 *                        axis. It is what the listener heard.
 *   the INTENDED curve   a stroked line, from `duckAt` -- the idealised single
 *                        shot the four parameters describe.
 *
 * They agree while nothing interrupts a duck and diverge the moment something
 * does: a retrigger part way through a recovery anchors on the level the
 * envelope had reached, so the real thing does not retrace the drawn one. No
 * editor drawing can show that, because it depends on when the next trigger
 * arrives -- which is exactly why both are here rather than one.
 *
 * FOUR HANDLES, AND EACH IS A HOST PARAMETER.
 *
 *   start     x -> Delay        "when it begins"
 *   bottom    x -> Attack       "how fast"
 *             y -> Depth        "how far"
 *   holdEnd   x -> Hold
 *   end       x -> Release
 *
 * Dragging one is begin/set/end on that parameter, so it lands in the host's
 * undo history and its automation lane like a knob. That is what
 * PLUG_DOES_STATE_CHUNKS 0 buys, and it is worth more than a free-form curve
 * with no automation behind it.
 *
 * THE HANDLES DO NOT ANIMATE. The design system's Motion section is explicit --
 * "controls never animate" -- and a curve editor whose handles glide to their
 * new positions is one you cannot aim at. Only the playhead moves.
 */
import { For, Show, createMemo } from 'solid-js';
import { Well, Axis, band, INSET, CAPTION, startDrag, setParam, beginGesture, endGesture }
  from '@ultraviolet/ui';
import { MSG, P, toNorm, fromNorm } from './msg.js';
import { duckAt, bounds } from './shape.js';

/*
 * THE AXIS IS ONE CYCLE, and both wells use it.
 *
 * Not "as much as the shape needs": the signal capture is phase-locked to the
 * trigger and spans exactly one cycle, so an editor spanning anything else would
 * put the drawn dip somewhere other than the measured one and the two halves
 * would stop being comparable. A shape longer than a cycle overruns the right
 * edge and is MARKED rather than accommodated -- see the overrun tick.
 */
export const SPAN = 100; /* percent of the cycle */

const HANDLE_R = 4.5;
/* 4px of dead zone before a press becomes a drag: a click is not a drag,
 * however much the hand shakes. steps.js:11-27 has the same rule for the same
 * reason. */
const DEAD_ZONE = 4;
/* The knob's ratio, so fine mode feels the same everywhere. Knob.jsx:24-26. */
const FINE = 5;

export function Shaper(props) {
  const w = () => props.w;
  const h = () => props.h;
  const top = () => CAPTION + 4;
  const bot = () => h() - INSET - 14; /* room for the axis under the curve */
  const plotW = () => w() - 2 * INSET;

  const x = (pct) => INSET + plotW() * (Math.min(SPAN, Math.max(0, pct)) / SPAN);
  /* y is the DUCK, so 0 is the top of the well and 1 is the bottom: the curve
   * goes DOWN as the signal is pushed down, which is the direction the ear
   * expects and the opposite of a gain plot. */
  const y = (duck) => top() + (bot() - top()) * Math.min(1, Math.max(0, duck));

  const p = () => props.shape; /* {curve, delay, attack, hold, release, depth} */
  const b = createMemo(() => bounds(p()));

  /* ---------------------------------------------------- the intended curve */

  const intended = createMemo(() => {
    const s = p();
    const n = Math.max(2, Math.round(plotW()));
    const depth = (s.depth ?? 100) / 100;
    const pts = [];
    for (let i = 0; i <= n; i++) {
      const pct = (i / n) * SPAN;
      pts.push(`${i ? 'L' : 'M'} ${(INSET + (plotW() * i) / n).toFixed(2)} `
        + `${y(duckAt(s, pct) * depth).toFixed(2)}`);
    }
    return pts.join(' ');
  });

  /* ------------------------------------------------- the measured trace ---- */

  /*
   * The capture's gain byte is the MULTIPLIER APPLIED, so the reduction is
   * `1 - gain`. Drawn as a region from the top down, which is the same geometry
   * the intended curve uses -- the two must be read against each other, and two
   * different y mappings would make them incomparable even when they agree.
   */
  const measured = createMemo(() => {
    const cols = props.scope;
    if (!cols || cols.length < 2) return '';
    const seen = props.seen;
    const n = cols.length;
    const width = Math.max(1, Math.round(plotW()));
    const runs = [];
    let run = [];
    const flush = () => {
      if (run.length > 1) {
        const first = run[0];
        const last = run[run.length - 1];
        runs.push(`M ${first[0]} ${y(0).toFixed(2)} `
          + run.map(([px, py]) => `L ${px} ${py}`).join(' ')
          + ` L ${last[0]} ${y(0).toFixed(2)} Z`);
      }
      run = [];
    };
    for (let px = 0; px < width; px++) {
      const a = Math.floor((px * n) / width);
      const bEnd = Math.max(a + 1, Math.floor(((px + 1) * n) / width));
      /* The DEEPEST duck behind this pixel, to match the capture's own
       * minimum-gain rule -- averaging would report a duck nobody heard. */
      let deepest = -1;
      for (let i = a; i < bEnd && i < n; i++) {
        if (seen && !seen(i)) continue;
        const g = cols[i]?.[4];
        if (g === undefined) continue;
        const d = 1 - g;
        if (d > deepest) deepest = d;
      }
      if (deepest < 0) {
        flush();
        continue;
      }
      run.push([INSET + px, y(deepest).toFixed(2)]);
    }
    flush();
    return runs.join(' ');
  });

  /* ------------------------------------------------------------- handles */

  const handles = createMemo(() => {
    const s = p();
    const depth = (s.depth ?? 100) / 100;
    const bb = b();
    return [
      { key: 'start', label: 'DELAY', px: x(bb.start), py: y(0), xIdx: P.delay },
      {
        key: 'bottom',
        label: 'ATTACK',
        px: x(bb.bottom),
        py: y(depth),
        xIdx: P.attack,
        yIdx: P.depth,
      },
      { key: 'holdEnd', label: 'HOLD', px: x(bb.holdEnd), py: y(depth), xIdx: P.hold },
      { key: 'end', label: 'RELEASE', px: x(bb.end), py: y(0), xIdx: P.release },
    ];
  });

  /*
   * A DRAG, AND EVERY RULE IN IT IS A FIX FOR A REAL FAULT.
   *
   *  - The sensitivity is chosen AT PRESS and never re-read. The delta is
   *    measured from the press position, so changing the divisor half way
   *    rescales everything since the press and the value jumps (Knob.jsx:43-54).
   *  - The geometry is measured through getBoundingClientRect, never offsetX --
   *    `main` carries a CSS scale transform, so offsetX is in the wrong units
   *    (Ring.jsx:81-106).
   *  - Listeners go on the WINDOW, via startDrag: a handle is 9px across and a
   *    drag leaves it immediately, and an element listener stops tracking there.
   *  - Both parameters of a two-axis handle are opened and closed together, so
   *    the host sees one gesture and undo is one step rather than two.
   */
  const grab = (hnd, ev, host) => {
    ev.preventDefault();
    ev.stopPropagation();

    /* Double-click resets, via `detail` rather than a dblclick listener -- a
     * separate listener fires after the drag has already moved the value. */
    if (ev.detail === 2) {
      for (const idx of [hnd.xIdx, hnd.yIdx]) {
        if (idx === undefined) continue;
        const d = props.defaults?.[idx];
        if (d === undefined) continue;
        beginGesture(idx);
        setParam(idx, d);
        endGesture(idx);
      }
      return;
    }

    const rect = host.getBoundingClientRect();
    if (!rect.width || !rect.height) return;
    /* The well's own pixels per client pixel, which is what the page's scale
     * transform changes. */
    const sx = w() / rect.width;
    const sy = h() / rect.height;
    const fine = ev.shiftKey ? FINE : 1;

    const startX = ev.clientX;
    const startY = ev.clientY;
    const s0 = { ...p() };
    const bb0 = bounds(s0);

    const idxs = [hnd.xIdx, hnd.yIdx].filter((i) => i !== undefined);
    for (const i of idxs) beginGesture(i);

    let moved = false;
    startDrag(
      (mv) => {
        if (!moved) {
          if (Math.abs(mv.clientX - startX) < DEAD_ZONE
            && Math.abs(mv.clientY - startY) < DEAD_ZONE) return;
          moved = true;
        }
        /* Pixels -> percent of the cycle, through the same mapping `x` uses. */
        const dPct = ((mv.clientX - startX) * sx) / plotW() * SPAN / fine;

        if (hnd.xIdx !== undefined) {
          /*
           * A HANDLE MOVES ITS OWN STAGE, NOT THE ONES AFTER IT. Each boundary
           * is the sum of the stages before it, so dragging `end` must change
           * Release alone -- adding the delta to the boundary and solving would
           * smear it across every earlier stage, and the handle you grabbed
           * would drag the others with it.
           */
          const base = {
            [P.delay]: s0.delay,
            [P.attack]: s0.attack,
            [P.hold]: s0.hold,
            [P.release]: s0.release,
          }[hnd.xIdx];
          setParam(hnd.xIdx, toNorm(hnd.xIdx, base + dPct));
        }
        if (hnd.yIdx !== undefined) {
          const dDuck = ((mv.clientY - startY) * sy) / (bot() - top()) / fine;
          const depth0 = s0.depth ?? 100;
          setParam(hnd.yIdx, toNorm(hnd.yIdx, depth0 + dDuck * 100));
        }
        void bb0;
      },
      () => { for (const i of idxs) endGesture(i); },
    );
  };

  let hostEl;

  /* The overrun: a shape that cannot finish inside one cycle. Marked rather
   * than accommodated, and in amber because it is the window's one warning --
   * the design system allows at most one. */
  const overruns = () => b().end > SPAN + 1e-9;

  return (
    <div class="shaper" ref={hostEl}>
      <Well w={w()} h={h()}
            caption={props.caption
              ?? 'SHAPE   DRAG A HANDLE -- SHIFT FOR FINE, DOUBLE-CLICK TO RESET'}>
        {/* The floor and the ceiling, so a curve that touches either reads as
          * touching a limit rather than as running off the drawing. */}
        <line x1={INSET} x2={w() - INSET} y1={y(0)} y2={y(0)} stroke="var(--line-100)" />
        <line x1={INSET} x2={w() - INSET} y1={y(1)} y2={y(1)} stroke="var(--line-100)" />

        {/* WHAT ACTUALLY HAPPENED, behind. Filled and unstroked so it reads as
          * ground rather than as a second line competing with the first. */}
        <path d={measured()} fill="var(--uv-glow)" stroke="none" />

        {/* WHAT WAS ASKED FOR, in front, at rail weight with the 3px arc halo --
          * `uv` is a near-white and a near-white drawn alone has no cast at all.
          * The 10px LED halo blooms into mud on a line this long. */}
        <g class="glow-arc">
          <path d={intended()} fill="none" stroke="var(--uv)" stroke-width="2" />
        </g>

        {/* The playhead, an ink hairline: `uv` and `ink` are 1.34:1 by
          * construction, so it must differ from the curve by POSITION and
          * colour rather than by brightness. */}
        <Show when={props.sweep !== undefined && props.sweep !== null}>
          <line x1={x(props.sweep * SPAN)} x2={x(props.sweep * SPAN)}
                y1={top()} y2={bot()} stroke="var(--ink)" opacity="0.6" />
        </Show>

        <Show when={overruns()}>
          <line x1={w() - INSET} x2={w() - INSET} y1={top()} y2={bot()}
                stroke="var(--amber)" stroke-width="2" />
        </Show>

        {/* Inside a <For>, a conditional child MUST be a <Show>: a For
          * callback's return value is evaluated once, when the signal is still
          * its empty initial value, so `cond && <g/>` resolves to false and
          * never comes back. Ring.jsx:159-176 -- the ring drew its rail and
          * nothing else. */}
        <For each={handles()}>{(hnd) => (
          <g class="handle" onPointerDown={(e) => grab(hnd, e, hostEl)}>
            {/* A generous invisible target over a small visible dot: 9px is
              * under any reasonable hit area, and a handle you cannot grab reads
              * as a handle that does not work. */}
            <circle cx={hnd.px} cy={hnd.py} r={HANDLE_R * 3} fill="transparent" />
            <circle cx={hnd.px} cy={hnd.py} r={HANDLE_R}
                    fill="var(--ink)" stroke="var(--bg-000)" />
          </g>
        )}</For>

        <Axis w={w()} y={h() - INSET - 14} spanMs={props.spanMs ?? 0}
              markMs={props.markMs ?? 0} />
      </Well>
    </div>
  );
}

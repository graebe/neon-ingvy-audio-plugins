// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * THE PLOT: one well spanning exactly one cycle, the shape you drag in it.
 *
 * In the plugin this well also holds the audio the shape produced -- the input
 * behind, the output in front, the measured gain as a ghost line -- because
 * the plugin streams its capture to the editor. The Move streams nothing to
 * the page (Schwung's manager carries parameter values and the module's
 * feeds, no audio), so here the well holds what the module CAN say: the shape
 * the engine is running, where it is in the cycle, and the cycle's length.
 * Without the audio the shape is the subject, and nothing competes with it.
 *
 * WHAT IS LEFT, BACK TO FRONT:
 *
 *   the zero line  line-100, so a duck that pinches to silence reads as one
 *   the shape      what you ASKED for: an ink line on a bg-000 casing,
 *                  mirrored about the centre as the plugin draws it -- the
 *                  ceiling a full-scale signal could reach
 *   the playhead   the module's sweep, a line-200 rule (the PlotWell card's
 *                  position mark)
 *   the overrun    a shape longer than a cycle, marked in amber at the edge
 *                  it runs past rather than accommodated
 *   the handles    four, each a parameter
 *   the ruler      the cycle in ms, with the instant the duck reaches its
 *                  floor as its landmark
 *
 * FOUR HANDLES, AND EACH IS A PARAMETER (layout.js HANDLES):
 *
 *   start     x -> Delay        "when it begins"
 *   bottom    x -> Attack       "how fast"
 *             y -> Depth        "how far"
 *   holdEnd   x -> Hold
 *   end       x -> Release
 *
 * THE HANDLES DO NOT ANIMATE -- the design system's Motion section is
 * explicit, and a curve editor whose handles glide is one you cannot aim at.
 * Only the playhead moves.
 */
import { Index, Show, createMemo, createSignal } from 'solid-js';
import { Well, Axis, INSET, CAPTION, startDrag, setParam, beginGesture, endGesture, sliderKey, infoAttrs }
  from '@ultraviolet/ui';
import { duckAt, bounds } from './shape.js';
import { HANDLES } from './layout.js';

/**
 * THE AXIS IS ONE CYCLE. The engine's sweep is a position in one cycle, so
 * anything else would put the playhead somewhere the duck is not. A shape
 * longer than a cycle overruns the right edge and is MARKED.
 */
export const SPAN = 100; /* percent of the cycle */

const AXIS_H = 14;
const HANDLE_R = 4.5;
/* 4px of dead zone before a press becomes a drag: a click is not a drag,
 * however much the hand shakes. */
const DEAD_ZONE = 4;
/* The knob's ratio, so fine mode feels the same everywhere. */
const FINE = 5;

export function Shaper(props) {
  const w = () => props.w;
  const h = () => props.h;

  /* The drawing area: bipolar, centred, with the caption above and the axis
   * below. */
  const top = () => CAPTION + 4;
  const bot = () => h() - INSET - AXIS_H;
  const mid = () => (top() + bot()) / 2;
  const half = () => (bot() - top()) / 2;
  const plotW = () => w() - 2 * INSET;

  const x = (pct) => INSET + plotW() * (Math.min(SPAN, Math.max(0, pct)) / SPAN);
  /* A GAIN, mirrored about the centre: 1 reaches the well's edges, 0 pinches
   * to the centre line. */
  const yTop = (g) => mid() - half() * Math.min(1, Math.max(0, g));
  const yBot = (g) => mid() + half() * Math.min(1, Math.max(0, g));

  /* While a handle is held the drawing follows the parameters the drag is
   * writing (`heldShape`), not the engine's feed, which lags by up to 150 ms
   * on the Move. */
  const [held, setHeld] = createSignal(false);
  const p = () => (held() && props.heldShape ? props.heldShape : props.shape);
  const b = createMemo(() => bounds(p()));
  /* Depth in its declared unit, 0..1: the engine's own. */
  const depth = () => p().depth ?? 1;

  /*
   * WHAT WAS ASKED FOR. TWO SUBPATHS IN ONE `d`, not a closed region: closing
   * it would draw verticals at both ends, where the gain is 1 and the two
   * edges are a whole well apart.
   */
  const intended = createMemo(() => {
    const s = p();
    const d = depth();
    const n = Math.max(2, Math.round(plotW()));
    const up = [];
    const dn = [];
    for (let i = 0; i <= n; i++) {
      const px = (INSET + (plotW() * i) / n).toFixed(2);
      const g = 1 - d * duckAt(s, (i / n) * SPAN);
      up.push(`${i ? 'L' : 'M'} ${px} ${yTop(g).toFixed(2)}`);
      dn.push(`${i ? 'L' : 'M'} ${px} ${yBot(g).toFixed(2)}`);
    }
    return `${up.join(' ')} ${dn.join(' ')}`;
  });

  const handles = createMemo(() => {
    const bb = b();
    const floor = 1 - depth(); /* the gain the duck bottoms out at */
    const at = { start: [bb.start, 1], bottom: [bb.bottom, floor],
                 holdEnd: [bb.holdEnd, floor], end: [bb.end, 1] };
    return HANDLES.map((hnd) => ({ ...hnd, px: x(at[hnd.key][0]), py: yTop(at[hnd.key][1]) }));
  });

  /*
   * A DRAG, AND EVERY RULE IN IT IS A FIX FOR A REAL FAULT.
   *
   *  - The sensitivity is chosen AT PRESS and never re-read: the delta is
   *    measured from the press position, so changing the divisor half way
   *    rescales everything since the press and the value jumps.
   *  - Geometry through getBoundingClientRect, never offsetX -- the window
   *    carries a CSS scale transform, so offsetX is in the wrong units.
   *  - Listeners on the WINDOW, via startDrag: a handle is 9px across and a
   *    drag leaves it immediately, where an element listener stops tracking.
   *  - Both parameters of a two-axis handle open and close together, so a
   *    host that records gestures sees one (Schwung's records none, and the
   *    kit makes its gestures no-ops; the rule costs nothing there).
   */
  const grab = (hnd, ev, host) => {
    ev.preventDefault();
    ev.stopPropagation();

    const rect = host.getBoundingClientRect();
    if (!rect.width || !rect.height) return;
    const sx = w() / rect.width;
    const sy = h() / rect.height;
    const fine = ev.shiftKey ? FINE : 1;

    const startX = ev.clientX;
    const startY = ev.clientY;
    const s0 = { ...p() };
    const idxs = [hnd.xIdx, hnd.yIdx].filter((i) => i !== undefined);
    /* The stage the sideways drag moves, as it was at the press. */
    const xBase = s0[hnd.stage];

    /* THE GESTURE OPENS ONCE THE DEAD ZONE IS LEFT, not at the press: a press
     * that moves nothing -- a click, either half of a double-click -- is no
     * edit. */
    let moved = false;
    startDrag(
      (mv) => {
        if (!moved) {
          if (Math.abs(mv.clientX - startX) < DEAD_ZONE
            && Math.abs(mv.clientY - startY) < DEAD_ZONE) return;
          moved = true;
          for (const i of idxs) beginGesture(i);
          setHeld(true);
        }
        /*
         * A HANDLE MOVES ITS OWN STAGE, NOT THE ONES AFTER IT. Each boundary
         * is the sum of the stages before it, so dragging `end` must change
         * Release alone -- adding the delta to the boundary and solving would
         * smear it across every earlier stage.
         */
        const dPct = (((mv.clientX - startX) * sx) / plotW()) * SPAN / fine;
        setParam(hnd.xIdx, props.toNorm(hnd.xIdx, xBase + dPct));
        if (hnd.yIdx !== undefined) {
          /*
           * `half`, NOT the well's height. The handle sits on the TOP edge of
           * a mirrored envelope, so a pixel of travel is a pixel of gain
           * against the half-height. Down is deeper.
           */
          const dDepth = ((mv.clientY - startY) * sy) / half() / fine;
          setParam(hnd.yIdx, props.toNorm(hnd.yIdx, (s0.depth ?? 1) + dDepth));
        }
      },
      () => {
        if (!moved) return;
        for (const i of idxs) endGesture(i);
        setHeld(false);
      },
    );
  };

  /*
   * Double-click resets, to the module's own default for each axis the handle
   * moves. The `dblclick` event rather than pointerdown's `detail`, which only
   * WebKit fills in.
   */
  const reset = (hnd, ev) => {
    ev.preventDefault();
    ev.stopPropagation();
    for (const idx of [hnd.xIdx, hnd.yIdx]) if (idx !== undefined) props.onReset?.(idx);
  };

  let hostEl;

  /*
   * THE KEYBOARD. Each handle is a slider on the parameter it drags: Left and
   * Right move it along the cycle, and on the bottom handle Up and Down move
   * the depth -- up is a shallower duck, as dragging up is. Each key is one
   * committed edit.
   */
  const onKey = (hnd, e) => {
    const clamp = (v) => Math.min(1, Math.max(0, v));
    const apply = (idx, k, sign = 1) => {
      if (idx === undefined || !k) return false;
      const now = props.value?.(idx) ?? 0;
      props.onCommit?.(idx, k.to !== undefined ? k.to : clamp(now + sign * k.delta));
      return true;
    };
    const done = hnd.yIdx !== undefined && (e.key === 'ArrowUp' || e.key === 'ArrowDown')
      ? apply(hnd.yIdx, sliderKey(e, 'y'), -1)
      : apply(hnd.xIdx, sliderKey(e, 'x'));
    if (done) e.preventDefault();
  };
  /* What a reader hears: the stage as its knob reads it, then the depth. */
  const valueText = (hnd) => [hnd.xIdx, hnd.yIdx].filter((i) => i !== undefined)
    .map((i) => props.text?.(i) ?? '').filter(Boolean).join(', ');

  /*
   * A shape that cannot finish inside one cycle. THE UNWRAPPED SPAN, not the
   * end: with an early delay the end wraps round to a small number, and
   * testing that would report every early duck as fitting.
   */
  const overruns = () => b().span > SPAN + 1e-9;

  return (
    <div class="shaper" ref={hostEl}>
      <Well w={w()} h={h()} caption={props.caption} info={props.info}>
        <line x1={INSET} x2={w() - INSET} y1={mid()} y2={mid()} stroke="var(--line-100)" />

        {/* `ink` on a `bg-000` casing: the casing keeps the line legible where
          * it crosses the playhead and the zero line. */}
        <path d={intended()} fill="none" stroke="var(--bg-000)" stroke-width="4"
              opacity="0.65" stroke-linecap="round" />
        <path class="intended" d={intended()} fill="none" stroke="var(--ink)" stroke-width="1.5"
              stroke-linecap="round" />

        <Show when={props.sweep !== undefined && props.sweep !== null}>
          <line class="sweep" x1={x(props.sweep * SPAN)} x2={x(props.sweep * SPAN)}
                y1={top()} y2={bot()} stroke="var(--line-200)" stroke-width="1" />
        </Show>

        <Show when={overruns()}>
          <line class="overrun" x1={w() - INSET} x2={w() - INSET} y1={top()} y2={bot()}
                stroke="var(--amber)" stroke-width="2" />
        </Show>

        {/* <Index>, keyed by position: the four handles are the same four
          * elements for the window's life, so one keeps focus while the keys
          * move it. */}
        <Index each={handles()}>{(hd) => (
          <g class="handle" data-handle={hd().key} {...infoAttrs(hd().info)}
             onPointerDown={(e) => grab(hd(), e, hostEl)}
             onDblClick={(e) => reset(hd(), e)}
             tabindex="0" role="slider" aria-label={hd().name}
             aria-valuemin="0" aria-valuemax="100"
             aria-valuenow={Math.round((props.value?.(hd().xIdx) ?? 0) * 100)}
             aria-valuetext={valueText(hd())}
             onKeyDown={(e) => onKey(hd(), e)}>
            {/* A generous invisible target over a small visible dot: a handle
              * you cannot grab reads as a handle that does not work. */}
            <circle cx={hd().px} cy={hd().py} r={HANDLE_R * 3} fill="transparent" />
            <circle cx={hd().px} cy={hd().py} r={HANDLE_R}
                    fill="var(--ink)" stroke="var(--bg-000)" stroke-width="1.5" />
          </g>
        )}</Index>

        <Axis w={w()} y={h() - INSET - AXIS_H} spanMs={props.spanMs ?? 0}
              markMs={props.markMs ?? 0} />
      </Well>
    </div>
  );
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * THE PLOT: one well, the audio behind, the shape in front.
 *
 * It was two wells stacked on a shared axis, which was already a compromise --
 * the shape and the audio it shaped are ONE fact, and reading it meant looking
 * in two places and trusting that the x axes matched. Joined, the relationship
 * is not something the reader has to assemble: the ducked waveform sits INSIDE
 * the gain envelope that produced it, because it is that envelope multiplied by
 * the input.
 *
 * FOUR LAYERS, BACK TO FRONT, AND EACH ANSWERS A DIFFERENT QUESTION:
 *
 *   the input      grey, at half alpha. What arrived. Context, not the subject:
 *                  a sustained input fills every column edge to edge, and at
 *                  full strength it is a slab the rest has to fight through.
 *   the envelope   a faint violet region -- the gain actually applied, mirrored
 *                  about the centre so it reads as the CEILING the output can
 *                  reach. It is drawn from the measured gain rather than from
 *                  the controls, so it is there even when the track is silent
 *                  and there is no waveform to infer it from.
 *   the output     violet, the audio that left. It fills the envelope from the
 *                  inside wherever the input is loud enough to reach it.
 *   the shape      a white line with handles: what you ASKED for.
 *
 * THE LINE AND THE ENVELOPE AGREE until something interrupts a duck, and then
 * they do not -- a trigger part way through a recovery anchors on the level the
 * envelope had actually reached. No drawing can predict that, which is why both
 * are here.
 *
 * THE SHAPE IS `ink` AND THE AUDIO IS `uv`, and that is not decoration. Ring.jsx
 * records the reason: those two are 1.34:1 by construction, so they must differ
 * by something other than brightness or they blur into one picture. Here they
 * differ by hue AND by kind -- a thin line with a dark casing over filled bands.
 *
 * FOUR HANDLES, AND EACH IS A HOST PARAMETER:
 *
 *   start     x -> Delay        "when it begins"
 *   bottom    x -> Attack       "how fast"
 *             y -> Depth        "how far"
 *   holdEnd   x -> Hold
 *   end       x -> Release
 *
 * Dragging one is begin/set/end on that parameter, so it lands in the host's
 * undo history and its automation lane like a knob.
 *
 * THE HANDLES DO NOT ANIMATE -- the design system's Motion section is explicit,
 * and a curve editor whose handles glide is one you cannot aim at. Only the
 * playhead and the audio move.
 */
import { Index, Show, createMemo, createSignal } from 'solid-js';
import { Well, Axis, band, INSET, CAPTION, startDrag, setParam, beginGesture, endGesture, sliderKey }
  from '@ultraviolet/ui';
import { P, toNorm } from './msg.js';
import { duckAt, bounds } from './shape.js';
import { COL } from './scope.js';

/**
 * THE AXIS IS ONE CYCLE.
 *
 * Not "as much as the shape needs": the capture is phase-locked to the trigger
 * and spans exactly one cycle, so anything else would put the drawn dip
 * somewhere other than the measured one. A shape longer than a cycle overruns
 * the right edge and is MARKED rather than accommodated.
 */
export const SPAN = 100; /* percent of the cycle */

const AXIS_H = 14;
const HANDLE_R = 4.5;
/* 4px of dead zone before a press becomes a drag: a click is not a drag,
 * however much the hand shakes. steps.js:11-27, same rule. */
const DEAD_ZONE = 4;
/* The knob's ratio, so fine mode feels the same everywhere. Knob.jsx:24-26. */
const FINE = 5;

export function Shaper(props) {
  const w = () => props.w;
  const h = () => props.h;

  /* The audio area: bipolar, centred, with the caption above and the axis
   * below. Every layer shares it -- that sharing IS the joined plot. */
  const top = () => CAPTION + 4;
  const bot = () => h() - INSET - AXIS_H;
  const mid = () => (top() + bot()) / 2;
  const half = () => (bot() - top()) / 2;
  const plotW = () => w() - 2 * INSET;

  const geom = () => ({ x0: INSET, w: Math.max(1, Math.round(plotW())),
                        top: top(), bottom: bot() });

  const x = (pct) => INSET + plotW() * (Math.min(SPAN, Math.max(0, pct)) / SPAN);
  /* A GAIN, mirrored about the centre: 1 reaches the well's edges, 0 pinches to
   * the centre line. That is the same mapping the waveform uses, which is the
   * whole reason the output can be seen to fill the envelope. */
  const yTop = (g) => mid() - half() * Math.min(1, Math.max(0, g));
  const yBot = (g) => mid() + half() * Math.min(1, Math.max(0, g));

  /* While a handle is held the drawing follows the parameters the drag is
   * writing (`heldShape`), not the engine's readout, which lags by a tick. */
  const [held, setHeld] = createSignal(false);
  const p = () => (held() && props.heldShape ? props.heldShape : props.shape);
  const b = createMemo(() => bounds(p()));
  const depth = () => (p().depth ?? 100) / 100;

  /* ------------------------------------------------- what was asked for ---- */

  /*
   * TWO SUBPATHS IN ONE `d`, not a closed region: closing it would draw
   * verticals at both ends, where the gain is 1 and the two edges are a whole
   * well apart. An open pair is the shape that is actually meant.
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

  /* --------------------------------------------------- what happened ------- */

  /*
   * THE MEASURED ENVELOPE, from the capture's gain byte rather than from the
   * controls -- so it is there when the track is silent, which is the one state
   * a waveform cannot describe.
   *
   * AN OUTLINE, NOT A REGION, and that was a real mistake first time round. As a
   * filled `uv-glow` area it became the largest mass in the picture: it buried
   * the grey input behind it, it competed with the audio it was supposed to
   * frame, and `uv-glow`'s own token says "alpha only; never a fill". It is also
   * the least informative layer here, because it agrees with the white line
   * almost always -- a thin line is exactly right for something whose job is to
   * be invisible until it diverges.
   *
   * Two subpaths rather than a closed region, for the same reason the intended
   * curve uses them: closing it would draw verticals a whole well apart.
   */
  const measured = createMemo(() => {
    const cap = props.scope;
    if (!cap || cap.count < 2) return '';
    const seen = props.seen;
    const n = cap.count;
    const width = Math.max(1, Math.round(plotW()));
    const runs = [];
    let up = [];
    let dn = [];
    const flush = () => {
      if (up.length > 1) runs.push(`${up.join(' ')} ${dn.join(' ')}`);
      up = [];
      dn = [];
    };
    for (let px = 0; px < width; px++) {
      const a = Math.floor((px * n) / width);
      const bEnd = Math.max(a + 1, Math.floor(((px + 1) * n) / width));
      /* The DEEPEST duck behind this pixel -- the lowest gain -- to match the
       * capture's own minimum rule. Averaging would report a duck nobody heard. */
      let lowest = 2;
      for (let i = a; i < bEnd && i < n; i++) {
        if (seen && !seen(i)) continue;
        const g = cap.data[i * cap.stride + COL.gain];
        if (g < lowest) lowest = g;
      }
      if (lowest > 1) {
        flush();
        continue;
      }
      const sx = INSET + px;
      const lead = up.length ? 'L' : 'M';
      up.push(`${lead} ${sx} ${yTop(lowest).toFixed(2)}`);
      dn.push(`${lead} ${sx} ${yBot(lowest).toFixed(2)}`);
    }
    flush();
    return runs.join(' ');
  });

  /* ------------------------------------------------------------- handles --- */

  const handles = createMemo(() => {
    const bb = b();
    const floor = 1 - depth(); /* the gain the duck bottoms out at */
    return [
      { key: 'start', name: 'Delay', px: x(bb.start), py: yTop(1), xIdx: P.delay },
      { key: 'bottom', name: 'Attack and depth', px: x(bb.bottom), py: yTop(floor),
        xIdx: P.attack, yIdx: P.depth },
      { key: 'holdEnd', name: 'Hold', px: x(bb.holdEnd), py: yTop(floor), xIdx: P.hold },
      { key: 'end', name: 'Release', px: x(bb.end), py: yTop(1), xIdx: P.release },
    ];
  });

  /*
   * A DRAG, AND EVERY RULE IN IT IS A FIX FOR A REAL FAULT.
   *
   *  - The sensitivity is chosen AT PRESS and never re-read: the delta is
   *    measured from the press position, so changing the divisor half way
   *    rescales everything since the press and the value jumps (Knob.jsx:43-54).
   *  - Geometry through getBoundingClientRect, never offsetX -- `main` carries a
   *    CSS scale transform, so offsetX is in the wrong units (Ring.jsx:81-106).
   *  - Listeners on the WINDOW, via startDrag: a handle is 9px across and a drag
   *    leaves it immediately, where an element listener stops tracking.
   *  - Both parameters of a two-axis handle open and close together, so the host
   *    sees one gesture and undo is one step rather than two.
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

    /* THE GESTURE OPENS ONCE THE DEAD ZONE IS LEFT, not at the press: a press
     * that moves nothing -- a click, either half of a double-click -- is no
     * edit and tells the host nothing. */
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
          /*
           * `half`, NOT the well's height. The handle sits on the TOP edge of a
           * mirrored envelope, so a pixel of travel is a pixel of gain against
           * the half-height -- using the full height would make the control
           * twice as slow as it looks.
           */
          const dDepth = ((mv.clientY - startY) * sy) / half() / fine;
          setParam(hnd.yIdx, toNorm(hnd.yIdx, (s0.depth ?? 100) + dDepth * 100));
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
   * Double-click resets, to the plugin's own default for each axis the handle
   * moves. The `dblclick` event rather than pointerdown's `detail`, which only
   * WebKit fills in -- Chromium leaves it at 0, so the reset did nothing there.
   * It cannot land after a drag has moved the value: two presses inside the
   * dead zone move nothing, and a press that left it is not a double-click.
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
  const valueText = (hnd) => [hnd.xIdx, hnd.yIdx].filter((i) => i !== undefined)
    .map((i) => props.text?.(i) ?? '').filter(Boolean).join(', ');

  /*
   * A shape that cannot finish inside one cycle. Marked rather than
   * accommodated, and in amber because it is the window's one warning.
   *
   * THE UNWRAPPED SPAN, not the end: with an early delay the end wraps round to
   * a small number, and testing that would report every early duck as fitting
   * however long it actually is.
   */
  const overruns = () => b().span > SPAN + 1e-9;

  const caption = () => (props.quiet
    ? 'ONE CYCLE   NOTHING REACHING THE PLUGIN'
    : 'ONE CYCLE   INPUT IN GREY BEHIND   DRAG A HANDLE, SHIFT FOR FINE');

  return (
    <div class="shaper" ref={hostEl}>
      <Well w={w()} h={h()} caption={caption()}>
        {/* The zero line, so a silent stretch reads as silence rather than as a
          * gap in the drawing. */}
        <line x1={INSET} x2={w() - INSET} y1={mid()} y2={mid()}
              stroke="var(--line-100)" />

        {/* 1. WHAT ARRIVED. Context, not the subject, so it goes back at partial
          * alpha -- at full strength it is a slab the rest fights through. The
          * grey shows as a halo around the output wherever the duck took
          * something away, which is precisely where it is worth seeing. */}
        <path d={band(props.scope, COL.dryLo, COL.dryHi, geom(), props.seen)}
              fill="var(--plot-dry)" opacity="0.5" />

        {/* 2. WHAT LEFT. The subject, and the picture's mass. It gets the arc
          * halo -- the same 3px falloff the knob's value arc uses. The 10px LED
          * halo blooms into mud on a trace that fills the well. */}
        <g class="glow-arc">
          <path d={band(props.scope, COL.wetLo, COL.wetHi, geom(), props.seen)} fill="var(--uv)" />
        </g>

        {/* 3. THE CEILING the output was allowed to reach, measured. A line, not
          * a region -- see the note on `measured`. */}
        <path d={measured()} fill="none" stroke="var(--uv-deep)" stroke-width="1" />

        {/* 4. WHAT WAS ASKED FOR. `ink` over `uv` is only 1.34:1, so the casing
          * underneath is what separates them -- they differ by kind and by hue
          * rather than by brightness alone. */}
        <path d={intended()} fill="none" stroke="var(--bg-000)" stroke-width="4"
              opacity="0.65" stroke-linecap="round" />
        <path d={intended()} fill="none" stroke="var(--ink)" stroke-width="1.5"
              stroke-linecap="round" />

        <Show when={props.sweep !== undefined && props.sweep !== null}>
          <line class="sweep" x1={x(props.sweep * SPAN)} x2={x(props.sweep * SPAN)}
                y1={top()} y2={bot()} stroke="var(--ink)" opacity="0.45" />
        </Show>

        <Show when={overruns()}>
          <line x1={w() - INSET} x2={w() - INSET} y1={top()} y2={bot()}
                stroke="var(--amber)" stroke-width="2" />
        </Show>

        {/* <Index>, keyed by position: the four handles are the same four
          * elements for the window's life, so one keeps focus while the keys
          * move it -- a <For> over fresh objects rebuilt them on every value. */}
        <Index each={handles()}>{(h) => (
          <g class="handle" onPointerDown={(e) => grab(h(), e, hostEl)}
             onDblClick={(e) => reset(h(), e)}
             tabindex="0" role="slider" aria-label={h().name}
             aria-valuemin="0" aria-valuemax="100"
             aria-valuenow={Math.round((props.value?.(h().xIdx) ?? 0) * 100)}
             aria-valuetext={valueText(h())}
             onKeyDown={(e) => onKey(h(), e)}>
            {/* A generous invisible target over a small visible dot: a handle
              * you cannot grab reads as a handle that does not work. */}
            <circle cx={h().px} cy={h().py} r={HANDLE_R * 3} fill="transparent" />
            <circle cx={h().px} cy={h().py} r={HANDLE_R}
                    fill="var(--ink)" stroke="var(--bg-000)" stroke-width="1.5" />
          </g>
        )}</Index>

        <Axis w={w()} y={h() - INSET - AXIS_H} spanMs={props.spanMs ?? 0}
              markMs={props.markMs ?? 0} />
      </Well>
    </div>
  );
}

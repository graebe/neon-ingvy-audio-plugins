/*
 * A knob that can be dragged finely.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * "Knobs and sliders: drag vertically, SHIFT-DRAG FOR FINE, double-click to
 * reset, click the readout to type" is the design system's own interaction
 * note, and all four are here because three of them are the ones people reach
 * for when a value will not land where they want it.
 */
import { createSignal, createEffect } from 'solid-js';
import { setParam, beginGesture, endGesture } from './iplug.js';

/* Pixels of vertical travel for the whole range. Shift divides into it, so
 * fine drag is a fifth of a percent per pixel rather than a whole one. */
const TRAVEL = 200;
const FINE = 5;

export default function Knob(props) {
  let el;
  const [dragging, setDragging] = createSignal(false);
  const [editing, setEditing] = createSignal(false);

  const norm = () => props.value ?? 0;

  const onPointerDown = (e) => {
    if (e.detail === 2) {                 /* double-click resets */
      beginGesture(props.idx);
      setParam(props.idx, props.default ?? 0);
      endGesture(props.idx);
      return;
    }
    /*
     * THE SENSITIVITY IS CHOSEN AT PRESS AND NOT RE-READ. The delta is
     * measured from the press position, so changing the divisor halfway
     * rescales everything since the press and the value jumps. Shift is
     * therefore held BEFORE the press, which is what "shift-drag" means.
     */
    const divisor = e.shiftKey ? TRAVEL * FINE : TRAVEL;
    const startY = e.clientY;
    const startV = norm();
    setDragging(true);
    beginGesture(props.idx);
    el.setPointerCapture(e.pointerId);

    const move = (ev) => {
      const v = Math.min(1, Math.max(0, startV + (startY - ev.clientY) / divisor));
      setParam(props.idx, v);
    };
    const up = (ev) => {
      el.releasePointerCapture(ev.pointerId);
      el.removeEventListener('pointermove', move);
      el.removeEventListener('pointerup', up);
      endGesture(props.idx);
      setDragging(false);
    };
    el.addEventListener('pointermove', move);
    el.addEventListener('pointerup', up);
  };

  /* The arc: 270 degrees with the gap at the bottom, which is where a knob
   * that has been turned all the way down should visibly stop. */
  const START = 135, SWEEP = 270;
  const R = 19, C = 24;
  const arc = (from, to) => {
    const a0 = (START + SWEEP * from) * Math.PI / 180;
    const a1 = (START + SWEEP * to) * Math.PI / 180;
    const x0 = C + R * Math.cos(a0), y0 = C + R * Math.sin(a0);
    const x1 = C + R * Math.cos(a1), y1 = C + R * Math.sin(a1);
    const large = SWEEP * (to - from) > 180 ? 1 : 0;
    return `M ${x0} ${y0} A ${R} ${R} 0 ${large} 1 ${x1} ${y1}`;
  };

  const commit = (text) => {
    setEditing(false);
    props.onText?.(text);
  };

  return (
    <div class="knob-card">
      <div class="knob-label">{props.label}</div>
      <svg
        ref={el}
        class="knob"
        classList={{ dragging: dragging() }}
        width="48" height="48" viewBox="0 0 48 48"
        onPointerDown={onPointerDown}
      >
        {/* the rail: the unlit part of the arc */}
        <path d={arc(0, 1)} fill="none" stroke="var(--line-200)" stroke-width="2" />
        {/* the signal: only ever as far as the value goes */}
        {norm() > 0.001 && (
          <path d={arc(0, norm())} fill="none" stroke="var(--uv-deep)" stroke-width="2" />
        )}
        <circle cx="24" cy="24" r="14" fill="var(--bg-200)" stroke="var(--line-100)" />
        {/* the pointer, which is what says WHERE it is at a glance */}
        <line
          x1="24" y1="24"
          x2={24 + 12 * Math.cos((START + SWEEP * norm()) * Math.PI / 180)}
          y2={24 + 12 * Math.sin((START + SWEEP * norm()) * Math.PI / 180)}
          stroke="var(--uv)" stroke-width="2"
        />
      </svg>
      {editing() ? (
        <input
          class="knob-readout editing"
          autofocus
          value={props.display ?? ''}
          onBlur={(e) => commit(e.currentTarget.value)}
          onKeyDown={(e) => {
            if (e.key === 'Enter') commit(e.currentTarget.value);
            if (e.key === 'Escape') setEditing(false);
          }}
        />
      ) : (
        /* CLICK THE READOUT TO TYPE. The plugin formats the text and parses
         * it back, so the UI never has to know a parameter's unit. */
        <div class="knob-readout" onClick={() => setEditing(true)}>
          {props.display ?? ''}
        </div>
      )}
    </div>
  );
}

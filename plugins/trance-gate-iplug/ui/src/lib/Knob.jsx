/*
 * A knob that can be dragged finely, and its readout.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * Ported from UvLookAndFeel::drawRotarySlider and UvReadout::paint.
 *
 * From the Knob card, a 48px box: disc r 16, rail r 20, pointer r 6 -> 14,
 * 2px stroke. Expressed as fractions of 48 because knob-lg is the same
 * drawing at 64. The rail runs 270 degrees with the gap at the bottom.
 */
import { createSignal } from 'solid-js';
import { setParam, beginGesture, endGesture, sendMessage, MSG } from './iplug.js';
import { startDrag } from './drag.js';

const BOX = 48;
const START = 135, SWEEP = 270;          /* degrees, gap at the bottom */
const TRAVEL = 200, FINE = 5;            /* px for the whole range; shift divides */

export default function Knob(props) {
  let el;
  const [editing, setEditing] = createSignal(false);
  const norm = () => Math.min(1, Math.max(0, props.value ?? 0));

  /* THE PLUGIN PARSES IT, not the UI. "40 ms" needs the unit, the range and
   * the width the stage is measured against, and the UI holds none of them --
   * it would have to guess, and a guess here silently moves the patch. */
  const commitText = (text) => sendMessage(MSG.setText, `${props.idx}:${text}`);

  const onPointerDown = (e) => {
    if (e.detail === 2) {                /* double-click resets */
      beginGesture(props.idx); setParam(props.idx, props.default ?? 0); endGesture(props.idx);
      return;
    }
    /* THE SENSITIVITY IS CHOSEN AT PRESS AND NOT RE-READ: the delta is
     * measured from the press position, so changing the divisor halfway
     * rescales everything since the press and the value jumps. */
    const divisor = e.shiftKey ? TRAVEL * FINE : TRAVEL;
    const startY = e.clientY, startV = norm();
    beginGesture(props.idx);
    /* Tracked on the window, so the value keeps following the pointer once it
     * leaves the knob -- which is most of a real drag. */
    startDrag(
      (ev) => setParam(props.idx,
                       Math.min(1, Math.max(0, startV + (startY - ev.clientY) / divisor))),
      () => endGesture(props.idx));
  };

  const pt = (r, deg) => {
    const a = deg * Math.PI / 180;
    return [24 + r * Math.cos(a), 24 + r * Math.sin(a)];
  };
  /* BUTT CAPS, not round: a rounded cap on a 2px stroke reads as a value
   * slightly past where it is -- which on a Length knob is a different number
   * of steps. */
  const arc = (from, to) => {
    const r = BOX * (20 / 48);
    const [x0, y0] = pt(r, START + SWEEP * from);
    const [x1, y1] = pt(r, START + SWEEP * to);
    return `M ${x0} ${y0} A ${r} ${r} 0 ${SWEEP * (to - from) > 180 ? 1 : 0} 1 ${x1} ${y1}`;
  };
  const ptr = () => {
    const [x0, y0] = pt(BOX * (6 / 48), START + SWEEP * norm());
    const [x1, y1] = pt(BOX * (14 / 48), START + SWEEP * norm());
    return `M ${x0} ${y0} L ${x1} ${y1}`;
  };

  /*
   * NUMBER IN ink, UNIT IN ink-muted -- split at the LAST space.
   *
   * "40.0 ms" and "90 %" split cleanly; "1/16" and "1.00" have no space and
   * are all number, which is what the system wants for a unitless value.
   * Splitting at the last space rather than the first keeps "1 / 16 T" whole
   * on the number side rather than calling "16 T" a unit.
   */
  const split = () => {
    const t = props.display ?? '';
    const cut = t.lastIndexOf(' ');
    return cut > 0 ? [t.slice(0, cut), t.slice(cut + 1)] : [t, ''];
  };

  return (
    <div class="knob-card">
      <div class="knob-label t-label">{props.label}</div>
      <svg ref={el} class="knob" width={BOX} height={BOX} viewBox={`0 0 ${BOX} ${BOX}`}
           onPointerDown={onPointerDown}>
        {/* the well */}
        <circle cx="24" cy="24" r={BOX * (16 / 48)} fill="var(--bg-200)" />
        {/* the rail */}
        <path d={arc(0, 1)} fill="none" stroke="var(--line-200)" stroke-width="2" stroke-linecap="butt" />
        {/*
          * THE ARC'S DROP SHADOW, WHICH IS WHERE ITS COLOUR COMES FROM. The
          * value arc is near-white; drawn alone it is a white line on a dark
          * disc and the knob has no cast at all. uv-deep underneath it, wider
          * and soft, is what makes it read as violet.
          */}
        {norm() > 0.0001 && (
          <g style={{ filter: 'drop-shadow(0 0 3px rgba(162, 89, 255, 0.75))' }}>
            <path d={arc(0, norm())} fill="none" stroke="var(--uv)" stroke-width="2" stroke-linecap="butt" />
          </g>
        )}
        {/* the pointer, inside the disc */}
        <path d={ptr()} stroke="var(--uv)" stroke-width="2" fill="none" stroke-linecap="butt" />
      </svg>

      {editing() ? (
        <input
          class="readout editing t-value" autofocus
          value={props.display ?? ''}
          onBlur={(e) => { setEditing(false); commitText(e.currentTarget.value); }}
          onKeyDown={(e) => {
            if (e.key === 'Enter') { setEditing(false); commitText(e.currentTarget.value); }
            if (e.key === 'Escape') setEditing(false);
          }}
        />
      ) : (
        /* DOUBLE-click to type, as the card says. */
        <div class="readout t-value" onDblClick={() => setEditing(true)}>
          <span class="num">{split()[0]}</span>
          {split()[1] && <span class="unit">{split()[1]}</span>}
        </div>
      )}
    </div>
  );
}

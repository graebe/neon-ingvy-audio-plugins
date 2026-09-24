/*
 * The pads. Copyright (c) 2026 Torben Gräber. MIT.
 * Ported from StepGridView::paint / mouseDown / mouseDrag.
 *
 * 16 columns of 40px steps with 8px between them, so the grid is 760 wide and
 * GROWS ROWS rather than the window -- a 128-step pattern wraps instead of
 * shrinking each pad to a sliver.
 */
import { For } from 'solid-js';
import { sendMessage, MSG } from './iplug.js';
import { startDrag } from './drag.js';

const COLS = 16, STEP = 40, GAP = 8;

export default function StepGrid(props) {
  const n = () => Math.max(1, props.length ?? 16);
  const rows = () => Math.max(1, Math.ceil(n() / COLS));

  const setStep = (i, mode) => sendMessage(MSG.setStep, `${i}:${mode}`);
  const setDepth = (i, a) =>
    sendMessage(MSG.setDepth, `${i}:${Math.max(0, Math.min(1, a)).toFixed(4)}`);

  const onDown = (i, e) => {
    e.preventDefault();
    const on = !!props.steps?.[i], tie = !!props.ties?.[i];
    /* A CLICK ACTIVATES FULLY, wherever in the pad it lands: setting the
     * amount from the pointer's y on mousedown brought a step on at 10% and
     * looked like the pad had half-failed. Shift cycles into Tie. */
    if (e.shiftKey) setStep(i, on && !tie ? 2 : 1);
    else setStep(i, on ? 0 : 1);

    /*
     * ACTIVATING A DEAD PAD GIVES IT ITS FULL AMOUNT. The amount is
     * independent of the on/off mask, so a pad dragged down to 20% came back
     * at 20% every time it was switched on again -- which reads as the click
     * having half-worked. OFF -> ON only: On<->Tie changes what a live step
     * does, and switching off must not discard an amount set on purpose.
     */
    if (!on) setDepth(i, 1);

    const box = e.currentTarget.getBoundingClientRect();
    let moved = false;
    /* On the window, so the amount keeps following the pointer once it
     * leaves the 40px pad -- which a vertical drag does almost at once. */
    startDrag((ev) => {
      /* A CLICK IS NOT A DRAG, however much the hand shakes. */
      if (!moved) {
        if (Math.abs(ev.clientY - (box.top + box.height / 2)) < 4) return;
        moved = true;
      }
      const amt = 1 - (ev.clientY - box.top) / box.height;
      /* ZERO MEANS OFF: dragging a pad to the floor deactivates it rather
       * than leaving a step that is on and silent. */
      if (amt <= 0.02) setStep(i, 0);
      else { if (!props.steps?.[i]) setStep(i, 1); setDepth(i, amt); }
    });
  };

  const cls = (i) => {
    const on = !!props.steps?.[i], tie = !!props.ties?.[i];
    const at = props.moving && props.playhead === i;
    return {
      pad: true, on: on && !tie, tie, at,
      /* Every fourth step carries a rail-coloured border, so BARS READ
       * WITHOUT NUMBERS -- which is why there are no numbers. */
      bar: !on && !tie && i % 4 === 0,
      cursor: props.cursor === i,
    };
  };

  return (
    <div class="grid" style={{ width: `${COLS * STEP + (COLS - 1) * GAP}px` }}>
      <For each={Array.from({ length: rows() }, (_, r) => r)}>{(r) => (
        <div class="grid-row">
          <For each={Array.from({ length: Math.min(COLS, n() - r * COLS) },
                                (_, k) => r * COLS + k)}>{(i) => (
            <div classList={cls(i)} onPointerDown={(e) => onDown(i, e)}>
              {/* THE AMOUNT IS THE LIT HEIGHT, FROM THE BOTTOM -- a lit height
                * reading as a level is how every step sequencer works, and it
                * is what the hardware does. */}
              {props.steps?.[i] && !props.ties?.[i] && (
                <div class="pad-lit" style={{
                  height: `${Math.max(5, 100 * (props.depths?.[i] ?? 1))}%`
                }}>
                  {/* THE PLAYHEAD ON A PAD THAT IS ALREADY LIT: darken the lit
                    * part. onUv is the token for "what goes on top of a uv
                    * fill"; at a quarter alpha it reads as a shadow crossing
                    * the row. NOT uv-deep, which is never a fill. */}
                  {props.moving && props.playhead === i && <div class="pad-dip" />}
                </div>
              )}
              {/* A tie is a hollow uv outline with a bar across it: the step
                * holds through, so it is not a fill. */}
              {props.ties?.[i] && <div class="pad-tiebar" />}
            </div>
          )}</For>
        </div>
      )}</For>
    </div>
  );
}

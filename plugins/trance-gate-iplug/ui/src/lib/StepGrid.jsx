/*
 * The pads. Copyright (c) 2026 Torben Gräber. MIT.
 *
 * Off / On / Tie is three-state rather than two, because a tie is not a
 * separate property of a step -- it is the third thing a step can be. Shift
 * cycles into Tie; a plain click toggles Off and On.
 */
import { For, createSignal } from 'solid-js';
import { sendMessage, MSG } from './iplug.js';

/* GROWS ROWS, NOT THE WINDOW. A 128-step pattern wraps instead of shrinking
 * each pad to a sliver. */
const PER_ROW = 16;

export default function StepGrid(props) {
  const [drag, setDrag] = createSignal(null);
  const n = () => Math.max(1, props.length ?? 16);
  const rows = () => Math.ceil(n() / PER_ROW);

  const setStep = (i, mode) => sendMessage(MSG.setStep, `${i}:${mode}`);
  const setDepth = (i, amt) =>
    sendMessage(MSG.setDepth, `${i}:${Math.max(0, Math.min(1, amt)).toFixed(4)}`);

  const onDown = (i, e) => {
    e.preventDefault();
    const on = props.steps?.[i];
    const tie = props.ties?.[i];
    /*
     * A CLICK ACTIVATES FULLY, wherever in the pad it lands. Setting the
     * amount from the pointer's y on mousedown brought a step on at 10% when
     * clicked near the bottom, which looked like the pad had half-failed.
     */
    if (e.shiftKey) setStep(i, on && !tie ? 2 : on ? 1 : 1);
    else setStep(i, on ? 0 : 1);
    setDrag({ i, el: e.currentTarget, moved: false });
  };

  const onMove = (e) => {
    const d = drag();
    if (!d) return;
    /* A CLICK IS NOT A DRAG, however much the hand shakes: a few pixels of
     * jitter after a press must not become an amount edit. */
    const b = d.el.getBoundingClientRect();
    if (!d.moved) {
      if (Math.abs(e.clientY - (b.top + b.height / 2)) < 4) return;
      setDrag({ ...d, moved: true });
    }
    const amt = 1 - (e.clientY - b.top) / b.height;
    /* ZERO MEANS OFF. Dragging a pad all the way down deactivates it rather
     * than leaving a step that is on and silent. */
    if (amt <= 0.02) setStep(d.i, 0);
    else { if (!props.steps?.[d.i]) setStep(d.i, 1); setDepth(d.i, amt); }
  };

  const onUp = () => setDrag(null);

  return (
    <div class="grid" onPointerMove={onMove} onPointerUp={onUp} onPointerLeave={onUp}>
      <For each={Array.from({ length: rows() }, (_, r) => r)}>{(r) => (
        <div class="grid-row">
          <For each={Array.from(
            { length: Math.min(PER_ROW, n() - r * PER_ROW) },
            (_, k) => r * PER_ROW + k)}>{(i) => (
            <div
              class="pad"
              classList={{
                on: !!props.steps?.[i],
                tie: !!props.ties?.[i],
                head: props.moving && props.playhead === i,
                cursor: props.cursor === i,
              }}
              onPointerDown={(e) => onDown(i, e)}
              title={`step ${i + 1}`}
            >
              {/* The depth is drawn as a fill from the bottom: the pad is lit
                * from below, so how far up it goes IS the amount. */}
              <div class="pad-fill" style={{
                height: `${100 * (props.steps?.[i] ? (props.depths?.[i] ?? 1) : 0)}%`
              }} />
              <span class="pad-n">{i + 1}</span>
            </div>
          )}</For>
        </div>
      )}</For>
    </div>
  );
}

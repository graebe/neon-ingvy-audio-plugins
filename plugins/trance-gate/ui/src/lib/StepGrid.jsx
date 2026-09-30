/*
 * The pads. Copyright (c) 2026 Torben Gräber. MIT.
 * Ported from StepGridView::paint / mouseDown / mouseDrag.
 *
 * 16 columns of 40px steps with 8px between them, so the grid is 760 wide and
 * GROWS ROWS rather than the window -- a 128-step pattern wraps instead of
 * shrinking each pad to a sliver.
 */
import { For, Show, createSignal } from 'solid-js';
/* The gesture itself lives in steps.js, shared with the ring -- those rules
 * are each a fix for something that read as the click half-failing, and a
 * second copy of them would have drifted. */
import { padGesture, pressStep, orderPress, setStep, setDepth, MODE } from './steps.js';
import { EditField, padKey } from '@ultraviolet/ui';

const COLS = 16, STEP = 40, GAP = 8;

export default function StepGrid(props) {
  const n = () => Math.max(1, props.length ?? 16);
  const rows = () => Math.max(1, Math.ceil(n() / COLS));

  const onDown = (i, e) => { setFocus(i); padGesture(i, e, props); };

  /*
   * THE KEYBOARD. One pad is in the tab order (the last one touched); the
   * arrows move between pads, Space or Enter does what a click does (shift: a
   * tie), and Alt with Up/Down sets the amount -- what a vertical drag does.
   */
  let gridEl;
  const [focus, setFocus] = createSignal(0);
  const onKey = (i, e) => {
    const k = padKey(e, i, n(), COLS);
    if (!k) return;
    e.preventDefault();
    if (k.move !== undefined) {
      setFocus(k.move);
      gridEl?.querySelector(`[data-step="${k.move}"]`)?.focus();
    } else if (k.toggle) {
      if (!orderPress(i, props)) pressStep(i, props, k.tie);
    } else if (k.depth !== undefined) {
      const amt = Math.min(1, Math.max(0, (props.depths?.[i] ?? 1) + k.depth));
      if (amt <= 0.02) setStep(i, MODE.off);
      else { if (!props.steps?.[i]) setStep(i, MODE.on); setDepth(i, amt); }
    }
  };
  const describe = (i) => {
    const state = props.ties?.[i] ? 'tie' : props.steps?.[i] ? 'on' : 'off';
    const amt = props.steps?.[i] ? `, ${Math.round((props.depths?.[i] ?? 1) * 100)} %` : '';
    return `Step ${i + 1}, ${state}${amt}`;
  };

  /*
   * THE BORDER IS WHAT YOU DREW. THE FILL IS WHAT YOU HEAR.
   *
   * One rule covers every state the fade can put a step in, including the two
   * the direction added. `w` is the engine's level factor, so `w == 0` is a gap
   * whatever the mask says:
   *
   *   drawn on,  arrived        uv border + fill      the ordinary lit pad
   *   drawn on,  not arrived    uv border, no fill    .pending  (Fade In)
   *   drawn off, still sounding no border, a fill     .filled   (Fade Out)
   *   drawn off, arrived        no border, no fill    a gap
   *
   * So a hole the fade has not removed yet is never mistaken for a step you
   * drew, and .pending and .filled are each other's mirror.
   */
  const w = (i) => props.weights?.[i] ?? 1;
  const drawn = (i) => !!props.steps?.[i];
  const sounds = (i) => w(i) > 0;
  const pending = (i) => drawn(i) && !sounds(i);
  const filled = (i) => !drawn(i) && sounds(i);

  const cls = (i) => {
    const on = !!props.steps?.[i], tie = !!props.ties?.[i];
    const at = props.moving && props.playhead === i;
    return {
      pad: true, on: on && !tie && !pending(i), tie: tie && !pending(i), at,
      filled: filled(i),
      /* Every fourth step carries a rail-coloured border, so BARS READ
       * WITHOUT NUMBERS -- which is why there are no step numbers. The number
       * below is not one; see .pad-order in app.css. */
      bar: !on && !tie && i % 4 === 0,
      cursor: props.cursor === i,
      pending: pending(i),
      /* In ORDER mode, a step whose place has not been named yet. */
      unnamed: !!props.orderMode && on && !props.named?.[i],
    };
  };

  /*
   * THE NUMBER IS DRAWN ONLY WHEN IT MEANS SOMETHING: while the order is being
   * sequenced, or while the fade is part way in and the numbers are the reason
   * the pads look as they do. At rest the grid is exactly what it was.
   */
  const showN = () => !!props.orderMode || !!props.fading;
  const num = (i) => props.orders?.[i] ?? 0;
  /* Only the kind the fade is introducing has a number worth reading: under
   * Fade Out that is the holes. */
  const numbered = (i) => showN() && (props.orderTakes?.(i) ?? drawn(i)) && num(i) > 0;

  /*
   * TYPING A RANK. The number is the control -- click it and it becomes a field.
   *
   * It is editable whenever it is DRAWN, so re-numbering needs no mode of its
   * own: ORDER mode is for laying a sequence down by tapping, this is for
   * changing one of them. The engine SWAPS on collision, so the step that held
   * the number you typed takes the one this step had, and nothing between them
   * moves.
   */
  const [editing, setEditing] = createSignal(-1);
  const commit = (i, text) => {
    const n = parseInt(text, 10);
    if (Number.isFinite(n) && n >= 1) props.onOrder?.(i, n);
  };

  return (
    <div class="grid" ref={gridEl} role="grid" aria-label="Steps"
         style={{ width: `${COLS * STEP + (COLS - 1) * GAP}px` }}>
      <For each={Array.from({ length: rows() }, (_, r) => r)}>{(r) => (
        <div class="grid-row" role="row">
          <For each={Array.from({ length: Math.min(COLS, n() - r * COLS) },
                                (_, k) => r * COLS + k)}>{(i) => (
            <div classList={cls(i)} role="gridcell" data-step={i}
                 tabindex={focus() === i ? 0 : -1} aria-label={describe(i)}
                 aria-selected={!!props.steps?.[i]}
                 onPointerDown={(e) => onDown(i, e)} onKeyDown={(e) => onKey(i, e)}>
              {/* THE AMOUNT IS THE LIT HEIGHT, FROM THE BOTTOM -- a lit height
                * reading as a level is how every step sequencer works, and it
                * is what the hardware does. */}
              {/* Show, not `cond && <el/>`: a bare && returned from a <For>
                * callback is evaluated once, against the empty initial array --
                * see the note in Ring.jsx, which lost the whole ring to it. In a
                * CHILDREN position either works, and this one is in one, but the
                * two forms sitting side by side in one file is how the other
                * kind gets written next. */}
              <Show when={numbered(i)}>
                <Show when={editing() === i}
                      fallback={
                        /* stopPropagation, or padGesture measures the drag
                         * against this 9px box instead of the pad. */
                        <span class="pad-order t-hint"
                              title="Click to type a new arrival number"
                              onPointerDown={(e) => {
                                e.stopPropagation();
                                e.preventDefault();
                                setEditing(i);
                              }}>{num(i)}</span>
                      }>
                  {/* Enter or a click away commits; Escape abandons. */}
                  <EditField class="pad-order-edit t-hint" value={String(num(i))}
                             inputmode="numeric" ariaLabel={`Arrival of step ${i + 1}`}
                             onCommit={(t) => commit(i, t)} onClose={() => setEditing(-1)} />
                </Show>
              </Show>
              {/* SOUNDS, not "is drawn on": a hole Fade Out has not removed
                * yet is lit too. That is the whole of the .filled state. */}
              {sounds(i) && !props.ties?.[i] && (
                <div class="pad-lit" style={{
                  /* THE FADE SCALES THE LIT HEIGHT, because the engine scales
                   * the step's level by exactly this number -- so a step part
                   * way in draws part way up, which is what soft mode sounds
                   * like. */
                  height: `${Math.max(5, 100 * (drawn(i) ? (props.depths?.[i] ?? 1) : 1) * w(i))}%`
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
              {props.ties?.[i] && !pending(i) && <div class="pad-tiebar" />}
            </div>
          )}</For>
        </div>
      )}</For>
    </div>
  );
}

/*
 * The pads. Copyright (c) 2026 Torben Gräber. MIT.
 * Ported from StepGridView::paint / mouseDown / mouseDrag.
 *
 * 16 columns of 40px steps with 8px between them, so the grid is 760 wide and
 * GROWS ROWS rather than the window -- a 128-step pattern wraps instead of
 * shrinking each pad to a sliver.
 */
import { For, Show } from 'solid-js';
/* The gesture itself lives in steps.js, shared with the ring -- those rules
 * are each a fix for something that read as the click half-failing, and a
 * second copy of them would have drifted. */
import { padGesture } from './steps.js';

const COLS = 16, STEP = 40, GAP = 8;

export default function StepGrid(props) {
  const n = () => Math.max(1, props.length ?? 16);
  const rows = () => Math.max(1, Math.ceil(n() / COLS));

  const onDown = (i, e) => padGesture(i, e, props);

  /* The fade's weight for a step, 1 when there is no fade running. A weight of
   * 0 is a step that is IN the pattern and not yet sounding -- which the grid
   * draws as a lit border with no fill, a third state it never had to show. */
  const w = (i) => props.weights?.[i] ?? 1;
  const pending = (i) => !!props.steps?.[i] && w(i) <= 0;

  const cls = (i) => {
    const on = !!props.steps?.[i], tie = !!props.ties?.[i];
    const at = props.moving && props.playhead === i;
    return {
      pad: true, on: on && !tie && !pending(i), tie: tie && !pending(i), at,
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
              {/* Show, not `cond && <el/>`: a bare && returned from a <For>
                * callback is evaluated once, against the empty initial array --
                * see the note in Ring.jsx, which lost the whole ring to it. In a
                * CHILDREN position either works, and this one is in one, but the
                * two forms sitting side by side in one file is how the other
                * kind gets written next. */}
              <Show when={showN() && num(i) > 0}>
                <span class="pad-order t-hint">{num(i)}</span>
              </Show>
              {props.steps?.[i] && !props.ties?.[i] && !pending(i) && (
                <div class="pad-lit" style={{
                  /* THE FADE SCALES THE LIT HEIGHT, because the engine scales
                   * the step's level by exactly this number -- so a step part
                   * way in draws part way up, which is what soft mode sounds
                   * like. */
                  height: `${Math.max(5, 100 * (props.depths?.[i] ?? 1) * w(i))}%`
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

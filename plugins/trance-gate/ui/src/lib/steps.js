/*
 * Editing a step — the one implementation of it.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * This was inline in StepGrid.jsx, which was fine while the pads were the only
 * thing you could click. The ring edits the same sixteen steps, and a second
 * copy of the rules below would have drifted from the first inside a week:
 * they are not obvious, and every one of them is a fix for something that read
 * as the click having half-failed.
 *
 * The rules, and what each is for:
 *
 *   - A CLICK ACTIVATES FULLY, wherever in the pad it lands. Setting the
 *     amount from the pointer's y on press brought a step on at 10% and looked
 *     like the pad had half-worked.
 *   - SHIFT CYCLES INTO TIE, because a tie is not a property of a step, it is
 *     the third thing a step can be: Off, On, Tie.
 *   - ACTIVATING A DEAD STEP RESTORES ITS FULL AMOUNT. The amount is
 *     independent of the on/off mask, so a step dragged down to 20% came back
 *     at 20% every time it was switched on again. OFF -> ON only: On <-> Tie
 *     changes what a live step does and must not discard an amount set on
 *     purpose.
 *   - A CLICK IS NOT A DRAG, however much the hand shakes: 4px of dead zone
 *     before a press starts setting the amount.
 *   - ZERO MEANS OFF. Dragging to the floor deactivates rather than leaving a
 *     step that is on and silent.
 */
import { sendMessage } from '@ultraviolet/ui';
import { MSG } from './msg.js';
import { startDrag, createCoalescer } from '@ultraviolet/ui';

export const MODE = { off: 0, on: 1, tie: 2 };

/** Off / On / Tie for one step. The cursor moves there first; see OnMessage. */
export const setStep = (i, mode) => sendMessage(MSG.setStep, `${i}:${mode}`);

/** A step's amount, 0..1. */
export const setDepth = (i, amount) =>
  sendMessage(MSG.setDepth, `${i}:${Math.max(0, Math.min(1, amount)).toFixed(4)}`);

/*
 * A DRAG'S AMOUNTS, AT MOST ONCE A FRAME. pointermove fires far faster than
 * anything can be drawn or heard, and each setDepth is a message across the
 * bridge and an edit for the audio thread.
 */
const depths = createCoalescer((i, amount) => setDepth(i, amount));

/**
 * A step's place in the fade's ARRIVAL ORDER, 1..N.
 *
 * The engine normalises the whole order after this, so a rank out of range
 * clamps and the result is still a permutation -- the UI never has to compute
 * what the other steps become.
 */
export const setOrder = (i, rank) =>
  sendMessage(MSG.setOrder, `${i}:${Math.max(1, Math.round(rank))}`);

/** Reroll the current slot. No payload: the engine walks its own generator, so
 *  two presses differ. */
export const randomize = () => sendMessage(MSG.randomize);

/**
 * The press half of the gesture: cycle the step under the pointer.
 *
 * `model` is `{ steps, ties }` — read live rather than captured, so a gesture
 * that crosses several steps sees each one as it actually is.
 *
 * Returns the mode it set, which is what a paint drag needs in order to apply
 * the same mode to everything it crosses rather than toggling each in turn.
 */
export function pressStep(i, model, shiftKey) {
  const on = !!model.steps?.[i], tie = !!model.ties?.[i];
  const mode = shiftKey ? (on && !tie ? MODE.tie : MODE.on)
                        : (on ? MODE.off : MODE.on);
  setStep(i, mode);
  if (!on && mode !== MODE.off) setDepth(i, 1);
  return mode;
}

/**
 * ORDER MODE: a click names the next arrival rather than editing the step.
 *
 * Tapping the pads in the order you want them to arrive is the whole gesture,
 * and it is a MODE rather than a modifier because both modifiers a pad has are
 * taken -- plain click cycles the step and shift makes a tie -- and because a
 * sequence is something you type out over several clicks, which a mode says and
 * a chord does not.
 *
 * `next` is how many have been named so far, so the first click is rank 1. A
 * step that is OFF is switched on first and then named: you can lay down an
 * arrival order and a pattern in one pass.
 *
 * Returns true when it handled the press, so the caller falls through to the
 * ordinary gesture when the mode is off.
 */
export function orderPress(i, model) {
  if (!model.orderMode) return false;
  /*
   * ONLY THE ARRIVING KIND CAN BE SEQUENCED. Under Fade Out you are putting the
   * HOLES in order, and a step you drew on has no place in that sequence -- so a
   * click on one is ignored rather than silently switching it off to make it
   * one. Without the callback (the ring, before it knew about directions) the
   * old rule stands and an off step is switched on to join the order.
   */
  const takes = model.orderTakes;
  if (takes) {
    if (!takes(i)) return true;
  } else if (!model.steps?.[i]) {
    setStep(i, MODE.on);
    setDepth(i, 1);
  }
  setOrder(i, (model.orderNext ?? 0) + 1);
  model.onOrdered?.(i);
  return true;
}

/**
 * The pads' whole gesture: press to cycle, then drag vertically for the
 * amount, measured against the pad's own box.
 */
export function padGesture(i, e, model) {
  e.preventDefault();
  if (orderPress(i, model)) return;
  pressStep(i, model, e.shiftKey);

  const box = e.currentTarget.getBoundingClientRect();
  let moved = false;
  /* On the window, so the amount keeps following the pointer once it leaves
   * the 40px pad — which a vertical drag does almost at once. */
  startDrag((ev) => {
    if (!moved) {
      if (Math.abs(ev.clientY - (box.top + box.height / 2)) < 4) return;
      moved = true;
    }
    const amt = 1 - (ev.clientY - box.top) / box.height;
    if (amt <= 0.02) { depths.flush(); setStep(i, MODE.off); }
    else { if (!model.steps?.[i]) setStep(i, MODE.on); depths.push(i, amt); }
  }, () => depths.flush());
}

/**
 * The ring's gesture: press to cycle, then PAINT the same mode across every
 * wedge the pointer crosses.
 *
 * THIS IS THE ONE PLACE THE RING DIFFERS FROM THE PADS, and it is geometry
 * rather than preference. A pad has 40px of height to read an amount off; a
 * wedge has an angle and no vertical extent, so there is nothing for a
 * vertical drag to mean. Painting is what the gesture affords instead, and it
 * is the one a hardware ring has: hold and sweep to fill a run of steps.
 *
 * `stepAt(ev)` maps a pointer event to a step index, or -1. Supplied by the
 * caller because only the ring knows its own centre and radii.
 */
export function ringGesture(i, e, model, stepAt) {
  e.preventDefault();
  /* The ring sequences too -- this file is the one implementation of what a
   * click does to a step, and a second copy of the rule would have drifted. */
  if (orderPress(i, model)) return;
  const mode = pressStep(i, model, e.shiftKey);

  /* Painted already, so a sweep back and forth does not flicker them. */
  const done = new Set([i]);
  startDrag((ev) => {
    const j = stepAt(ev);
    if (j < 0 || done.has(j)) return;
    done.add(j);
    setStep(j, mode);
    /* Same reasoning as a press: a step being switched on gets its full
     * amount back rather than whatever it was left at. */
    if (mode !== MODE.off && !model.steps?.[j]) setDepth(j, 1);
  });
}

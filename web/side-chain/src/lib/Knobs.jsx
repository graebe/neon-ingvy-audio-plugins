// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * This window's two knobs that are not the kit's ParamKnob as it stands -- the
 * native editor's StageKnob behaviour and its ParamChoiceKnob -- each the kit's
 * Knob bound to a params store exactly as ParamKnob binds it (a drag is a
 * gesture, a reset is the module's default), with one difference apiece.
 *
 * They live here, not in the kit, by the kit's own rule: a component with one
 * caller has no API yet, only a shape.
 */
import { Knob } from '@ultraviolet/ui';
import { stepFrom } from './status.js';

/*
 * A STAGE'S KNOB READS IN THE UNIT TIME ASKS FOR, and takes either. `display`
 * is the readout the window computes (status.js stageText) and `onText` reads
 * what is typed (stageEdit), so a stage says and takes one thing whether it
 * is reached here or on its handle.
 */
export function StageKnob(props) {
  const p = () => props.params;
  return (
    <Knob label={props.label} info={props.info} readoutInfo={props.readoutInfo}
          value={p().value(props.idx)} display={props.display} bipolar={props.bipolar}
          onBegin={() => p().begin(props.idx)}
          onInput={(v) => p().input(props.idx, v)}
          onEnd={() => p().end(props.idx)}
          onReset={() => p().reset(props.idx)}
          onText={(text) => props.onText?.(text)} />
  );
}

/*
 * A KNOB OVER A CHOICE TOO LONG FOR A SELECT: a MIDI note of 128, a channel of
 * 17. Every value it writes is a whole option, at least one away from where
 * the gesture began (status.js stepFrom) -- so an arrow on Channel is the next
 * channel rather than a sixth of one that rounds back. The readout is the
 * parameter's own text ("C1", "Omni"), and typing is the module's to read.
 */
export function ChoiceKnob(props) {
  const p = () => props.params;
  let from = 0;
  return (
    <Knob label={props.label} info={props.info} readoutInfo={props.readoutInfo}
          value={p().value(props.idx)} display={p().text(props.idx)}
          onBegin={() => { from = p().value(props.idx); p().begin(props.idx); }}
          onInput={(v) => p().input(props.idx, stepFrom(from, v, props.options))}
          onEnd={() => p().end(props.idx)}
          onReset={() => p().reset(props.idx)}
          onText={(text) => p().sendText(props.idx, text)} />
  );
}

/*
 * Binding the kit's controls to host parameters.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * @ultraviolet/ui's controls know nothing about iPlug2: a Knob takes a
 * normalised number and reports one back, a Select takes an index. That is
 * what lets the same Select serve this editor, where every control is a host
 * parameter, and the Spectrogram's, which has none.
 *
 * This is the other half -- the four lines per control that turn "the user
 * moved it" into a parameter write with a gesture around it. It lives here
 * because WHICH parameter and WHAT it means are this plugin's business.
 */
import { Knob, Select, Toggle } from '@ultraviolet/ui';
import { setParam, beginGesture, endGesture, sendMessage } from '@ultraviolet/ui';
import { MSG } from './msg.js';

/*
 * A DRAG IS ONE GESTURE, NOT A HUNDRED EDITS. Begin/end bracket it so the host
 * writes a single undo step and an automation lane records a touch rather than
 * a hundred separate writes.
 */
const commit = (idx, v) => { beginGesture(idx); setParam(idx, v); endGesture(idx); };

export function ParamKnob(props) {
  return (
    <Knob
      label={props.label}
      value={props.value}
      display={props.display}
      default={props.default}
      onBegin={() => beginGesture(props.idx)}
      onInput={(v) => setParam(props.idx, v)}
      onEnd={() => endGesture(props.idx)}
      /* THE PLUGIN PARSES TYPED TEXT, not the UI. "40 ms" needs the unit, the
       * range and the width the stage is measured against, and this side holds
       * none of them -- it would have to guess, and a guess here silently moves
       * the patch. */
      onText={(text) => sendMessage(MSG.setText, `${props.idx}:${text}`)}
    />
  );
}

/*
 * An enum parameter as a Select. The wire is normalised 0..1 in both
 * directions and the option index is what a Select speaks, so the conversion
 * lives here -- in ONE place, rather than in every caller.
 */
export function ParamSelect(props) {
  const n = () => props.options.length;
  const idx = () => Math.round((props.value ?? 0) * (n() - 1));
  return (
    <Select
      options={props.options}
      value={idx()}
      label={props.label}
      labelWidth={props.labelWidth}
      width={props.width}
      onChange={(i) => commit(props.idx, n() > 1 ? i / (n() - 1) : 0)}
    />
  );
}

/** A bool parameter as a Toggle. Normalised 0 or 1, as everything else is. */
export function ParamToggle(props) {
  return (
    <Toggle
      label={props.label}
      value={(props.value ?? 0) > 0.5}
      onChange={(on) => commit(props.idx, on ? 1 : 0)}
    />
  );
}

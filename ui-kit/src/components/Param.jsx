/*
 * The kit's controls, bound to host parameters through a params store.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * The controls themselves know nothing about parameters -- a Knob turns a
 * normalised number, a Select chooses an index. This is the one binding every
 * editor used to write for itself (twice as params.jsx, once inline): a drag
 * is a gesture, a click is one committed write, a reset is the plugin's
 * default, and typed text goes to the plugin to be parsed.
 */
import { Knob } from './Knob.jsx';
import { Select } from './Select.jsx';
import { Toggle } from './Toggle.jsx';

export function ParamKnob(props) {
  const p = () => props.params;
  return (
    <Knob
      label={props.label}
      size={props.size}
      value={p().value(props.idx)}
      display={props.display ?? p().text(props.idx)}
      onBegin={() => p().begin(props.idx)}
      onInput={(v) => p().input(props.idx, v)}
      onEnd={() => p().end(props.idx)}
      onReset={() => p().reset(props.idx)}
      onText={(text) => p().sendText(props.idx, text)}
    />
  );
}

/*
 * An enum parameter as a Select. `(n - 1)` is the divisor, not `n`: the last
 * option is at 1.0, and getting that wrong leaves it unreachable by automation
 * while every other one works.
 */
export function ParamSelect(props) {
  const n = () => props.options.length;
  return (
    <Select
      options={props.options}
      value={Math.round(props.params.value(props.idx) * Math.max(0, n() - 1))}
      label={props.label}
      ariaLabel={props.ariaLabel}
      labelWidth={props.labelWidth}
      width={props.width}
      onChange={(i) => props.params.commit(props.idx, n() > 1 ? i / (n() - 1) : 0)}
    />
  );
}

/** A bool parameter as a Toggle: normalised 0 or 1, like everything else. */
export function ParamToggle(props) {
  return (
    <Toggle
      label={props.label}
      value={props.params.value(props.idx) > 0.5}
      onChange={(on) => props.params.commit(props.idx, on ? 1 : 0)}
    />
  );
}

/*
 * The binding layer: kit components on one side, host parameters on the other.
 *
 * The kit deliberately knows nothing about parameters -- a Knob takes a
 * normalised number, a Select takes an index -- so every editor needs this seam,
 * and this is Pump's.
 *
 * A GESTURE IS begin / set / end, ALWAYS. Not politeness: a host uses the
 * begin/end pair to decide what one undo step is and when an automation lane is
 * being written. A bare `setParam` during a drag records a hundred separate
 * edits, and undo then walks back through the drag one pixel at a time.
 */
import { Knob, Select, Toggle, setParam, beginGesture, endGesture, sendMessage }
  from '@ultraviolet/ui';
import { MSG, COUNTS } from './msg.js';

/** One value, one undo step. For a click, a keystroke or a handle's snap. */
export function commit(idx, v) {
  beginGesture(idx);
  setParam(idx, v);
  endGesture(idx);
}

export function ParamKnob(props) {
  return (
    <Knob label={props.label}
          value={props.value}
          display={props.display}
          default={props.default}
          onBegin={() => beginGesture(props.idx)}
          onInput={(v) => setParam(props.idx, v)}
          onEnd={() => endGesture(props.idx)}
          /* The editor cannot parse "-18 dB": it holds normalised numbers and no
           * units. The plugin owns the format in both directions. */
          onText={(text) => sendMessage(MSG.setText, `${props.idx}:${text}`)} />
  );
}

/*
 * THE INDEX <-> NORMALISED CONVERSION LIVES HERE AND NOWHERE ELSE.
 *
 * A host parameter is a 0..1 number even when it is an enum, and `(n - 1)` is
 * the divisor rather than `n` -- the last option is at 1.0, not at (n-1)/n.
 * Getting that wrong makes the final option unreachable by automation while
 * every other one works, which is the kind of bug that survives a demo.
 */
export function ParamSelect(props) {
  const n = () => COUNTS[props.idx] ?? 2;
  return (
    <Select label={props.label}
            options={props.options}
            width={props.width}
            labelWidth={props.labelWidth}
            value={Math.round((props.value ?? 0) * (n() - 1))}
            onChange={(i) => commit(props.idx, i / (n() - 1))} />
  );
}

export function ParamToggle(props) {
  return (
    <Toggle label={props.label}
            value={(props.value ?? 0) > 0.5}
            onChange={(on) => commit(props.idx, on ? 1 : 0)} />
  );
}

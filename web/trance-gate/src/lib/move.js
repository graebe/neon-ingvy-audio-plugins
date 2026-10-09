// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The editor on a Move: what its parameters, messages and readouts are in the
 * module's own terms. Pure, so node tests every line of it.
 *
 * The module declares itself in engines/trance-gate/crates/tg-move/src/
 * params.rs (CHAIN_PARAMS); the kit's transport (@ultraviolet/ui/schwung)
 * carries values both ways; this file is the table between the two.
 *
 *   KEYS      editor parameter index -> chain_params key
 *   FEEDS     the `web_ui` declaration's extra_keys -> the editor's tags
 *   MESSAGES  the editor's own messages -> the writes that do the same
 *   expandParams(bridge)
 *             the bridge, with the `params` feed also delivered as the values
 *             it carries -- see below for why
 */
import { MSG, P, NUM_PARAMS } from './msg.js';

/* Width is `hold` on the Move: the engine's own name for it. */
export const KEYS = (() => {
  const keys = new Array(NUM_PARAMS).fill(null);
  keys[P.slot] = 'slot';
  keys[P.length] = 'length';
  keys[P.rate] = 'rate';
  keys[P.legato] = 'legato';
  keys[P.timeMode] = 'time_mode';
  keys[P.curve] = 'curve';
  keys[P.amount] = 'amount';
  keys[P.width] = 'hold';
  keys[P.attack] = 'attack';
  keys[P.decay] = 'decay';
  keys[P.sustain] = 'sustain';
  keys[P.release] = 'release';
  keys[P.fade] = 'fade';
  keys[P.fadeSoft] = 'fade_soft';
  keys[P.fadeDir] = 'fade_dir';
  return keys;
})();

export const FEEDS = { ui: MSG.uiState, params: MSG.params, state: MSG.state };

/* A step's three states, as the `step` enum names them. */
export const STEP_MODES = ['Off', 'On', 'Tie'];

/* "<index>:<rest>" -> [index, rest], or null for a payload that is not one. */
function indexed(text) {
  const at = String(text).indexOf(':');
  if (at <= 0) return null;
  const i = Number(text.slice(0, at));
  if (!Number.isInteger(i) || i < 0) return null;
  return [i, text.slice(at + 1)];
}

/*
 * PER-STEP STATE IS EDITED AT THE CURSOR. The module has one Step, one Step
 * Amount and (undeclared) one step_order, each meaning "the step the cursor is
 * on" -- which is how the Move's own encoders edit a pattern -- so every step
 * edit is the cursor first and then the value. Writes reach the module in
 * order and none is dropped, so the pair cannot be split by another write of
 * this page. The cursor is written every time rather than remembered: the
 * Move's jog moves it too, and a page that assumed it had not moved would
 * edit the wrong step.
 *
 * `step_order` IS NOT IN chain_params, and does not have to be: the manager
 * forwards any key to the chain, and the chain any key to the module
 * (schwung's chain_host.c). Declared, it would be a knob on the Move's own
 * pages, which is not where an arrival order is set.
 */
export const MESSAGES = {
  [MSG.setStep]: (text, write) => {
    const m = indexed(text);
    const mode = m && STEP_MODES[Number(m[1])];
    if (!mode) return;
    write('cursor', m[0]);
    write('step', mode);
  },
  [MSG.setDepth]: (text, write) => {
    const m = indexed(text);
    const amount = m && Number(m[1]);
    if (!m || !Number.isFinite(amount)) return;
    write('cursor', m[0]);
    write('step_amount', Math.min(1, Math.max(0, amount)).toFixed(4));
  },
  [MSG.setOrder]: (text, write) => {
    const m = indexed(text);
    const rank = m && Math.round(Number(m[1]));
    if (!m || !(rank >= 1)) return;
    write('cursor', m[0]);
    write('step_order', rank);
  },
  /*
   * A ROLL, AND THEN THE KNOB BACK TO REST. `randomize` is an enum on the Move
   * because Schwung has no write-only parameter, and a knob writes whatever it
   * is turned to -- so the label "Roll" fires (a label and not an index, which
   * would seed every roll alike) and "Hold" does nothing. The button is an
   * action, so it leaves the Move's own Random knob where an action leaves a
   * control: at rest, where turning it to Roll rolls.
   */
  [MSG.randomize]: (_text, write) => {
    write('randomize', 'Roll');
    write('randomize', 'Hold');
  },
};

/*
 * THE `params` READOUT, IN THE ORDER tg-core WRITES IT (get_param "params"):
 *
 *   slot:legato:time_mode:curve:rate:length:amount:hold:attack:decay:sustain:
 *   release:width_ms:fade:fade_soft:fade_dir:detents
 *
 * Each field is on its key's own wire -- the indices and the rate's label as
 * the single-key getters answer them, the floats at %.9g -- so a field IS the
 * value its key would echo. width_ms and the detents are no parameter.
 */
export const PARAMS_FIELDS = ['slot', 'legato', 'time_mode', 'curve', 'rate', 'length',
                              'amount', 'hold', 'attack', 'decay', 'sustain', 'release',
                              null, 'fade', 'fade_soft', 'fade_dir'];

/** The `params` readout -> { key: wire } for every field that is a parameter;
 *  null for a readout too short to be one. */
export function paramsWires(text) {
  const f = String(text).split(':');
  if (f.length < PARAMS_FIELDS.length) return null;
  const out = {};
  PARAMS_FIELDS.forEach((key, i) => { if (key && f[i] !== '') out[key] = f[i]; });
  return out;
}

/*
 * THE SOUND COMES FROM THE READOUT, AND ONLY FROM IT.
 *
 * The manager pushes a declared parameter when somebody WRITES it. A slot
 * recall writes one -- `slot` -- and changes all fourteen others with nobody
 * writing them, so a page that listened to the echoes would show the new
 * slot's number over the old slot's knobs until each was touched. And an echo
 * can be stale without anyone noticing: a write to the Rate of slot 4 just
 * before a switch back to slot 1 echoes slot 4's Rate, and if the switch
 * leaves the engine as it was at the last readout, no readout follows to say
 * otherwise. The engine's `params` readout is re-read after every write and
 * twice a second, and it is what the engine holds, for the slot it is on.
 *
 * So for the fifteen values the readout carries, the echoes are dropped and
 * each field of the readout is delivered as its key's value when it says
 * something new: a field that moved since the page was last given it, or any
 * field of a key this page has written since. The second is what puts a
 * control where the engine put it -- an enum knob nudged between two options
 * snaps back to the one the module took, and a write the Move overtook (a
 * Rate set just before a slot switch) is corrected although the readout did
 * not move. A heartbeat that repeats the readout whole says nothing new, and
 * is not delivered again.
 *
 * A decoration of the bridge rather than of the transport, so the kit's
 * transport stays the same for every editor: it sees the fields as the
 * declared values they are.
 */
const FIELD_KEYS = new Set(PARAMS_FIELDS.filter(Boolean));

export function expandParams(bridge) {
  const prefix = `${bridge.component || 'synth'}:`;
  const keyOf = (full) => (String(full).startsWith(prefix) ? String(full).slice(prefix.length) : null);
  /* key -> the wire value the page was last given for it. */
  const told = new Map();
  /* The keys this page has written since the last readout. */
  const written = new Set();
  return {
    ...bridge,
    setParam(full, value) {
      const key = keyOf(full);
      if (FIELD_KEYS.has(key)) written.add(key);
      bridge.setParam(full, value);
    },
    onParamChange(fn) {
      bridge.onParamChange((changes) => {
        const out = {};
        for (const [full, value] of Object.entries(changes ?? {})) {
          if (!FIELD_KEYS.has(keyOf(full))) out[full] = value;
        }
        const text = changes?.[`${prefix}params`];
        const wires = text === undefined ? null : paramsWires(text);
        for (const [key, wire] of Object.entries(wires ?? {})) {
          if (told.get(key) === wire && !written.has(key)) continue;
          told.set(key, wire);
          written.delete(key);
          out[prefix + key] = wire;
        }
        if (Object.keys(out).length) fn(out);
      });
    },
  };
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * This editor's vocabulary on the Move: its parameters, the chain key each one
 * is, and the feeds it is drawn from.
 *
 * Plain JavaScript with no imports but the kit's shell tags, so `node --test`
 * holds this table to the module's own declaration (test/bindings.test.mjs).
 */
import { SHELL_MSG } from '@ultraviolet/ui/shell';

/*
 * THE PARAMETER INDICES ARE THE ENGINE'S, in sc-core's `Param` order -- so the
 * `params` feed, which lists the engine's values in that order, is read by the
 * same index the controls use, and no second table stands between them.
 *
 * `Param` is append-only and the two the Move does not have, Threshold and
 * Lockout, are its last two (13 and 14). So the Move's parameters are a PREFIX
 * of the engine's, and this table simply stops before them: there is no index
 * for a control the device cannot have, rather than an index bound to nothing.
 */
export const P = {
  source: 0,
  rate: 1,
  timeMode: 2,
  delay: 3,
  attack: 4,
  hold: 5,
  release: 6,
  depth: 7,
  curve: 8,
  channel: 9,
  note: 10,
  midiMode: 11,
  velSens: 12,
};
export const NUM_PARAMS = 13;

/*
 * THE BINDING TABLE: editor index -> the module's chain_params key, which is
 * also sc-core's `Param::key()`. The transport writes and listens by these.
 */
export const KEYS = [
  'source', 'rate', 'time_mode', 'delay', 'attack', 'hold', 'release',
  'depth', 'curve', 'channel', 'trigger_note', 'midi_mode', 'vel_sens',
];

/*
 * The editor's own message tags. The shell's are the kit's; these three are
 * what the plugin's editor listened on, kept so the editor reads the same
 * readouts by the same tags whichever host is on the other end.
 */
export const MSG = {
  ...SHELL_MSG,
  uiState: 64,
  params: 65,
  stageMs: 67,
};

/*
 * THE FEEDS: the `extra_keys` of the module's trailing `web_ui` declaration
 * (sc-move/src/params.rs), each to the tag the editor listens on. The manager
 * re-reads them after any write and on its heartbeat, and sends them when
 * they moved -- the only way a value the engine changes by itself, such as
 * the playhead, reaches the page.
 */
export const FEEDS = {
  ui: MSG.uiState,
  params: MSG.params,
  stage_ms: MSG.stageMs,
};

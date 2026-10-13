// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * This editor's message tags and parameter indices.
 *
 * The shell's tags are the kit's (@ultraviolet/ui/shell), the same in every
 * editor; the rest are this editor's own vocabulary. They were the plugin's
 * EMsgTags when the editor lived in iPlug2's WebView. On the Move nothing on
 * the other end knows a tag: the transport (lib/move.js) turns each into the
 * module's own writes, and each readout the module publishes into the tag the
 * editor already listened on. The numbers are kept so the editor's code did
 * not have to change around them.
 */
import { SHELL_MSG } from '@ultraviolet/ui/shell';

export const MSG = {
  ...SHELL_MSG,
  /* <- the module's `ui` readout: the pattern and the playhead. */
  uiState: 64,
  /* <- the `params` readout: the sound at full precision, and the Length
   * detents. */
  params: 65,
  /* <- the `state` readout: the whole patch, every slot, as JSON. The page
   * renders the plots' curves from it with the engine itself. */
  state: 67,
  /* -> "<index>:<0|1|2>": a step Off, On or Tie. */
  setStep: 96,
  /* -> "<index>:<0..1>": a step's amount. */
  setDepth: 97,
  /* -> "<index>:<rank>": a step's place in the fade's arrival order. */
  setOrder: 103,
  /* -> reroll the current slot. An ACTION: no payload, so the engine walks its
   * own generator and two presses differ. */
  randomize: 104,
};

/* The editor's parameters by index -- the engine's own Param order (tg-core's
 * params.rs), which lib/move.js maps onto the module's chain_params keys. */
export const P = { slot: 0, length: 1, rate: 2, legato: 3, timeMode: 4, curve: 5,
                   amount: 6, width: 7, attack: 8, decay: 9, sustain: 10, release: 11,
                   fade: 12, fadeSoft: 13, fadeDir: 14 };
export const NUM_PARAMS = 15;

/* Length is 1..MAX_LENGTH steps, normalised over its 127 intervals -- the
 * Move's 128-option enum, whose option i is a length of i + 1. */
export const MAX_LENGTH = 128;
export const lengthNorm = (steps) => (steps - 1) / (MAX_LENGTH - 1);

/*
 * This editor's message tags. Copyright (c) 2026 Torben Gräber. MIT.
 *
 * The shell's tags are the kit's (@ultraviolet/ui/shell), the same in every
 * plugin; the rest are this plugin's own vocabulary. tests/editor_tags.test.mjs
 * holds both to the C++.
 */
import { SHELL_MSG } from '@ultraviolet/ui/shell';

/* EMsgTags in TranceGate.h, after the shell's. Tags 0..NUM_PARAMS-1 are a
 * parameter's display string, tagged with its index. */
export const MSG = {
  ...SHELL_MSG,
  uiState: 64, params: 65, scope: 66, patch: 67,
  setStep: 96, setDepth: 97, setCursor: 98, requestPatch: 99,
  /* "<index>:<rank>" -- a step's place in the fade's arrival order. Per-step
   * state, so a message and not a parameter, exactly like setDepth. */
  setOrder: 103,
  /* Reroll the current slot. An ACTION: no payload from here, so the engine
   * walks its own generator and two presses differ. */
  randomize: 104,
  /*
   * "<length>:<perStep>:" + a raw byte a sample -- the gate across one cycle,
   * as the ENGINE applies it: the samples themselves, rendered by a scratch
   * engine with a DC input, not a description drawn from. Binary, like the
   * scope (66): both arrive through onBytes.
   */
  gate: 105,
};

/* EParams in Params.h, which is the engine's own Param order -- so the host
 * index IS the engine index. Fade and its two switches are APPENDED. */
export const P = { slot: 0, length: 1, rate: 2, legato: 3, timeMode: 4, curve: 5,
                   amount: 6, width: 7, attack: 8, decay: 9, sustain: 10, release: 11,
                   fade: 12, fadeSoft: 13, fadeDir: 14 };
export const NUM_PARAMS = 15;

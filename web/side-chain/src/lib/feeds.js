// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The module's three feeds, read. Each is a string sc-core's get_param writes
 * (engines/side-chain/crates/sc-core/src/params.rs), and each is POSITIONAL --
 * a contract, not a debug dump -- so a short one is dropped whole rather than
 * read with its tail missing: half a state drawn anyway reads as a real one.
 *
 * Plain JavaScript, so `node --test` can hold these to the engine's own
 * output (test/feeds.test.mjs runs the real engine, as wasm).
 */
import { NUM_PARAMS } from './msg.js';

const numbers = (text) => String(text ?? '').split(':').map(Number);

/**
 * `ui`: source:rate:ms_cycle:sweep:advancing:fires:duck:key:connected:stage:phase
 * :devms -- eleven fields and the engine's clock after them -- or null. `key`
 * and `connected` are the sidechain detector's and mean nothing on the Move,
 * which has no key input; they are read only because the format is
 * positional. `devms` is the time the engine had processed when the readout
 * was true (null from an engine older than it), which the playhead is carried
 * from.
 */
export function parseUi(text) {
  const f = numbers(text);
  if (f.length < 11 || f.some((v) => !Number.isFinite(v))) return null;
  return {
    source: f[0], rate: f[1], msCycle: f[2], sweep: f[3], advancing: f[4],
    fires: f[5], duck: f[6], key: f[7], connected: f[8], stage: f[9], phase: f[10],
    devms: f.length > 11 ? f[11] : null,
  };
}

/**
 * `params`: every value the engine has, in its own units and `Param` order --
 * which is this editor's index order (msg.js) -- or null. The engine lists
 * fifteen; the Move's thirteen are the first of them, and the rest are left
 * unread.
 */
export function parseParams(text) {
  const f = numbers(text);
  if (f.length < NUM_PARAMS || f.some((v) => !Number.isFinite(v))) return null;
  return f;
}

/** `stage_ms`: delay:attack:hold:release in ms, or null. */
export function parseStageMs(text) {
  const f = numbers(text);
  if (f.length < 4 || f.slice(0, 4).some((v) => !Number.isFinite(v))) return null;
  return f.slice(0, 4);
}

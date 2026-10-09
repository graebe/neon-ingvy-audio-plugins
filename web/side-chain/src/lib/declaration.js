// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What the module DECLARES about this editor's parameters: each one's range,
 * and each choice's options -- read from its chain_params, never restated.
 *
 * WHY THE EDITOR NEEDS RANGES AT ALL. The drawing reads the engine's own
 * values (the `params` feed), so no range stands between the picture and the
 * DSP. But a drag goes the other way: a pointer position becomes a
 * percentage of the cycle, and a write is normalised 0..1, so `toNorm` needs
 * the range. In the plugin era that range was a copy of Params.cpp held to it
 * by a test; on the Move the page is handed the declaration itself, so there
 * is nothing to copy and nothing to drift.
 *
 * WHY THE OPTIONS COME FROM HERE. The transport normalises an enum as
 * index / (options - 1) over the MOVE's options (web/kit/src/lib/chain.js),
 * and a Select turns a normalised value back into an index over the options
 * it is given. If those two lists differ -- the plugin's three sources against
 * the Move's two -- every choice lands one place off. One list, the
 * declaration's, serves both.
 *
 * The page is built with the declaration it ships beside (the build reads
 * sc-move's params.rs), and the transport reads the device's copy of the same
 * string at runtime; the two are one artefact.
 *
 * Plain JavaScript, so `node --test` can reach it.
 */
import { KEYS, P } from './msg.js';

const clamp01 = (n) => Math.min(1, Math.max(0, n));

/**
 * The declaration as this editor reads it:
 *
 *   entry(idx)        the chain_params entry for an editor index, or undefined
 *   options(idx)      a choice's option labels, [] for anything else
 *   range(idx)        [min, max] in the parameter's own unit, or null
 *   toNorm(idx, v)    a value in that unit -> 0..1, clamped
 *   fromNorm(idx, n)  and back, clamped
 */
export function declarationOf(chainParams) {
  const byKey = new Map((chainParams ?? []).filter((e) => e && e.key).map((e) => [e.key, e]));
  const entry = (idx) => byKey.get(KEYS[idx]);
  const isNumber = (e) => e && (e.type === 'float' || e.type === 'int');

  const range = (idx) => {
    const e = entry(idx);
    return isNumber(e) ? [Number(e.min), Number(e.max)] : null;
  };
  const options = (idx) => (entry(idx)?.options ?? []).map(String);

  /* A parameter with no range has nothing to convert: 0, rather than a value
   * invented for it. */
  const toNorm = (idx, v) => {
    const r = range(idx);
    if (!r || r[1] === r[0]) return 0;
    return clamp01((v - r[0]) / (r[1] - r[0]));
  };
  const fromNorm = (idx, n) => {
    const r = range(idx);
    if (!r) return 0;
    return r[0] + (r[1] - r[0]) * clamp01(n);
  };

  return { entry, options, range, toNorm, fromNorm };
}

/**
 * THE SHAPE AS THE PARAMETERS HOLD IT, for the duration of a drag.
 *
 * The drawing normally follows the engine's `params` feed, which on the Move
 * arrives up to 150 ms after a write -- so a handle being dragged would trail
 * the pointer. The normalised values are written locally the moment the
 * pointer moves; converted back through the declared ranges, they put the
 * handle under it. `base` supplies what the handles do not move (curve, cycle).
 *
 * Every length is in its declared unit, which is the engine's: percent of the
 * cycle for the stages, 0..1 for Depth.
 */
export function shapeFromNorm(value, base, decl) {
  return {
    ...base,
    delay: decl.fromNorm(P.delay, value(P.delay)),
    attack: decl.fromNorm(P.attack, value(P.attack)),
    hold: decl.fromNorm(P.hold, value(P.hold)),
    release: decl.fromNorm(P.release, value(P.release)),
    depth: decl.fromNorm(P.depth, value(P.depth)),
  };
}

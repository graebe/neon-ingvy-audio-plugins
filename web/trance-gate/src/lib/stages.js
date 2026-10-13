// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Attack, Decay and Release as text: in ms or in % of the gate, as Time asks.
 *
 * The engine holds a stage as a PERCENTAGE OF THE GATE'S OPEN TIME, and that
 * is the module's wire (0..200 %). Time in % is only a reading: in ms the same
 * number is `pct / 100 * width_ms`, where width_ms is the engine's, from the
 * `params` readout. In Live the plugin formats and parses this
 * (plugins/trance-gate/Params.cpp, formatStage and parseStage); on the Move
 * nothing does, so this is that pair, line for line, and stages.test.mjs
 * holds it to the plugin's own cases.
 */

/* The engine's ceiling for a stage (tg-core STAGE_MAX_PCT), which is also the
 * declared range's top; move.test.mjs checks the declaration says so. */
export const STAGE_MAX_PCT = 200;

/** A stage's display text: "12.5 ms", or "6.25 %" in % mode or before the
 *  gate's width is known. */
export function formatStage(pct, ms, widthMs) {
  if (ms && widthMs > 0) return `${(pct / 100 * widthMs).toFixed(1)} ms`;
  return `${pct.toFixed(2)} %`;
}

/*
 * Typed text -> a percentage, or null for text that does not start with a
 * number. THE UNIT TYPED WINS OVER THE MODE: "40 ms" means milliseconds even
 * while the readout shows percent, and "25 %" the other way round.
 */
export function parseStage(text, ms, widthMs) {
  const t = String(text).trim();
  if (!/^[-+]?(\d|\.\d)/.test(t)) return null;
  const v = Number.parseFloat(t);
  if (!Number.isFinite(v)) return null;
  const saysMs = t.includes('ms');
  const saysPct = t.includes('%');
  const asMs = saysMs || (ms && !saysPct);
  const p = asMs ? (widthMs > 0 ? v / widthMs * 100 : 0) : v;
  return Math.min(STAGE_MAX_PCT, Math.max(0, p));
}

/*
 * The params store with the three stages' text and typing done here. The
 * module declares a stage as "0..200 %" and the transport shows it so; the
 * window shows it the way Live does. `timeMs()` says whether Time reads in ms,
 * and `widthMs()` is the engine's gate width in ms (0 until it is known).
 */
export function withStages(host, { stages, timeMs, widthMs }) {
  const isStage = (i) => stages.includes(i);
  const pctOf = (i) => host.value(i) * STAGE_MAX_PCT;
  return {
    ...host,
    text: (i) => (isStage(i) ? formatStage(pctOf(i), timeMs(), widthMs()) : host.text(i)),
    sendText: (i, text) => {
      if (!isStage(i)) return host.sendText(i, text);
      const pct = parseStage(text, timeMs(), widthMs());
      if (pct !== null) host.commit(i, pct / STAGE_MAX_PCT);
      return undefined;
    },
  };
}

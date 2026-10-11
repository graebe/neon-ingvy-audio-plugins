// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The engine's readouts, decoded.
 *
 * Plain JavaScript so node can test it: these are the parsers the editor used
 * to carry inline in App.jsx.
 */

/** Hex digits, least significant bit first from the END of the string. */
export function hexToBits(hex, n) {
  const bits = new Array(n).fill(false);
  if (!hex) return bits;
  let bit = 0;
  for (let i = hex.length - 1; i >= 0 && bit < n; i--) {
    const v = parseInt(hex[i], 16);
    if (Number.isNaN(v)) continue;
    for (let k = 0; k < 4 && bit < n; k++, bit++) bits[bit] = ((v >> k) & 1) === 1;
  }
  return bits;
}

/*
 * The `ui` readout's fields, in the engine's order -- tg-core's UI_FIELDS,
 * which `get_param("ui_fields")` answers and test/fields.test.mjs holds this
 * to. Read by position, as every reader does; the engine only ever appends.
 */
export const UI_FIELDS = ['steps', 'ties', 'length', 'phase', 'ms_step', 'moving', 'cursor', 'depths', 'orders', 'slot', 'devms'];

/**
 * The `ui` readout:
 * "steps:ties:length:phase:msStep:moving:cursor:depths:orders:slot"
 * -> an object, or null when it has too few fields.
 */
export function decodeUi(text) {
  const f = String(text).split(':');
  if (f.length < 9) return null;
  const length = Math.max(1, parseInt(f[2], 10) || 16);
  const depths = [];
  const orders = [];
  for (let i = 0; i < length; i++) {
    depths.push((parseInt((f[7] || '').substr(i * 2, 2), 16) || 0) / 255);
    /* The arrival rank, 1..N, and 0 for a step that is off. */
    orders.push(parseInt((f[8] || '').substr(i * 2, 2), 16) || 0);
  }
  return {
    steps: hexToBits(f[0], length),
    ties: hexToBits(f[1], length),
    length,
    phase: parseFloat(f[3]) || 0,
    msStep: parseFloat(f[4]) || 0,
    moving: f[5] === '1',
    cursor: parseInt(f[6], 10) || 0,
    depths,
    orders,
    /* Appended by the engine; absent from an older one. */
    slot: f.length > 9 ? parseInt(f[9], 10) || 0 : 0,
    /* The engine's clock when this was true: the playhead is carried from it. */
    devms: f.length > 10 && Number.isFinite(parseFloat(f[10])) ? parseFloat(f[10]) : null,
  };
}

/**
 * A step edit the editor has sent, applied to a decoded `ui` readout ahead of
 * the engine's next one: `{ i, mode }` (0 off, 1 on, 2 tie) or `{ i, amount }`
 * (0..1, held as the engine holds it, a byte). A new object; the readout given
 * is not touched, and an edit past the pattern's end changes nothing.
 */
export function applyEdit(u, edit) {
  const i = edit?.i;
  if (!u || !Number.isInteger(i) || i < 0 || i >= u.length) return u;
  const next = { ...u };
  if (edit.mode !== undefined) {
    next.steps = u.steps.slice();
    next.ties = u.ties.slice();
    next.steps[i] = edit.mode !== 0;
    next.ties[i] = edit.mode === 2;
  } else if (edit.amount !== undefined) {
    next.depths = u.depths.slice();
    next.depths[i] = Math.round(Math.min(1, Math.max(0, edit.amount)) * 255) / 255;
  }
  return next;
}

/**
 * The Length detents, "16,32,64,128" -> [16, 32, 64, 128]: the lengths the
 * ENGINE says are half a bar to four bars at the Rate and the host's meter.
 * Absent (an older plugin) or empty is none.
 */
export function decodeDetents(field) {
  if (!field) return [];
  return String(field).split(',').map((x) => parseInt(x, 10)).filter((n) => n > 0);
}

/**
 * The `params` readout, the engine's own units:
 * slot:legato:time_mode:curve:rate:length:amount:hold:attack:decay:sustain:
 * release:width_ms:fade:fade_soft:fade_dir:detents -> an object, or null.
 */
export function decodeEngineParams(text) {
  const f = String(text).split(':');
  if (f.length < 16) return null;
  return {
    legato: +f[1] >= 0.5, timeMode: +f[2], curve: +f[3], rate: f[4],
    amount: +f[6], width: +f[7],
    attack: +f[8], decay: +f[9], sustain: +f[10],
    release: +f[11], widthMs: +f[12],
    fade: +f[13], fadeSoft: +f[14] >= 0.5, fadeOut: +f[15] >= 0.5,
    detents: decodeDetents(f[16]),
  };
}


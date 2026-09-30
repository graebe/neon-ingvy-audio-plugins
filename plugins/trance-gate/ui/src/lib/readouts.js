/*
 * The engine's readouts, decoded. Copyright (c) 2026 Torben Gräber. MIT.
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

/**
 * The `ui` readout: "steps:ties:length:phase:msStep:moving:cursor:depths:orders"
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
  };
}

/**
 * The `params` readout, the engine's own units:
 * slot:legato:time_mode:curve:rate:length:amount:hold:attack:decay:sustain:
 * release:width_ms:fade:fade_soft:fade_dir -> an object, or null.
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
  };
}

/**
 * The capture: "<cols>:<cycleMs>:<head>:<4 hex bytes a column>", column k at
 * pattern phase k/cols -> { cols: [[dryLo, dryHi, wetLo, wetHi]], windowMs,
 * head } or null.
 */
export function decodeScope(text) {
  const f = String(text).split(':');
  if (f.length < 4) return null;
  const n = Math.max(0, Math.min(1024, parseInt(f[0], 10) || 0));
  const windowMs = parseFloat(f[1]) || 0;
  const head = Math.max(0, Math.min(n - 1, parseInt(f[2], 10) || 0));
  const hex = f[3];
  const cols = new Array(n);
  for (let i = 0; i < n; i++) {
    const o = i * 8;
    const v = (k) => (parseInt(hex.substr(o + k * 2, 2), 16) || 0) / 127.5 - 1;
    cols[i] = [v(0), v(1), v(2), v(3)];
  }
  return { cols, windowMs, head };
}

/**
 * The gate across one cycle as the engine applies it:
 * "<length>:<perStep>:<hex>", a byte per sample -> { length, perStep, values }.
 */
export function decodeGate(text) {
  const f = String(text).split(':');
  if (f.length < 3) return null;
  const length = Math.max(1, parseInt(f[0], 10) || 1);
  const perStep = Math.max(1, parseInt(f[1], 10) || 1);
  const hex = f[2];
  const n = Math.min(length * perStep, hex.length >> 1);
  const values = new Array(n);
  for (let i = 0; i < n; i++) values[i] = (parseInt(hex.substr(i * 2, 2), 16) || 0) / 255;
  return { length, perStep, values };
}

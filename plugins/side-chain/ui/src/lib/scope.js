/*
 * The Side-Chain capture, decoded. Copyright (c) 2026 Torben Gräber. MIT.
 *
 * "<cols>:<seen bits>:<5 hex pairs per column>" -- dry low/high and wet
 * low/high, bipolar, then the gain, UNIPOLAR. Plain JavaScript so node can
 * test it.
 */

/* A nibble table rather than parseInt: this runs over every column every frame. */
const NIB = new Int8Array(128).fill(-1);
for (let i = 0; i < 16; i++) NIB['0123456789ABCDEF'.charCodeAt(i)] = i;

/**
 * -> { cols: [[dryLo, dryHi, wetLo, wetHi, gain], ...], seen: '0101...' } or
 * null. A SHORT PAYLOAD IS DROPPED WHOLE: the transport truncates rather than
 * fails, and half a picture drawn anyway reads as a real transient.
 */
export function decodeScope(text) {
  if (typeof text !== 'string') return null;
  const c1 = text.indexOf(':');
  const c2 = text.indexOf(':', c1 + 1);
  if (c1 < 0 || c2 < 0) return null;
  const n = Math.max(0, Math.min(1024, parseInt(text.slice(0, c1), 10) || 0));
  const seen = text.slice(c1 + 1, c2);
  if (seen.length < n || text.length - (c2 + 1) < n * 10) return null;

  const cols = new Array(n);
  for (let i = 0; i < n; i++) {
    const o = (c2 + 1) + i * 10;
    const byteAt = (k) => {
      const hi = NIB[text.charCodeAt(o + k * 2)];
      const lo = NIB[text.charCodeAt(o + k * 2 + 1)];
      return hi < 0 || lo < 0 ? 128 : (hi << 4) | lo;
    };
    cols[i] = [
      byteAt(0) / 127.5 - 1, byteAt(1) / 127.5 - 1,
      byteAt(2) / 127.5 - 1, byteAt(3) / 127.5 - 1,
      /* Unipolar: read through the bipolar mapping the trace would draw upside
       * down and twice as tall. */
      byteAt(4) / 255,
    ];
  }
  return { cols, seen };
}

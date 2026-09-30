/*
 * The Trance Gate's two binary readouts: the Signal capture and the rendered
 * gate. Copyright (c) 2026 Torben Gräber. MIT.
 *
 * Both arrive as bytes (the bridge's onBytes), an ASCII header and then one
 * byte a value. Plain JavaScript so node can test them.
 */
import { readHeader, intField, bipolar, unipolar, reuse } from '@ultraviolet/ui/capture';

/** Values per capture column: dry low, dry high, wet low, wet high. */
export const SCOPE_STRIDE = 4;

/**
 * "<cols>:<cycleMs>:<head>:" then four bytes a column, column k at pattern
 * phase k/cols -> { data, stride, count, windowMs, head } or null. `data` is
 * `into` when it is big enough -- the capture arrives every frame.
 */
export function decodeScope(bytes, into) {
  const h = readHeader(bytes, 3);
  if (!h) return null;
  const count = intField(h.fields[0]);
  const windowMs = Number(h.fields[1]);
  const head = intField(h.fields[2]);
  if (!(count >= 1 && count <= 1024) || !Number.isFinite(windowMs)) return null;
  const n = count * SCOPE_STRIDE;
  /* Short is dropped whole: half a capture would read as a real signal. */
  if (bytes.length - h.offset < n) return null;
  const data = reuse(into, n);
  for (let i = 0; i < n; i++) data[i] = bipolar(bytes[h.offset + i]);
  return {
    data, stride: SCOPE_STRIDE, count, windowMs,
    head: Number.isInteger(head) ? Math.max(0, Math.min(count - 1, head)) : 0,
  };
}

/**
 * The gate across one cycle as the engine applies it: "<length>:<perStep>:"
 * then a byte of gain per sample -> { length, perStep, values } or null.
 */
export function decodeGate(bytes) {
  const h = readHeader(bytes, 2);
  if (!h) return null;
  const length = intField(h.fields[0]);
  const perStep = intField(h.fields[1]);
  if (!(length >= 1) || !(perStep >= 1)) return null;
  const n = Math.min(length * perStep, bytes.length - h.offset);
  const values = new Float32Array(n);
  for (let i = 0; i < n; i++) values[i] = unipolar(bytes[h.offset + i]);
  return { length, perStep, values };
}

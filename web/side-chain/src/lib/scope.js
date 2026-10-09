// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Side-Chain capture, decoded.
 *
 * "<cols>:" then six raw bytes a column -- seen (0 or 1), dry low/high and wet
 * low/high (bipolar), then the gain (UNIPOLAR) -- delivered as bytes by the
 * bridge. Plain JavaScript so node can test it.
 */
import { readHeader, intField, bipolar, unipolar, reuse } from '@ultraviolet/ui/capture';

/* Where each value sits in a decoded column. */
export const COL = { seen: 0, dryLo: 1, dryHi: 2, wetLo: 3, wetHi: 4, gain: 5 };
export const STRIDE = 6;

/**
 * -> { data, stride, count } or null, `data` reusing `into` when it is big
 * enough. A SHORT PAYLOAD IS DROPPED WHOLE: the transport truncates rather than
 * fails, and half a picture drawn anyway reads as a real transient.
 */
export function decodeScope(bytes, into) {
  const h = readHeader(bytes, 1);
  if (!h) return null;
  const count = intField(h.fields[0]);
  if (!(count >= 1 && count <= 1024)) return null;
  if (bytes.length - h.offset < count * STRIDE) return null;
  const data = reuse(into, count * STRIDE);
  for (let i = 0; i < count; i++) {
    const o = h.offset + i * STRIDE;
    const d = i * STRIDE;
    data[d + COL.seen] = bytes[o] ? 1 : 0;
    data[d + COL.dryLo] = bipolar(bytes[o + 1]);
    data[d + COL.dryHi] = bipolar(bytes[o + 2]);
    data[d + COL.wetLo] = bipolar(bytes[o + 3]);
    data[d + COL.wetHi] = bipolar(bytes[o + 4]);
    /* Unipolar: read through the bipolar mapping the trace would draw upside
     * down and twice as tall. */
    data[d + COL.gain] = unipolar(bytes[o + 5]);
  }
  return { data, stride: STRIDE, count };
}

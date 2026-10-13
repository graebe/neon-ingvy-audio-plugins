// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Reading a binary payload from the plugin: a short ASCII header, then bytes.
 *
 * The plot captures and the spectrogram's columns used to travel as hex inside
 * the base64 the WebView transport adds anyway: 2.7 times the bytes, decoded
 * three times. They are raw bytes now, decoded from base64 once by the bridge
 * (onBytes), and these read them in place. Plain JavaScript for node.
 */

/**
 * The first `n` colon-terminated ASCII fields of `bytes` and where the data
 * starts after them, or null when the header is not there. Nothing past the
 * header is read as text.
 */
export function readHeader(bytes, n, maxLen = 64) {
  if (!(bytes instanceof Uint8Array)) return null;
  const fields = [];
  let start = 0;
  for (let i = 0; i < bytes.length && i < maxLen && fields.length < n; i++) {
    if (bytes[i] !== 58) continue;               /* ':' */
    let s = '';
    for (let k = start; k < i; k++) s += String.fromCharCode(bytes[k]);
    fields.push(s);
    start = i + 1;
  }
  return fields.length === n ? { fields, offset: start } : null;
}

/** A whole decimal field, or NaN. */
export const intField = (s) => (/^-?\d+$/.test(s) ? Number(s) : NaN);

/** A byte as a waveform bound, -1..1 (128 is silence). */
export const bipolar = (b) => b / 127.5 - 1;
/** A byte as a gain, 0..1. */
export const unipolar = (b) => b / 255;

/**
 * A Float32Array of at least `size`, reusing `buf` when it is big enough: a
 * capture arrives every frame, and a new array every frame is garbage every
 * frame.
 */
export const reuse = (buf, size) => (buf && buf.length >= size ? buf : new Float32Array(size));

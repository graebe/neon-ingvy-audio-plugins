/*
 * The wire, decoded: columns, the frequency axis, and where a label goes.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * Plain JavaScript in its own file rather than helpers inside App.jsx, because
 * these three are the only things in the editor that can be WRONG rather than
 * merely ugly -- an off-by-one in the hex decode is a picture shifted by a band,
 * which nobody will spot by eye. node's own test runner can import this; it
 * cannot import JSX.
 */
/*
 * THE HEX DECODE, and it is a nibble table rather than parseInt because this runs
 * on every column of every frame. '0'..'9' are 48..57 and 'A'..'F' are 65..70;
 * the plugin writes upper case only (see kHex in Spectrogram.cpp).
 */
const nibble = (code) => (code <= 57 ? code - 48 : code - 55);

/** "<cols>:<bands>:<hex>" -> { count, bands, data } or null. */
export function decodeColumns(text) {
  if (typeof text !== 'string') return null;
  const a = text.indexOf(':');
  const b = text.indexOf(':', a + 1);
  if (a < 1 || b < a + 2) return null;

  const count = Number.parseInt(text.slice(0, a), 10);
  const bands = Number.parseInt(text.slice(a + 1, b), 10);
  if (!Number.isInteger(count) || !Number.isInteger(bands)) return null;
  if (count < 1 || bands < 1) return null;

  const hex = text.slice(b + 1);
  const bytes = count * bands;
  /*
   * A SHORT PAYLOAD IS DROPPED WHOLE RATHER THAN DRAWN IN PART. The transport
   * truncates rather than fails (see kMaxJSString), and a truncated batch drawn
   * anyway would put a column of garbage in the middle of the picture -- which
   * reads as a real transient and cannot be told from one.
   */
  if (hex.length < bytes * 2) return null;

  const data = new Uint8Array(bytes);
  for (let i = 0; i < bytes; i++) {
    data[i] = (nibble(hex.charCodeAt(i * 2)) << 4) | nibble(hex.charCodeAt(i * 2 + 1));
  }
  return { count, bands, data };
}

/** "20.6,21.4,..." -> Float32Array of band centres, or null. */
export function decodeAxis(text) {
  if (typeof text !== 'string' || text === '') return null;
  const parts = text.split(',');
  const hz = new Float32Array(parts.length);
  for (let i = 0; i < parts.length; i++) {
    const v = Number.parseFloat(parts[i]);
    if (!Number.isFinite(v) || v <= 0) return null;
    hz[i] = v;
  }
  return hz;
}

/*
 * THE MARKS ON THE SCALE: decades and their halves, and only the ones the axis
 * actually covers.
 *
 * Eight over 256 pixels is a mark every 32px or so -- enough to read a height
 * off and few enough that the gutter stays a scale rather than becoming a ruler.
 * The system's restraint applies to instruments too.
 *
 * 10 and 20 arrived with the longer window: before it the axis could not start
 * below 47 Hz, so a 10 Hz mark would have pointed at nothing.
 */
const MARKS = [
  [10, '10'], [20, '20'], [50, '50'], [100, '100'], [500, '500'],
  [1000, '1k'], [5000, '5k'], [10000, '10k'],
];

/*
 * THE ZOOM, as named bands rather than as numbers to type.
 *
 * Overlapping on purpose: a kick's fundamental and its click belong to
 * different views, and each should appear in more than one. They are the
 * ranges a mix is talked about in, which is the point -- a dropdown of
 * "10-100, 100-1k, 1k-10k" would be easier to write and harder to reach for.
 *
 * The zoom spreads the SAME bins over the full height; it does not add any. In
 * Sub that turns ~33 bins crowded into the bottom 40 pixels into ~33 bins at 8
 * pixels each, which is what makes 50 Hz and 60 Hz two visibly different rows.
 * Higher up there are several bins to a band already and the zoom is detail in
 * the ordinary sense.
 */
export const RANGES = [
  { name: 'Full', lo: 10, hi: 20000 },
  { name: 'Sub', lo: 10, hi: 200 },
  { name: 'Bass', lo: 40, hi: 800 },
  { name: 'Mid', lo: 200, hi: 4000 },
  { name: 'High', lo: 2000, hi: 20000 },
];

/**
 * Where a frequency sits in the well, 0 at the top and 1 at the bottom.
 *
 * The axis is geometric, so its ends are all this needs -- and taking them from
 * the analyzer's own centres is what keeps the label beside the band it names.
 */
export function marksFor(hz, height) {
  if (!hz || hz.length < 2) return [];
  let lo = Math.log(hz[0]);
  let hi = Math.log(hz[hz.length - 1]);

  /*
   * THE PICTURE SPANS BAND EDGES; THE AXIS LISTS BAND CENTRES.
   *
   * Half a band separates the two, and that half band is exactly where the end
   * marks live: the lowest band is CENTRED at 10.15 Hz because it runs from 10
   * to 10.3, so a 10 Hz mark tested against the centres falls just outside the
   * axis and is dropped -- and the picture ends up with no mark at either end,
   * which are the two a reader most wants.
   *
   * With three or more centres the per-band ratio is known, so the true extent
   * is recoverable. With two it is not: the "step" would be the entire span, and
   * extending by half of that would put the ends an octave out. Tests hand this
   * two-element axes on purpose.
   */
  if (hz.length >= 3) {
    const half = (hi - lo) / (hz.length - 1) / 2;
    lo -= half;
    hi += half;
  }
  const out = [];
  for (const [f, label] of MARKS) {
    const t = (Math.log(f) - lo) / (hi - lo);
    /*
     * THE TOLERANCE IS NOT SLOPPINESS, IT IS THE END MARKS SURVIVING.
     *
     * The extension above recovers the bottom edge to within a rounding error
     * -- 10.000000035 Hz rather than 10 -- and a hard `t < 0` then drops the
     * 10 Hz mark on the wrong side of nine decimal places. A mark this close to
     * an end IS at that end; one genuinely outside the axis (10 kHz on a picture
     * that stops at 8) is outside by a tenth of the span, not by 1e-9.
     */
    if (t < -1e-6 || t > 1 + 1e-6) continue;
    /* Band 0 is at the BOTTOM, so t runs upwards and y runs down. */
    out.push({ label, y: (1 - Math.min(1, Math.max(0, t))) * height });
  }
  return out;
}

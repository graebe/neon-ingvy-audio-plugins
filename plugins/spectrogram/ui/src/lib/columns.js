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
  [1000, '1k'], [5000, '5k'], [10000, '10k'], [20000, '20k'],
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

/* ------------------------------------------------------------------ level --
 *
 * THE INVERSE OF THE ENGINE'S `amplitude_to_byte`, AND IT HAS TO STAY THAT.
 *
 * bands.rs maps an amplitude to `round(((dB - floor) / (ceil - floor)) * 255)`.
 * Reading a byte back is that arithmetic run the other way -- which is the one
 * legitimate reason for this file to know the floor and the ceiling at all.
 * They are stated once, here, and the hint bar prints THIS constant rather than
 * a second spelling of -96.
 */
export const DB_FLOOR = -96;
export const DB_CEIL = 0;

/**
 * A column byte as dBFS. Byte 0 is not "-96 dB" but "at or below it" -- the
 * engine returns 0 for silence and for anything under the floor alike -- so it
 * comes back as -Infinity and the caller decides how to say so.
 */
export function dbForLevel(byte, floor = DB_FLOOR, ceil = DB_CEIL) {
  if (!Number.isFinite(byte) || byte <= 0) return -Infinity;
  const v = Math.min(255, byte);
  return floor + (v / 255) * (ceil - floor);
}

/* ------------------------------------------------------------------- time --
 *
 * THE TIME AXIS IS DRAWN FROM THE COLUMN RATE, NOT MEASURED.
 *
 * One column is one pixel and the engine holds the column rate constant across
 * sample rates (that is what `pick_hop` is for), so the width IS the span and a
 * tick every second is a fixed number of pixels. Nothing here is a clock: if
 * columns were ever dropped the picture would compress and these ticks would
 * lie -- which is why the engine's ring is sized so that cannot happen quietly.
 *
 * `anchor` keeps the end labels inside the picture: the 0 s label sits at the
 * very right edge and centring it would hang half of it over the frame.
 */
export function timeMarksFor(cols, colsPerSecond, width, stepS = 1) {
  if (!(cols > 0) || !(colsPerSecond > 0) || !(width > 0) || !(stepS > 0)) return [];
  const out = [];
  const span = cols / colsPerSecond;          /* seconds across the picture */
  for (let a = 0; a <= span + 1e-9; a += stepS) {
    const x = width - (a / span) * width;
    if (x < -1e-9) break;
    out.push({
      label: a === 0 ? '0s' : `-${Number.isInteger(a) ? a : a.toFixed(1)}s`,
      x,
      /* 'end' hugs the right edge, 'start' the left, 'mid' is centred. */
      anchor: x > width - 12 ? 'end' : x < 12 ? 'start' : 'mid',
    });
  }
  return out;
}

/**
 * Seconds ago for a column `age` columns left of the newest one, so the
 * crosshair can say WHEN as well as what.
 */
export const secondsAgo = (age, colsPerSecond) =>
  (!(colsPerSecond > 0) || !(age >= 0) ? 0 : age / colsPerSecond);

/* ------------------------------------------------------------------- bars --
 *
 * THE BAR-ALIGNED AXIS. The plugin reports where the host's transport is; every
 * decision about where that puts a column is made here.
 *
 * The whole claim of this view is that a musical position maps to a PIXEL, not
 * to an arrival order -- so the same beat lands on the same column every pass
 * and two takes of one bar stack on each other. Everything below is that one
 * mapping and its inverse, kept as plain arithmetic so node can test it.
 */

/**
 * "<ppq>:<bpm>:<num>:<denom>:<running>:<ppqPerCol>" -> an object, or null.
 *
 * Rejected whole rather than in part: a sync message with one unreadable field
 * would place columns somewhere confidently wrong, and a picture that is
 * confidently wrong about time is worse than one that has not moved yet.
 */
export function decodeSync(text) {
  if (typeof text !== 'string') return null;
  const p = text.split(':');
  if (p.length < 6) return null;

  const ppq = Number.parseFloat(p[0]);
  const bpm = Number.parseFloat(p[1]);
  const num = Number.parseInt(p[2], 10);
  const denom = Number.parseInt(p[3], 10);
  const running = p[4] === '1';
  const ppqPerCol = Number.parseFloat(p[5]);

  if (!Number.isFinite(ppq) || !Number.isFinite(bpm) || bpm <= 0) return null;
  if (!Number.isInteger(num) || num < 1 || !Number.isInteger(denom) || denom < 1) return null;
  if (!Number.isFinite(ppqPerCol) || ppqPerCol <= 0) return null;

  return { ppq, bpm, num, denom, running, ppqPerCol };
}

/**
 * Beats in a bar, where a "beat" is the quarter note PPQ counts in.
 *
 * A host's PPQ is quarter notes, always -- so 6/8 is three quarters to the bar,
 * not six. Reading the numerator alone would draw 6/8 twice as wide as it is.
 */
export const beatsPerBar = (num, denom) =>
  (!(num > 0) || !(denom > 0) ? 4 : (num * 4) / denom);

/**
 * Which pixel column a musical position belongs to, 0 at the left edge.
 *
 * THE MODULO HAS TO SURVIVE A NEGATIVE ppq. Hosts count bars from 1 but PPQ
 * from 0, and a count-in -- or simply dragging the playhead before the start --
 * reports a NEGATIVE position. JavaScript's `%` keeps the sign of its left
 * operand, so `-0.5 % 8` is `-0.5` and the naive version indexes off the front
 * of the buffer. The extra `+ window` folds it back.
 */
export function slotForPpq(ppq, bars, num, denom, cols) {
  if (!Number.isFinite(ppq) || !(bars > 0) || !(cols > 0)) return 0;
  const window = bars * beatsPerBar(num, denom);
  if (!(window > 0)) return 0;
  const phase = (((ppq % window) + window) % window) / window;
  /* Clamped as well as wrapped: `phase` can reach 1 through rounding at the
   * very top of the window, and cols is one past the last pixel. */
  return Math.min(cols - 1, Math.max(0, Math.floor(phase * cols)));
}

/**
 * The inverse, for the crosshair: which bar and beat a pixel column is.
 *
 * One-based, because that is what a DAW's transport shows -- Live's "2.3.1" is
 * bar 2, beat 3. A zero-based readout beside a host that counts from one is a
 * readout nobody can check against anything.
 */
export function posForSlot(slot, bars, num, denom, cols) {
  if (!(cols > 0) || !(bars > 0)) return { bar: 1, beat: 1 };
  const bpb = beatsPerBar(num, denom);
  const beats = (Math.min(cols - 1, Math.max(0, slot)) / cols) * bars * bpb;
  const bar = Math.floor(beats / bpb);
  return { bar: bar + 1, beat: beats - bar * bpb + 1 };
}

/*
 * The ticks under a bar-aligned picture: one per bar, always labelled, and the
 * beats inside it only when there is room.
 *
 * At 16 bars a bar is 38 pixels and its beats would be 9 apart -- a grey haze
 * rather than a grid, and the bar lines themselves would stop reading as the
 * strong ones. `MIN_BEAT_PX` is where that turns over.
 */
const MIN_BEAT_PX = 14;

export function barMarksFor(bars, num, denom, width) {
  if (!(bars > 0) || !(width > 0)) return [];
  const bpb = beatsPerBar(num, denom);
  const perBar = width / bars;
  const perBeat = perBar / bpb;
  const beats = perBeat >= MIN_BEAT_PX;

  const out = [];
  for (let b = 0; b < bars; b++) {
    out.push({ label: String(b + 1), x: b * perBar, beat: false });
    if (!beats) continue;
    /* From 1: beat 0 IS the bar line, and drawing both stacks two ticks. */
    for (let k = 1; k < Math.round(bpb); k++) {
      out.push({ label: '', x: b * perBar + k * perBeat, beat: true });
    }
  }
  return out;
}

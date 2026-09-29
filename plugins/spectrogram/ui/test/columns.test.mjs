/*
 * The wire, decoded: the three things in the editor that can be WRONG.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * An off-by-one in the hex decode is a picture shifted by one band, and a
 * spectrogram shifted by one band still looks exactly like a spectrogram. Same
 * for the axis: a scale drawn half a band out is unfalsifiable by eye. So these
 * are the parts with an oracle, and this is it.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

import {
  decodeColumns, decodeAxis, marksFor, RANGES,
  timeMarksFor, secondsAgo, dbForLevel, DB_FLOOR, DB_CEIL,
  decodeSync, beatsPerBar, slotForPpq, posForSlot, barMarksFor,
} from '../src/lib/columns.js';

/*
 * THE ENCODER IS NOT WRITTEN HERE ANY MORE.
 *
 * It used to be, and the comment above it admitted what that meant: "this is a
 * transcription of it, which is the point: if the two disagree, this test is
 * the disagreement". A transcription agrees with its source until somebody
 * edits one of them -- which is exactly how curves.js kept an un-mirrored
 * S-curve for as long as it existed, with a passing test beside it the whole
 * time.
 *
 * So the plugin's own Wire.cpp generates one table and both sides are checked
 * against it, neither against the other:
 *
 *   spectro_wire        Wire.cpp still agrees with wire_table.txt
 *   spectro_columns_js  decodeColumns still agrees with the same file
 *
 * Regenerate with `spectro_wire --dump > ui/test/wire_table.txt`, and only when
 * the wire format is MEANT to change.
 */
const here = dirname(fileURLToPath(import.meta.url));
const TABLE = process.env.SPECTRO_WIRE_TABLE ?? join(here, 'wire_table.txt');

/* "<cols> <bands> <b,b,...> <encoded>" -- the decimals are the input the
 * plugin encoded, so decoding the payload must reproduce them. */
const CASES = readFileSync(TABLE, 'utf8')
  .split('\n')
  .filter((l) => l.trim())
  .map((l) => {
    const [cols, bands, bytes, encoded] = l.split(' ');
    return {
      cols: Number(cols),
      bands: Number(bands),
      bytes: bytes.split(',').map(Number),
      encoded,
    };
  });

/* Still needed for the malformed-payload cases below, which are about what the
 * decoder REFUSES and so have no encoder side to generate them. */
const encode = (columns, bands) => {
  const hex = columns
    .flat()
    .map((b) => b.toString(16).toUpperCase().padStart(2, '0'))
    .join('');
  return `${columns.length}:${bands}:${hex}`;
};

test('the fixture is the shape it claims to be', () => {
  /* A shrinking fixture could quietly stop testing the cases that matter --
   * the 9 -> A nibble boundary, and a batch of more than one column. */
  assert.ok(CASES.length >= 7, `only ${CASES.length} cases in ${TABLE}`);
  assert.ok(CASES.some((c) => c.bands === 256), 'no case covering every byte value');
  assert.ok(CASES.some((c) => c.cols > 1), 'no multi-column case, so column order is untested');
  for (const c of CASES) {
    assert.equal(c.bytes.length, c.cols * c.bands, `${c.cols}x${c.bands} has ${c.bytes.length} bytes`);
  }
});

test('the decoder reproduces exactly what the plugin encoded', () => {
  /*
   * THE ONE THAT REPLACED THE TRANSCRIPTION. Every payload here came out of
   * the C++ the plugin actually runs, so an off-by-one in either the nibble
   * map or the column order is a failure rather than a shared assumption.
   */
  for (const { cols, bands, bytes, encoded } of CASES) {
    const got = decodeColumns(encoded);
    assert.ok(got, `rejected a payload the plugin produced: ${encoded.slice(0, 40)}`);
    assert.equal(got.count, cols);
    assert.equal(got.bands, bands);
    assert.deepEqual([...got.data], bytes,
      `decoded ${cols}x${bands} wrongly -- the nibble map or the column order`);
  }
});

test('a batch of columns round-trips', () => {
  const columns = [
    [0, 1, 15, 16, 127, 128, 254, 255],
    [255, 254, 128, 127, 16, 15, 1, 0],
    [0, 0, 0, 0, 0, 0, 0, 0],
  ];
  const got = decodeColumns(encode(columns, 8));
  assert.ok(got, 'a well-formed batch was rejected');
  assert.equal(got.count, 3);
  assert.equal(got.bands, 8);
  assert.deepEqual([...got.data], columns.flat());
});

test('every byte value survives the nibble table', () => {
  /* 0..255 as one 256-band column: the decode is a hand-written nibble map, and
   * the boundary it can get wrong is 9 -> A (char codes 57 -> 65). */
  const all = [Array.from({ length: 256 }, (_, i) => i)];
  const got = decodeColumns(encode(all, 256));
  assert.deepEqual([...got.data], all[0]);
});

test('a truncated payload is dropped whole', () => {
  /* The transport truncates rather than failing, and half a batch drawn anyway
   * puts a column of garbage in the middle of the picture -- which reads as a
   * real transient and cannot be told from one. */
  const text = encode([[1, 2, 3, 4]], 4);
  assert.equal(decodeColumns(text.slice(0, text.length - 2)), null);
});

test('nonsense is rejected rather than half-read', () => {
  for (const bad of ['', ':', '1:', '1:4', 'x:4:00', '0:4:', '1:0:', '-1:4:0000',
                     null, undefined, 42]) {
    assert.equal(decodeColumns(bad), null, `accepted ${JSON.stringify(bad)}`);
  }
});

test('the axis parses, and a broken one is refused', () => {
  const hz = decodeAxis('20.6,41.2,82.4');
  assert.equal(hz.length, 3);
  assert.ok(Math.abs(hz[1] - 41.2) < 1e-3);
  for (const bad of ['', '20,,40', '20,x,40', '20,-5', '20,0', null]) {
    assert.equal(decodeAxis(bad), null, `accepted ${JSON.stringify(bad)}`);
  }
});

test('a mark sits where its frequency sits, and the bass is at the bottom', () => {
  /*
   * AN AXIS WHOSE ENDS MAKE THE ANSWER ARITHMETIC. From 100 Hz to 10 kHz, 1 kHz
   * is the geometric centre, so it must land at exactly half the well's height,
   * the bottom end at the bottom and the top end at the top -- no tolerance
   * needed and no second copy of the log formula in the test to check it with.
   */
  const marks = marksFor(new Float32Array([100, 10000]), 268);
  const at = (label) => marks.find((m) => m.label === label);

  assert.ok(Math.abs(at('1k').y - 134) < 1e-6, `1 kHz at ${at('1k').y}, not 134`);
  assert.ok(Math.abs(at('100').y - 268) < 1e-6, `100 Hz at ${at('100').y}, not the bottom`);
  assert.ok(Math.abs(at('10k').y - 0) < 1e-6, `10 kHz at ${at('10k').y}, not the top`);

  /* y grows downwards, so a higher frequency has a SMALLER y. */
  assert.ok(at('10k').y < at('1k').y && at('1k').y < at('100').y, 'the axis is upside down');

  /* A DECADE IS A DECADE ANYWHERE ON THE SCALE, which is what makes it a log
   * axis rather than merely a curve that rises. */
  const decade = at('100').y - at('1k').y;
  assert.ok(Math.abs((at('1k').y - at('10k').y) - decade) < 1e-6, 'decades are uneven');

  for (const m of marks) assert.ok(m.y >= 0 && m.y <= 268, `${m.label} at ${m.y}`);
});

test('every range is one a picture can actually be drawn over', () => {
  /*
   * The engine refuses a range it cannot draw -- not finite, below 1 Hz, or
   * less than half an octave wide -- and a refusal shows up as a dropdown entry
   * that silently does nothing. Better to fail here.
   */
  assert.ok(RANGES.length >= 2, 'a dropdown of one is not a dropdown');
  for (const { name, lo, hi } of RANGES) {
    assert.ok(Number.isFinite(lo) && Number.isFinite(hi), `${name}: not finite`);
    assert.ok(lo >= 10, `${name}: starts at ${lo}, below the analyzer's floor`);
    assert.ok(hi <= 20000, `${name}: ends at ${hi}, above the analyzer's ceiling`);
    assert.ok(hi >= lo * 2, `${name}: ${lo}-${hi} is under an octave`);
  }
  /* The first is the whole picture: the view you come back to. */
  assert.equal(RANGES[0].lo, 10);
  assert.equal(RANGES[0].hi, 20000);

  /* Every range carries at least a couple of the scale's marks, or it is a
   * window with no numbers down its side. */
  for (const { name, lo, hi } of RANGES) {
    const n = 64;
    const ratio = (hi / lo) ** (1 / n);
    const hz = Float32Array.from(
      { length: n }, (_, i) => lo * ratio ** i * Math.sqrt(ratio)
    );
    assert.ok(marksFor(hz, 256).length >= 2, `${name}: ${marksFor(hz, 256).length} marks`);
  }
});

test('the scale reaches the bottom of the new axis', () => {
  /*
   * 10 and 20 Hz marks exist because the axis can now START there. At the old
   * 1024-point window the picture began at 47 Hz whatever f_min said, and a
   * 10 Hz mark would have pointed at nothing.
   */
  const marks = marksFor(new Float32Array([10, 20000]), 256);
  const labels = marks.map((m) => m.label);
  assert.deepEqual(labels, ['10', '20', '50', '100', '500', '1k', '5k', '10k', '20k']);

  const at = (label) => marks.find((m) => m.label === label);
  assert.ok(Math.abs(at('10').y - 256) < 1e-6, '10 Hz is not at the bottom');
  /* And the other end: 20 kHz is the top of the Full range, so the picture's
   * upper edge gets a number too rather than trailing off after 10k. */
  assert.ok(Math.abs(at('20k').y - 0) < 1e-6, '20 kHz is not at the top');
  assert.ok(at('10').y > at('20').y, 'the axis is upside down at the bottom');
  /* A decade is a decade: 10 -> 100 spans the same pixels as 1k -> 10k. */
  const low = at('10').y - at('100').y;
  const high = at('1k').y - at('10k').y;
  assert.ok(Math.abs(low - high) < 1e-6, `decades uneven: ${low} vs ${high}`);
});

test('the end marks survive the half-band between a centre and an edge', () => {
  /*
   * A REAL AXIS, not a two-element stand-in: 64 geometric centres from 10 Hz to
   * 20 kHz, the way the analyzer sends them. Each centre sits half a band above
   * its own lower edge, so measuring the picture's extent from the centres
   * alone loses the 10 Hz and 20 kHz marks -- the two a reader most wants.
   */
  const n = 64;
  const lo = 10, hi = 20000;
  const ratio = (hi / lo) ** (1 / n);
  const hz = Float32Array.from(
    { length: n },
    (_, i) => lo * ratio ** i * Math.sqrt(ratio)   /* centre of band i */
  );

  const labels = marksFor(hz, 256).map((m) => m.label);
  assert.ok(labels.includes('10'), `10 Hz was dropped: ${labels.join()}`);
  assert.ok(labels.includes('10k'), `10 kHz was dropped: ${labels.join()}`);

  const at = (l) => marksFor(hz, 256).find((m) => m.label === l);
  assert.ok(Math.abs(at('10').y - 256) < 0.5, `10 Hz at ${at('10').y}, not the bottom`);
  /* And the scale is still a log scale after the extension. */
  const decade = at('10').y - at('100').y;
  assert.ok(Math.abs((at('100').y - at('1k').y) - decade) < 1e-6, 'decades uneven');
});

test('marks outside the axis are left out rather than clamped to its edge', () => {
  /* A 32 kHz session: Nyquist is 16 kHz, so the analyzer's top band is below the
   * 10k mark's neighbours -- and a mark stacked on the frame would be a label
   * pointing at a frequency the picture does not show. */
  const marks = marksFor(new Float32Array([200, 8000]), 268);
  assert.deepEqual(marks.map((m) => m.label), ['500', '1k', '5k']);
  assert.equal(marksFor(null, 268).length, 0);
  assert.equal(marksFor(new Float32Array([1000]), 268).length, 0);
});


/* --------------------------------------------------------------- the level --
 *
 * These assert against the ENGINE's mapping, not against this file's: bands.rs
 * does `round(((dB - floor) / (ceil - floor)) * 255)`, and if that ever changes
 * the crosshair starts quietly naming the wrong decibel.
 */
test('a level byte reads back as the decibel the engine encoded', () => {
  assert.equal(dbForLevel(255), DB_CEIL, 'full scale is not the ceiling');
  assert.equal(dbForLevel(0), -Infinity, 'byte 0 is "at or below", not a number');

  /* The engine's own round trip: -48 dB is halfway up a -96..0 scale. */
  const byte = Math.round(((-48 - DB_FLOOR) / (DB_CEIL - DB_FLOOR)) * 255);
  assert.ok(Math.abs(dbForLevel(byte) - -48) < 0.2, `${dbForLevel(byte)} is not -48 dB`);

  /* Monotonic, or a brighter pixel could read as a quieter one. */
  for (let v = 2; v <= 255; v++) {
    assert.ok(dbForLevel(v) > dbForLevel(v - 1), `not monotonic at ${v}`);
  }
  assert.equal(dbForLevel(NaN), -Infinity);
  assert.equal(dbForLevel(-3), -Infinity);
});

/* ---------------------------------------------------------------- the time --
 *
 * The axis is arithmetic on the column rate, so these are the arithmetic.
 */
test('the time axis is a tick a second, right edge to left', () => {
  const marks = timeMarksFor(606, 47, 606);

  assert.equal(marks[0].label, '0s', 'the newest column is not at 0 s');
  assert.equal(marks[0].x, 606, '0 s is not at the right edge');
  assert.equal(marks[0].anchor, 'end', '0 s would hang over the frame');

  /* 606 columns at 47 a second is 12.89 s, so -12s fits and -13s does not. */
  assert.equal(marks.at(-1).label, '-12s');
  assert.ok(marks.at(-1).x > 0, 'the last tick fell off the left edge');
  assert.ok(!marks.some((m) => m.label === '-13s'), '-13s is past the picture');

  /* Evenly spaced, because one column is one pixel at a constant column rate. */
  const step = marks[0].x - marks[1].x;
  for (let i = 1; i < marks.length; i++) {
    const d = marks[i - 1].x - marks[i].x;
    assert.ok(Math.abs(d - step) < 1e-9, `uneven at ${marks[i].label}: ${d} vs ${step}`);
  }
  assert.ok(Math.abs(step - 47) < 1e-9, `a second is ${step}px, not 47`);

  /* Descending across the picture: later time to the right. */
  for (let i = 1; i < marks.length; i++) {
    assert.ok(marks[i].x < marks[i - 1].x, 'the time axis runs backwards');
  }
});

test('the time axis refuses arguments it cannot draw', () => {
  assert.deepEqual(timeMarksFor(0, 47, 606), []);
  assert.deepEqual(timeMarksFor(606, 0, 606), []);
  assert.deepEqual(timeMarksFor(606, 47, 0), []);
  assert.deepEqual(timeMarksFor(606, 47, 606, 0), []);
});

test('a column age is its own age in seconds', () => {
  assert.equal(secondsAgo(0, 47), 0, 'the newest column is not now');
  assert.ok(Math.abs(secondsAgo(47, 47) - 1) < 1e-9);
  assert.ok(Math.abs(secondsAgo(605, 47) - 605 / 47) < 1e-9);
  assert.equal(secondsAgo(-1, 47), 0);
  assert.equal(secondsAgo(10, 0), 0);
});


/* --------------------------------------------------------------- the bars --
 *
 * The bar view's whole claim is that a musical position maps to a PIXEL rather
 * than to an arrival order. These pin that mapping, its inverse, and the two
 * host-shaped edge cases that break a naive version of it.
 */
test('a sync message is taken whole or not at all', () => {
  const s = decodeSync('8.5:120:4:4:1:0.017');
  assert.deepEqual(s, { ppq: 8.5, bpm: 120, num: 4, denom: 4, running: true, ppqPerCol: 0.017 });
  assert.equal(decodeSync('8.5:120:4:4:0:0.017').running, false);

  /* A field short, or a field unreadable, and the whole message goes: placing
   * columns from half a clock is worse than not moving the picture. */
  assert.equal(decodeSync('8.5:120:4:4:1'), null, 'a short message was accepted');
  assert.equal(decodeSync('x:120:4:4:1:0.017'), null, 'a bad ppq was accepted');
  assert.equal(decodeSync('8.5:0:4:4:1:0.017'), null, 'a zero tempo was accepted');
  assert.equal(decodeSync('8.5:120:0:4:1:0.017'), null, 'a zero numerator was accepted');
  assert.equal(decodeSync('8.5:120:4:4:1:0'), null, 'a zero column length was accepted');
  assert.equal(decodeSync(null), null);
});

test('PPQ counts quarter notes, so 6/8 is three beats to the bar', () => {
  assert.equal(beatsPerBar(4, 4), 4);
  assert.equal(beatsPerBar(3, 4), 3);
  /* The one that catches a numerator-only reading: six eighths is three
   * quarters, and a host's PPQ is quarters whatever the metre says. */
  assert.equal(beatsPerBar(6, 8), 3);
  assert.equal(beatsPerBar(5, 4), 5);
  assert.equal(beatsPerBar(0, 4), 4, 'a nonsense signature is not a crash');
});

test('a musical position always lands on the same pixel', () => {
  const COLS = 606;
  const at = (ppq) => slotForPpq(ppq, 4, 4, 4, COLS);

  assert.equal(at(0), 0, 'the downbeat is not at the left edge');
  assert.equal(at(8), Math.floor((8 / 16) * COLS), 'half way is not half way');

  /* THE POINT OF THE FEATURE: one window later is the same pixel. */
  for (const ppq of [0, 1.5, 7.25, 13.75]) {
    assert.equal(at(ppq), at(ppq + 16), `${ppq} moved after one window`);
    assert.equal(at(ppq), at(ppq + 160), `${ppq} moved after ten windows`);
  }

  /* A count-in, or the playhead dragged before the start, reports a NEGATIVE
   * ppq -- and `%` keeps the sign of its left operand, so the naive version
   * indexes off the front of the buffer. */
  assert.ok(at(-0.5) > COLS / 2, `a negative ppq gave ${at(-0.5)}`);
  assert.equal(at(-16), at(0), 'a whole window back is not the downbeat');
  assert.equal(at(-0.5), at(15.5), 'the fold is not the same as the wrap');

  /* Never off either end, whatever arrives. */
  for (const ppq of [-1e9, -1e-9, 0, 1e9, 15.999999]) {
    const s = at(ppq);
    assert.ok(Number.isInteger(s) && s >= 0 && s < COLS, `${ppq} gave ${s}`);
  }
  assert.equal(slotForPpq(NaN, 4, 4, 4, COLS), 0);
  assert.equal(slotForPpq(4, 0, 4, 4, COLS), 0);
});

test('a pixel reads back as the bar and beat a DAW would show', () => {
  const COLS = 606;
  /* One-based, like every host's transport: Live's "2.3.1" is bar 2, beat 3. */
  assert.deepEqual(posForSlot(0, 4, 4, 4, COLS), { bar: 1, beat: 1 });

  /* CEIL, NOT FLOOR: the bar-2 line falls at pixel 151.5 of 606, so 151 is the
   * LAST pixel of bar 1 and 152 is the first of bar 2. Both are right; the
   * floor of the quarter point is simply on the other side of the line. */
  assert.equal(posForSlot(151, 4, 4, 4, COLS).bar, 1, 'pixel 151 left bar 1 early');
  const q = posForSlot(Math.ceil(COLS / 4), 4, 4, 4, COLS);
  assert.equal(q.bar, 2, 'a quarter of a 4-bar window is not bar 2');
  assert.ok(Math.abs(q.beat - 1) < 0.05, `beat ${q.beat} is not the downbeat`);

  const h = posForSlot(Math.floor(COLS / 2), 4, 4, 4, COLS);
  assert.equal(h.bar, 3);

  /* Round trip against the forward mapping, which is the thing that matters. */
  for (const ppq of [0, 1, 2.5, 7, 11.25, 15.5]) {
    const p = posForSlot(slotForPpq(ppq, 4, 4, 4, COLS), 4, 4, 4, COLS);
    const beats = (p.bar - 1) * 4 + (p.beat - 1);
    assert.ok(Math.abs(beats - ppq) < 0.05, `${ppq} came back as ${beats}`);
  }
  /* Always inside the window. */
  for (const slot of [-5, 0, 300, 605, 99999]) {
    const p = posForSlot(slot, 4, 4, 4, COLS);
    assert.ok(p.bar >= 1 && p.bar <= 4, `bar ${p.bar} is outside a 4-bar window`);
    assert.ok(p.beat >= 1 && p.beat < 5, `beat ${p.beat} is outside a bar`);
  }
});

test('the bar grid drops its beats when a bar gets too narrow', () => {
  const COLS = 606;

  const four = barMarksFor(4, 4, 4, COLS);
  const bars = four.filter((m) => !m.beat);
  assert.equal(bars.length, 4, 'not one tick a bar');
  assert.deepEqual(bars.map((m) => m.label), ['1', '2', '3', '4'], 'bars count from 1');
  assert.equal(four.filter((m) => m.beat).length, 12, 'not three beats inside each bar');
  /* A beat tick never sits on a bar line -- that would stack two ticks and make
   * the strong one unreadable. */
  for (const b of four.filter((m) => m.beat)) {
    assert.ok(!bars.some((x) => Math.abs(x.x - b.x) < 1e-9), `a beat sits on a bar at ${b.x}`);
  }

  /* Evenly spaced across the picture, starting at the left edge. */
  assert.equal(bars[0].x, 0);
  for (let i = 1; i < bars.length; i++) {
    assert.ok(Math.abs((bars[i].x - bars[i - 1].x) - COLS / 4) < 1e-9, 'bars uneven');
  }

  /* At 16 bars a bar is 38px and its beats would be 9 apart: a haze, not a
   * grid, and the bar lines stop reading as the strong ones. */
  const sixteen = barMarksFor(16, 4, 4, COLS);
  assert.equal(sixteen.filter((m) => !m.beat).length, 16);
  assert.ok(!sixteen.some((m) => m.beat), 'beat ticks survived at 16 bars');

  /* 6/8 is three beats, so two ticks inside each bar rather than three. */
  const six = barMarksFor(2, 6, 8, COLS);
  assert.equal(six.filter((m) => m.beat).length, 4, '6/8 did not get three beats a bar');

  assert.deepEqual(barMarksFor(0, 4, 4, COLS), []);
  assert.deepEqual(barMarksFor(4, 4, 4, 0), []);
});

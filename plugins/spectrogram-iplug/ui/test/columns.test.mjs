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

import { decodeColumns, decodeAxis, marksFor, RANGES } from '../src/lib/columns.js';

/* The plugin's own encoding: upper-case hex, two characters a byte, column after
 * column. See OnIdle in Spectrogram.cpp -- this is a transcription of it, which
 * is the point: if the two disagree, this test is the disagreement. */
const encode = (columns, bands) => {
  const hex = columns
    .flat()
    .map((b) => b.toString(16).toUpperCase().padStart(2, '0'))
    .join('');
  return `${columns.length}:${bands}:${hex}`;
};

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
  assert.deepEqual(labels, ['10', '20', '50', '100', '500', '1k', '5k', '10k']);

  const at = (label) => marks.find((m) => m.label === label);
  assert.ok(Math.abs(at('10').y - 256) < 1e-6, '10 Hz is not at the bottom');
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

/*
 * The JavaScript copy of the curve maths, pinned to the engine's own output.
 *
 * WHAT MAKES THIS TEST WORTH HAVING. It does not compare the JS against a
 * transcription of the Rust, and it does not compare it against the C. Both
 * sides read `shape_table.txt`, which `engines/side-chain/tests/shape_table.c`
 * produced by calling the engine -- so this asserts that the editor draws the
 * curve the DSP actually applies, which is the only claim anyone cares about.
 *
 * TOL is 1e-12 and not 1e-6 on purpose: the two implementations do the same
 * arithmetic in the same order, so they should agree to very nearly the last
 * bit. A loosened tolerance here would hide exactly the class of drift this
 * exists to catch -- `1 - Math.exp(-3)` instead of the spelled-out DENOM, say.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import { shape, duckAt, bounds, CURVES } from '../src/lib/shape.js';

const HERE = dirname(fileURLToPath(import.meta.url));
const TOL = 1e-12;

function rows(file) {
  return readFileSync(join(HERE, file), 'utf8')
    .split('\n')
    .filter((l) => l && !l.startsWith('#'))
    .map((l) => l.trim().split(/\s+/));
}

test('the fixture exists and covers every curve', () => {
  const r = rows('shape_table.txt');
  assert.ok(r.length > 3000, `only ${r.length} rows -- was the fixture regenerated?`);
  const seen = new Set(r.map((f) => f[0]));
  for (let c = 0; c < CURVES.length; c++) {
    assert.ok(seen.has(String(c)), `curve ${c} missing from the fixture`);
  }
  /* And NOTHING BEYOND them: a fourth curve in the fixture means the table was
   * regenerated from a build that still had `Pump`. */
  assert.equal(seen.size, CURVES.length, `fixture has ${seen.size} curves`);
});

test('shape matches the engine to 1e-12', () => {
  let n = 0;
  for (const [c, t, want] of rows('shape_table.txt')) {
    const got = shape(Number(c), Number(t));
    assert.ok(
      Math.abs(got - Number(want)) < TOL,
      `curve ${c} t=${t}: got ${got}, engine says ${want}`,
    );
    n++;
  }
  assert.ok(n > 3000, `only checked ${n} rows`);
});

test('every curve starts at 0, ends at 1, and never dips', () => {
  for (let c = 0; c < CURVES.length; c++) {
    assert.equal(shape(c, 0), 0, `${CURVES[c]} at 0`);
    assert.equal(shape(c, 1), 1, `${CURVES[c]} at 1`);
    let prev = -1;
    for (let k = 0; k <= 1000; k++) {
      const v = shape(c, k / 1000);
      assert.ok(v >= prev, `${CURVES[c]} dipped at ${k}`);
      assert.ok(v >= 0 && v <= 1, `${CURVES[c]} out of range at ${k}: ${v}`);
      prev = v;
    }
  }
});

test('out of range and NaN are clamped, not propagated', () => {
  /* A NaN gain silences a track permanently and cannot be recovered from, so
   * it must not survive the first guard. */
  for (let c = 0; c < CURVES.length; c++) {
    assert.equal(shape(c, NaN), 0);
    assert.equal(shape(c, -1), 0);
    assert.equal(shape(c, 2), 1);
  }
});

test('the S-curve is flat at both ends and antisymmetric about the middle', () => {
  /* THE BUG THIS PINS: an unmirrored first half leaves the floor vertically. */
  const d = 1e-3;
  const ends = shape(2, d) / d;
  const middle = (shape(2, 0.5 + d) - shape(2, 0.5 - d)) / (2 * d);
  assert.ok(ends < 0.5, `leaves the floor too fast: ${ends}`);
  assert.ok(middle > 1.5, `middle too slow: ${middle}`);
  assert.ok(middle > ends * 4, `not an S: ends=${ends} middle=${middle}`);
  for (let k = 0; k <= 500; k++) {
    const t = k / 1000;
    assert.ok(Math.abs(shape(2, t) + shape(2, 1 - t) - 1) < 1e-12);
  }
});

test('the exponential is the documented bend', () => {
  /* Halfway through, ~82% of the way. If CURVE_K moves, this says so. */
  assert.ok(Math.abs(shape(1, 0.5) - 0.8176) < 1e-3);
});

test('there are three curves, and an unknown index is Linear', () => {
  /*
   * `Pump` WAS A FOURTH, asymmetric one, and the direction argument existed
   * only to serve it. Both are gone. This asserts the removal rather than
   * merely not testing it: index 3 must now behave as Linear, which is the
   * fallback, and not as a curve of its own.
   */
  assert.equal(CURVES.length, 3);
  for (let k = 0; k <= 100; k++) {
    const t = k / 100;
    assert.equal(shape(3, t), shape(0, t), 'a removed curve must fall back to Linear');
    assert.equal(shape(99, t), shape(0, t));
  }
});


/* ------------------------------------------------- the drawing model ---- */

test('duckAt draws a single shot: flat, down, held, back, flat', () => {
  const p = { curve: 0, delay: 10, attack: 20, hold: 10, release: 40 };
  const b = bounds(p);
  /* `span` is the UNWRAPPED total, which is what the overrun mark reads. */
  assert.deepEqual(b, { start: 10, bottom: 30, holdEnd: 40, end: 80, span: 70 });

  assert.equal(duckAt(p, 0), 0, 'open before the delay expires');
  assert.equal(duckAt(p, 9.9), 0);
  assert.ok(Math.abs(duckAt(p, 20) - 0.5) < 1e-12, 'half way down a linear attack');
  assert.equal(duckAt(p, 30), 1, 'at the bottom');
  assert.equal(duckAt(p, 35), 1, 'held there');
  assert.ok(Math.abs(duckAt(p, 60) - 0.5) < 1e-12, 'half way back');
  assert.equal(duckAt(p, 80), 0, 'open again');
  assert.equal(duckAt(p, 200), 0, 'and stays open');
});

test('duckAt never leaves 0..1 for any settings or curve', () => {
  /* The editor divides by stage lengths, so a zero-length stage is the case
   * that produces an Infinity and draws a path with "NaN" in its `d`
   * attribute -- which renders as nothing at all and looks like a missing
   * component rather than a division. */
  for (let c = 0; c < CURVES.length; c++) {
    for (const p of [
      { curve: c, delay: 0, attack: 0, hold: 0, release: 0 },
      { curve: c, delay: 0, attack: 0, hold: 10, release: 0 },
      { curve: c, delay: 50, attack: 0, hold: 0, release: 30 },
      { curve: c, delay: 0, attack: 200, hold: 200, release: 200 },
      { curve: c },
    ]) {
      for (let k = 0; k <= 700; k++) {
        const v = duckAt(p, k);
        assert.ok(Number.isFinite(v), `not finite at t=${k}: ${v} for ${JSON.stringify(p)}`);
        assert.ok(v >= 0 && v <= 1, `out of range at t=${k}: ${v}`);
      }
    }
  }
});

/* ------------------------------------------------- the wrapped phase ---- */

test('on Cycle, a negative delay wraps to the end of the view', () => {
  /*
   * THE WHOLE POINT OF THE SIGN, in the drawing. The engine fires on
   * `floor(phase - offset)`, so -20% is a trigger at 80% of the cycle -- and
   * the duck that belongs to the next beat is already on screen at the right
   * of this one. Wrapping here is not a drawing trick; it is what the engine
   * does.
   */
  const early = { cycle: true, curve: 0, delay: -20, attack: 10, hold: 5, release: 20 };
  const b = bounds(early);
  assert.equal(b.start, 80, 'the duck begins at 80% of the cycle');
  assert.equal(b.bottom, 90);
  assert.equal(b.holdEnd, 95);
  assert.equal(b.end, 15, 'and finishes 15% into the NEXT cycle');
  assert.equal(b.span, 35, 'the span is unwrapped, so the overrun check works');

  assert.equal(duckAt(early, 79), 0, 'open before it begins');
  assert.ok(Math.abs(duckAt(early, 85) - 0.5) < 1e-12, 'half way down at 85');
  assert.equal(duckAt(early, 92), 1, 'held at the bottom across 90..95');
  assert.ok(Math.abs(duckAt(early, 5) - 0.5) < 1e-12, 'half way back, past the wrap');
  assert.equal(duckAt(early, 20), 0, 'open again');
});

test('off Cycle, a negative delay is simply no delay', () => {
  /* MIDI has nothing periodic to anticipate, so the engine clamps it -- and the
   * drawing has to clamp it the same way or the picture promises something the
   * sound will not do. */
  const midi = { cycle: false, curve: 0, delay: -20, attack: 10, hold: 5, release: 20 };
  const none = { cycle: false, curve: 0, delay: 0, attack: 10, hold: 5, release: 20 };
  for (let k = 0; k <= 100; k++) {
    assert.equal(duckAt(midi, k), duckAt(none, k), `differ at ${k}`);
  }
  assert.equal(bounds(midi).start, 0);
});

test('a positive delay draws the same whichever mechanism it is', () => {
  /* The engine lands a positive delay on identical samples either way, so the
   * two drawings must agree too -- otherwise switching source would appear to
   * move a duck that has not moved. */
  const cyc = { cycle: true, curve: 1, delay: 15, attack: 10, hold: 5, release: 20 };
  const wait = { ...cyc, cycle: false };
  for (let k = 0; k <= 100; k++) {
    assert.ok(Math.abs(duckAt(cyc, k) - duckAt(wait, k)) < 1e-12, `differ at ${k}`);
  }
});

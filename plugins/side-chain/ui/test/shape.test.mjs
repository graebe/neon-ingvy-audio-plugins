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
import { shape, shapeInv, duckAt, bounds, CURVES, DOWN, UP } from '../src/lib/shape.js';

const HERE = dirname(fileURLToPath(import.meta.url));
const TOL = 1e-12;

function rows(file) {
  return readFileSync(join(HERE, file), 'utf8')
    .split('\n')
    .filter((l) => l && !l.startsWith('#'))
    .map((l) => l.trim().split(/\s+/));
}

test('the fixture exists and covers every curve in both directions', () => {
  const r = rows('shape_table.txt');
  assert.ok(r.length > 8000, `only ${r.length} rows -- was the fixture regenerated?`);
  const seen = new Set(r.map((f) => `${f[0]}:${f[1]}`));
  for (let c = 0; c < CURVES.length; c++) {
    for (const d of [DOWN, UP]) {
      assert.ok(seen.has(`${c}:${d}`), `curve ${c} dir ${d} missing from the fixture`);
    }
  }
});

test('shape matches the engine to 1e-12', () => {
  let n = 0;
  for (const [c, d, t, want] of rows('shape_table.txt')) {
    const got = shape(Number(c), Number(t), Number(d));
    assert.ok(
      Math.abs(got - Number(want)) < TOL,
      `curve ${c} dir ${d} t=${t}: got ${got}, engine says ${want}`,
    );
    n++;
  }
  assert.ok(n > 8000, `only checked ${n} rows`);
});

test('shapeInv matches the engine to 1e-12', () => {
  for (const [c, d, t, , want] of rows('shape_table.txt')) {
    const got = shapeInv(Number(c), Number(t), Number(d));
    assert.ok(
      Math.abs(got - Number(want)) < TOL,
      `curve ${c} dir ${d} w=${t}: got ${got}, engine says ${want}`,
    );
  }
});

/*
 * The properties below are not redundant with the fixture. The fixture says
 * "these are the numbers"; these say "and these are the numbers we MEANT",
 * which is what catches a regenerated fixture that nobody intended.
 */

test('every curve starts at 0, ends at 1, and never dips', () => {
  for (let c = 0; c < CURVES.length; c++) {
    for (const d of [DOWN, UP]) {
      assert.equal(shape(c, 0, d), 0, `${CURVES[c]} dir ${d} at 0`);
      assert.equal(shape(c, 1, d), 1, `${CURVES[c]} dir ${d} at 1`);
      let prev = -1;
      for (let k = 0; k <= 1000; k++) {
        const v = shape(c, k / 1000, d);
        assert.ok(v >= prev, `${CURVES[c]} dir ${d} dipped at ${k}`);
        assert.ok(v >= 0 && v <= 1, `${CURVES[c]} dir ${d} out of range at ${k}: ${v}`);
        prev = v;
      }
    }
  }
});

test('out of range and NaN are clamped, not propagated', () => {
  /* A NaN gain silences a track permanently and cannot be recovered from, so
   * it must not survive the first guard. */
  for (let c = 0; c < CURVES.length; c++) {
    for (const d of [DOWN, UP]) {
      assert.equal(shape(c, NaN, d), 0);
      assert.equal(shape(c, -1, d), 0);
      assert.equal(shape(c, 2, d), 1);
      assert.equal(shapeInv(c, NaN, d), 0);
    }
  }
});

test('the S-curve is flat at both ends and antisymmetric about the middle', () => {
  /* THE BUG THIS PINS: an unmirrored first half leaves the floor vertically. */
  const d = 1e-3;
  const ends = shape(2, d, DOWN) / d;
  const middle = (shape(2, 0.5 + d, DOWN) - shape(2, 0.5 - d, DOWN)) / (2 * d);
  assert.ok(ends < 0.5, `leaves the floor too fast: ${ends}`);
  assert.ok(middle > 1.5, `middle too slow: ${middle}`);
  assert.ok(middle > ends * 4, `not an S: ends=${ends} middle=${middle}`);
  for (let k = 0; k <= 500; k++) {
    const t = k / 1000;
    assert.ok(Math.abs(shape(2, t, DOWN) + shape(2, 1 - t, DOWN) - 1) < 1e-12);
  }
});

test('the exponential is the documented bend', () => {
  /* Halfway through, ~82% of the way. If CURVE_K moves, this says so. */
  assert.ok(Math.abs(shape(1, 0.5, DOWN) - 0.8176) < 1e-3);
});

test('Pump is the only asymmetric curve', () => {
  for (let k = 0; k <= 100; k++) {
    const t = k / 100;
    assert.ok(Math.abs(shape(3, t, DOWN) - t) < 1e-12, 'Pump down is linear');
    const inv = 1 - t;
    assert.ok(Math.abs(shape(3, t, UP) - (1 - inv * inv * inv)) < 1e-12);
    for (const c of [0, 1, 2]) {
      assert.equal(shape(c, t, DOWN), shape(c, t, UP), `${CURVES[c]} is symmetric`);
    }
  }
});

test('shapeInv round-trips shape', () => {
  for (let c = 0; c < CURVES.length; c++) {
    for (const d of [DOWN, UP]) {
      for (let k = 1; k < 1000; k++) {
        const t = k / 1000;
        assert.ok(Math.abs(shapeInv(c, shape(c, t, d), d) - t) < 1e-9);
      }
    }
  }
});

/* ------------------------------------------------- the drawing model ---- */

test('duckAt draws a single shot: flat, down, held, back, flat', () => {
  const p = { curve: 0, delay: 10, attack: 20, hold: 10, release: 40 };
  const b = bounds(p);
  assert.deepEqual(b, { start: 10, bottom: 30, holdEnd: 40, end: 80 });

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

/*
 * The parameter ranges, which exist in two languages.
 *
 * WHY THEY EXIST TWICE AT ALL. The drawing reads the ENGINE's values -- the
 * `params` readout carries them in their own units, so no range arithmetic
 * stands between the picture and the DSP. But a drag has to go the other way: a
 * pointer position becomes a percentage, and a host parameter is a 0..1 number,
 * so `toNorm` needs the range.
 *
 * A duplicated range drifts silently and the symptom is subtle: the handle
 * lands somewhere the sound is not, which reads as a drawing bug and is an
 * arithmetic one. So this parses SideChain.cpp -- the declaration itself, not a
 * copy of it -- and fails if the two disagree.
 *
 * The same idiom as the token guard, which parses tokens.css as text, and the
 * versions test, which parses config.h: the authority is the file that the
 * build actually uses.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import { RANGES, P, toNorm, fromNorm } from '../src/lib/msg.js';

const HERE = dirname(fileURLToPath(import.meta.url));
const CPP = join(HERE, '..', '..', 'SideChain.cpp');

/* The C++ enum name for each parameter this test covers. Only the continuous
 * ones: an enum parameter's range is its option count, which COUNTS already
 * carries and ParamSelect derives from. */
const ENUM_NAME = {
  kDelay: P.delay,
  kAttack: P.attack,
  kHold: P.hold,
  kRelease: P.release,
  kDepth: P.depth,
  kVelSens: P.velSens,
  kThreshold: P.threshold,
  kLockout: P.lockout,
};

/** What SideChain.cpp actually declares, in its own units. */
function declared() {
  const src = readFileSync(CPP, 'utf8')
    /* Comments first: a range inside one is prose, not a declaration. */
    .replace(/\/\*[\s\S]*?\*\//g, '')
    .replace(/\/\/[^\n]*/g, '');
  const out = {};

  /* The `pct` helper: pct(GetParam(kAttack), "Attack", 2.0, 200.0) -- it calls
   * InitDouble with a fixed low bound of 0. */
  for (const m of src.matchAll(
    /pct\(GetParam\((k\w+)\)\s*,\s*"[^"]*"\s*,\s*([-\d.]+)\s*,\s*([-\d.]+)\s*\)/g)) {
    out[m[1]] = [0, Number(m[3])];
  }

  /* The direct form: GetParam(kThreshold)->InitDouble("Threshold", -24, -60, 0, ...) */
  for (const m of src.matchAll(
    /GetParam\((k\w+)\)->InitDouble\(\s*"[^"]*"\s*,\s*([-\d.]+)\s*,\s*([-\d.]+)\s*,\s*([-\d.]+)/g)) {
    out[m[1]] = [Number(m[3]), Number(m[4])];
  }
  return out;
}

test('SideChain.cpp declares every range this editor knows about', () => {
  const d = declared();
  for (const name of Object.keys(ENUM_NAME)) {
    assert.ok(d[name], `${name} not found in SideChain.cpp -- was it renamed?`);
  }
});

test('the editor\'s ranges are the plugin\'s ranges', () => {
  const d = declared();
  for (const [name, idx] of Object.entries(ENUM_NAME)) {
    assert.deepEqual(
      RANGES[idx], d[name],
      `${name}: SideChain.cpp says [${d[name]}], msg.js says [${RANGES[idx]}]`,
    );
  }
});

test('toNorm and fromNorm are inverses across every range', () => {
  for (const idx of Object.values(ENUM_NAME)) {
    const [lo, hi] = RANGES[idx];
    for (let k = 0; k <= 20; k++) {
      const v = lo + ((hi - lo) * k) / 20;
      const back = fromNorm(idx, toNorm(idx, v));
      assert.ok(Math.abs(back - v) < 1e-9, `${idx}: ${v} -> ${back}`);
    }
    /* And the ends land exactly on 0 and 1 -- an enum's last option being
     * unreachable by automation is the classic version of this bug. */
    assert.equal(toNorm(idx, lo), 0);
    assert.equal(toNorm(idx, hi), 1);
  }
});

test('out of range clamps rather than extrapolating', () => {
  /* A drag runs past the end of the well constantly; letting it produce a
   * normalised value outside 0..1 would hand the host a number it is entitled
   * to reject, and the handle would stick. */
  for (const idx of Object.values(ENUM_NAME)) {
    const [lo, hi] = RANGES[idx];
    assert.equal(toNorm(idx, lo - 1000), 0);
    assert.equal(toNorm(idx, hi + 1000), 1);
    assert.equal(fromNorm(idx, -5), lo);
    assert.equal(fromNorm(idx, 5), hi);
  }
});

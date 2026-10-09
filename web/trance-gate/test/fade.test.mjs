// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The fade's weights in JS, against the ENGINE's measured gain.
 *
 * ui/src/lib/fade.js is a second implementation of something the DSP owns, and
 * this is what makes that tolerable. The fixture is not a transcription of
 * recalc_fade: tests/fade_table.c runs the real engine with a DC input and no
 * envelope, so the gain during a step IS that step's arrival weight. If the
 * weight table and the gain law ever disagree, the fixture reports the gain law
 * -- the one a listener hears -- and this test reports the UI against it.
 *
 * node's own test runner, so the editor keeps a dependency tree of vite and solid
 * and nothing else. That is the licence audit, not a preference.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

import { fadeWeight, fadeWeights } from '../src/lib/fade.js';

const here = dirname(fileURLToPath(import.meta.url));
const TABLE = process.env.TG_FADE_TABLE ?? join(here, 'fade_table.txt');

/* The engine quantises a step's level to a byte on the way in, and these
 * weights only ever multiply a FULL depth -- so the measured gain is the weight
 * to within rounding. Generous by a wide margin, and still tight enough that a
 * wrong formula cannot hide. */
const TOL = 1e-4;

const rows = readFileSync(TABLE, 'utf8')
  .split('\n')
  .filter((l) => l.trim() && !l.startsWith('#'))
  .map((l) => {
    const f = l.trim().split(/\s+/);
    return {
      pattern: f[0],
      out: f[1] === '1',
      soft: f[2] === '1',
      fade: Number(f[3]),
      /* The drawn mask. Every step carries a rank now -- among its own kind --
       * so the rank alone can no longer say which kind a step is. */
      steps: f.slice(4, 20).map((v) => v === '1'),
      ranks: f.slice(20, 36).map(Number),
      want: f.slice(36, 52).map(Number),
    };
  });

test('the fixture covers what it is supposed to', () => {
  assert.ok(rows.length >= 100, `only ${rows.length} rows`);
  assert.ok(rows.some((r) => r.fade === 0), 'no fade 0 case');
  assert.ok(rows.some((r) => r.fade === 1), 'no fade 1 case');
  assert.ok(rows.some((r) => r.soft), 'no soft case');
  assert.ok(rows.some((r) => !r.soft), 'no hard case');
  assert.ok(rows.some((r) => r.out), 'no Fade Out case');
  assert.ok(rows.some((r) => !r.out), 'no Fade In case');
  /* The float nearest 1 from beneath. Without the epsilon in both
   * implementations the last arrival is silent at exactly this setting and
   * nowhere else, which is why it is in the sweep. */
  assert.ok(rows.some((r) => r.fade > 0.999999 && r.fade < 1),
            'no just-under-1 case, which is where a missing epsilon hides');
});

test('every weight matches the engine', () => {
  let worst = 0, worstAt = null;
  for (const r of rows) {
    const got = fadeWeights(r.ranks, r.steps, 16, r.fade, r.soft, r.out);
    for (let i = 0; i < 16; i++) {
      const d = Math.abs(got[i] - r.want[i]);
      if (d > worst) { worst = d; worstAt = { ...r, i, got: got[i] }; }
    }
  }
  assert.ok(worst <= TOL,
    worstAt
      ? `${worstAt.pattern} out=${worstAt.out} soft=${worstAt.soft} ` +
        `fade=${worstAt.fade} step ${worstAt.i}: ` +
        `got ${worstAt.got}, engine ${worstAt.want[worstAt.i]} (off by ${worst})`
      : `off by ${worst}`);
});

/*
 * The named rules, asserted directly as well as through the table. The fixture
 * would catch all of these, but it would report "row 84, step 12" -- and what a
 * failure needs to say is WHICH PROMISE broke.
 */
const ALL_ON = [true, true, true, true];

test('0% is nothing and 100% is everything', () => {
  const ranks = [1, 2, 3, 4];
  assert.deepEqual(fadeWeights(ranks, ALL_ON, 4, 0, false), [0, 0, 0, 0]);
  assert.deepEqual(fadeWeights(ranks, ALL_ON, 4, 0, true), [0, 0, 0, 0]);
  assert.deepEqual(fadeWeights(ranks, ALL_ON, 4, 1, false), [1, 1, 1, 1]);
  assert.deepEqual(fadeWeights(ranks, ALL_ON, 4, 1, true), [1, 1, 1, 1]);
});

/*
 * FADE OUT IS THE SAME FORMULA AGAINST THE OTHER KIND. The holes arrive instead
 * of the hits, an arriving hole ramps its level DOWN rather than up, and 100% is
 * the drawn pattern in both directions -- which is what keeps it the neutral
 * default and lets the direction be switched at rest.
 */
test('Fade Out fills the holes and leaves the hits alone', () => {
  const steps = [true, false, false, false];    /* one hit, three holes */
  const ranks = [1, 1, 2, 3];                   /* each among its own kind */
  assert.deepEqual(fadeWeights(ranks, steps, 4, 0, false, true), [1, 1, 1, 1],
                   'out 0%: every hole filled');
  assert.deepEqual(fadeWeights(ranks, steps, 4, 1, false, true), [1, 0, 0, 0],
                   'out 100%: the pattern as drawn');
  assert.deepEqual(fadeWeights(ranks, steps, 4, 1 / 3, false, true), [1, 0, 1, 1],
                   'out 1/3: the first hole has gone');
});

test('the two directions agree at 100%', () => {
  const steps = [true, false, true, false];
  const ranks = [1, 1, 2, 2];
  assert.deepEqual(fadeWeights(ranks, steps, 4, 1, false, false),
                   fadeWeights(ranks, steps, 4, 1, false, true));
});

test('soft Out ramps a hole DOWN, which is the dual of a hit ramping up', () => {
  const steps = [true, false, false, false];
  const ranks = [1, 1, 2, 3];
  /* 1.5 arrivals of 3: the first hole gone, the second half way down. */
  assert.deepEqual(fadeWeights(ranks, steps, 4, 0.5, true, true), [1, 0, 0.5, 1]);
});

test('a pattern with no holes is unmoved by Fade Out', () => {
  const ranks = [1, 2, 3, 4];
  for (const f of [0, 0.25, 0.5, 1])
    assert.deepEqual(fadeWeights(ranks, ALL_ON, 4, f, false, true), [1, 1, 1, 1],
                     `at ${f}`);
});

test('the arrivals are equidistant: rank r crosses at r/n', () => {
  for (let n = 1; n <= 16; n++) {
    for (let r = 1; r <= n; r++) {
      assert.equal(fadeWeight(r, n, r / n, false), 1,
                   `rank ${r} of ${n} should be in at ${r}/${n}`);
      /* Just below its own crossing it must still be out. A hair under, because
       * r/n is exactly representable for the powers of two and this has to hold
       * for the others too. */
      assert.equal(fadeWeight(r, n, r / n - 1e-3, false), 0,
                   `rank ${r} of ${n} should be out just under ${r}/${n}`);
    }
  }
});

test('soft has exactly one arrival part-way in, never two', () => {
  for (let pct = 1; pct < 100; pct++) {
    const w = fadeWeights([1, 2, 3, 4, 5], [true, true, true, true, true],
                          5, pct / 100, true);
    const partial = w.filter((v) => v > 1e-9 && v < 1 - 1e-9).length;
    assert.ok(partial <= 1, `fade ${pct}%: ${partial} arrivals part-way in`);
  }
});

test('soft and hard agree at every arrival boundary', () => {
  const n = 5, ranks = [1, 2, 3, 4, 5];
  const on = [true, true, true, true, true];
  for (let r = 0; r <= n; r++) {
    const f = r / n;
    assert.deepEqual(fadeWeights(ranks, on, n, f, true), fadeWeights(ranks, on, n, f, false),
      `at ${r}/${n} the two shapes must place the arrivals identically`);
  }
});

test('soft really ramps, rather than being hard with extra steps', () => {
  /* 1.5 arrivals of 4: the first fully in, the second half in, the rest out. */
  assert.deepEqual(fadeWeights([1, 2, 3, 4], ALL_ON, 4, 0.375, true), [1, 0.5, 0, 0]);
});

test('a step with no rank has no weight', () => {
  assert.equal(fadeWeight(0, 4, 1, false), 0, 'rank 0 is a gap, not an arrival');
  assert.equal(fadeWeight(0, 4, 1, true), 0);
  /* And it does not count towards the divisor either: two on steps out of
   * eight means halves, not eighths. */
  const steps = [true, false, false, false, true, false, false, false];
  const w = fadeWeights([1, 1, 2, 3, 2, 4, 5, 6], steps, 8, 0.5, false);
  assert.deepEqual(w, [1, 0, 0, 0, 0, 0, 0, 0]);
});

test('it is monotone in the knob', () => {
  for (const soft of [false, true]) {
    for (let r = 1; r <= 5; r++) {
      let prev = -1;
      for (let pct = 0; pct <= 100; pct++) {
        const v = fadeWeight(r, 5, pct / 100, soft);
        assert.ok(v >= prev - 1e-12,
                  `rank ${r} soft=${soft} went down at ${pct}%: ${prev} -> ${v}`);
        prev = v;
      }
    }
  }
});

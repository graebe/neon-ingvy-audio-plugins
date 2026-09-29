/*
 * The UI's envelope, against the engine's measured output.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * The companion to curves.test.mjs, and for the same reason: the editor draws
 * the envelope, so the stage machine exists in JavaScript as well as in Rust,
 * and the copy had drifted. What makes this one different is that the fixture
 * is not a transcription of the engine's formulas -- it is the engine's
 * OUTPUT, rendered with a DC input at amount 1 where the gain it applies is
 * the envelope by definition. Whatever the DSP does, including anything nobody
 * modelled, is in the table.
 *
 * See tests/envelope_table.c. Regenerate with
 *   tg_envelope_table > ui/test/envelope_table.txt
 * and only when the envelope is MEANT to change.
 *
 *   node --test ui/test/envelope.test.mjs
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import { gateAt, envLevel, stageLevel } from '../src/lib/curves.js';

const here = dirname(fileURLToPath(import.meta.url));
const TABLE = process.env.TG_ENVELOPE_TABLE ?? join(here, 'envelope_table.txt');

/* Matches TOL in envelope_table.c — see the note there on why it is 3e-3 and
 * not smaller: the engine's stage clock ticks per sample and this compares
 * against a level derived from the pattern phase. */
const TOL = 3e-3;
const CURVES = ['linear', 'exponential', 's-curve'];

const rows = readFileSync(TABLE, 'utf8').trim().split('\n').map((l) => {
  const [ci, curve, a, d, s, r, hold, t, v] = l.split(' ').map(Number);
  return { ci, p: { curve, attack: a, decay: d, sustain: s, release: r, width: hold }, t, v };
});

/* One entry per case, so a failure names the settings rather than a row. */
const cases = new Map();
for (const row of rows) {
  if (!cases.has(row.ci)) cases.set(row.ci, { p: row.p, pts: [] });
  cases.get(row.ci).pts.push(row);
}

const label = (p) =>
  `${CURVES[p.curve]} a=${p.attack}% d=${p.decay}% s=${p.sustain} ` +
  `r=${p.release}% width=${p.width}`;

test('the fixture covers the grid it claims to', () => {
  assert.ok(cases.size >= 45, `only ${cases.size} cases`);
  assert.ok(rows.every((r) => Number.isFinite(r.v) && Number.isFinite(r.t)));
  /* Every curve, and both ends of every awkward axis, must be represented —
   * otherwise a shrinking fixture could quietly stop testing the cases that
   * caught the original bug. */
  const P = [...cases.values()].map((c) => c.p);
  for (const c of [0, 1, 2]) assert.ok(P.some((p) => p.curve === c), `no curve ${c}`);
  assert.ok(P.some((p) => (p.attack + p.decay) / 100 * p.width > p.width),
    'no case where attack+decay outlast the gate');
  assert.ok(P.some((p) => p.width >= 1), 'no case at Width 100%');
  assert.ok(P.some((p) => p.attack === 0 && p.decay === 0 && p.release === 0),
    'no case with every stage at zero length');
  assert.ok(P.some((p) => p.sustain === 0) && P.some((p) => p.sustain === 1),
    'sustain is not tested at both extremes');
});

for (const [ci, c] of cases) {
  test(`case ${ci}: ${label(c.p)}`, () => {
    /*
     * THE CARRY-IN COMES FROM THE MEASUREMENT, and that is the point rather
     * than a concession. `att_from` depends on what the NEIGHBOURING steps
     * did, which a per-step function cannot know and must be told; the caller
     * that does know threads it (see PatternPlot). Taking the engine's own
     * level at the start of the step tests the shape the whole way up
     * INCLUDING an attack that begins part way, which is the thing five of
     * these cases exercise and nothing else would.
     */
    const from = c.pts[0].v;
    let worst = 0, at = 0, got = 0, want = 0;
    for (const row of c.pts) {
      const v = gateAt(c.p, row.t, from);
      const err = Math.abs(v - row.v);
      if (err > worst) { worst = err; at = row.t; got = v; want = row.v; }
    }
    assert.ok(worst <= TOL,
      `worst ${worst.toExponential(2)} at t=${at.toFixed(4)}: ` +
      `UI ${got.toFixed(5)} vs engine ${want.toFixed(5)} (tolerance ${TOL})`);
  });
}

/*
 * The two rules the old model broke, asserted directly so a failure says WHICH
 * rule rather than just naming a case.
 */
test('the gate shuts at Width whatever stage is running', () => {
  /* Attack alone is twice the gate: the engine shuts mid-attack. */
  const p = { curve: 0, attack: 200, decay: 50, sustain: 0.5, release: 25, width: 0.5 };
  assert.ok(Math.abs(gateAt(p, 0.499) - 0.5) < 0.01,
    'half way up a linear attack that is twice the gate, at the moment it shuts');
  assert.ok(gateAt(p, 0.55) < gateAt(p, 0.5), 'it must be falling after the shut');
  assert.equal(gateAt(p, 0.7), 0, 'and be shut once the release has run');
});

test('the release falls from the level reached, not from sustain', () => {
  /* Shut half way up the attack, so the release must start at ~0.5 and not at
   * sustain (0.9) — the old model drew a jump up at the gate close. */
  const p = { curve: 0, attack: 200, decay: 0, sustain: 0.9, release: 100, width: 0.5 };
  const atShut = gateAt(p, 0.5 - 1e-6);
  const justAfter = gateAt(p, 0.5 + 1e-6);
  assert.ok(Math.abs(atShut - 0.5) < 0.01, `expected ~0.5 at the shut, got ${atShut}`);
  assert.ok(Math.abs(justAfter - atShut) < 0.01,
    `the release jumped from ${atShut} to ${justAfter}`);
});

test('a zero-length release is a cut, not a hold', () => {
  const p = { curve: 0, attack: 0, decay: 0, sustain: 0.5, release: 0, width: 0.5 };
  assert.equal(gateAt(p, 0.25), 0.5);
  assert.equal(gateAt(p, 0.5), 0, 'shut the instant the gate closes');
});

test('stageLevel is the envelope as dialled, with no gate over it', () => {
  /* What the ghost curve draws: the same settings, ungated, keep decaying to
   * sustain past the point the real gate would have shut. */
  const e = { curve: 0, attack: 0.1, decay: 0.4, sustain: 0.25, release: 0.1, gate: 0.2 };
  assert.ok(Math.abs(stageLevel(e, 0.5) - 0.25) < 1e-9, 'reaches sustain ungated');
  assert.equal(envLevel(e, 0.5), 0, 'while the gated one has shut and released');
});

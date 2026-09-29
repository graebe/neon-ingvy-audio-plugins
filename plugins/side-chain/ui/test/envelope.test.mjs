/*
 * The editor's drawing model against the envelope the DSP actually produces.
 *
 * THE FIXTURE IS MEASURED, NOT TRANSCRIBED, and that is the whole point. It is
 * the OUTPUT of a DC input of 1.0 at depth 1.0, so each value is literally
 * `1 - duck` read off the thing that makes the sound. No model of the envelope
 * was consulted in producing it -- which is how the Trance Gate's equivalent
 * caught `att_from`: five of sixty cases started part way up the scale while
 * the model insisted they started at zero.
 *
 * WHAT IS DELIBERATELY NOT CHECKED HERE: the `retrig-` cases.
 *
 * `duckAt` draws the IDEALISED SINGLE SHOT, because the editor is where you say
 * what shape you want. The engine anchors a retrigger on the level it has
 * actually reached, so a duck that fires again mid-recovery does not retrace
 * the drawn curve -- and no editor drawing can show that, since it depends on
 * when the next trigger arrives. The plugin pushes the measured gain-reduction
 * trace for that. The editor says the intent; the trace says what happened.
 *
 * Skipping them silently would be the bug, so this file asserts they are
 * present, counts them, and says why it is leaving them alone.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import { duckAt } from '../src/lib/shape.js';

const HERE = dirname(fileURLToPath(import.meta.url));

/* The fixture's own conditions, stated in its header: 48 kHz, 120 bpm, rate
 * 1/4 -- so one cycle is 24 000 samples and a sample index divided by 240 is a
 * percentage of the cycle, which is the unit the stage lengths are in. */
const SAMPLES_PER_CYCLE = 24000;
const PCT = SAMPLES_PER_CYCLE / 100;

/*
 * TOL is 3e-3, and the reason is structural rather than a shrug.
 *
 * The engine walks the envelope one sample at a time with `pos` counting whole
 * samples, so its value at a stage boundary is quantised to the sample grid;
 * `duckAt` evaluates the same curve in continuous time. Over a stage a thousand
 * samples long the two differ by well under a thousandth, except within one
 * sample of a boundary, where a decimated fixture may catch the step. 3e-3
 * covers that and is still far tighter than any visible difference in a plot.
 */
const TOL = 3e-3;

function cases() {
  const text = readFileSync(join(HERE, 'envelope_table.txt'), 'utf8');
  const out = [];
  let cur = null;
  for (const line of text.split('\n')) {
    const m = line.match(
      /^# case (\S+) curve=(\d+) delay=(\S+) attack=(\S+) hold=(\S+) release=(\S+) retrigger=(\S+)/,
    );
    if (m) {
      cur = {
        /*
         * `cycle: false` BECAUSE THE FIXTURE IS RENDERED ON THE MIDI SOURCE.
         * There Delay is a wait rather than a phase, which is the branch
         * `duckAt` has to take to match these numbers. Stated rather than left
         * to `undefined` being falsy, because that is an accident waiting to
         * become a wrong picture.
         */
        cycle: false,
        name: m[1],
        curve: Number(m[2]),
        delay: Number(m[3]),
        attack: Number(m[4]),
        hold: Number(m[5]),
        release: Number(m[6]),
        retrigger: Number(m[7]),
        rows: [],
      };
      out.push(cur);
      continue;
    }
    if (!line || line.startsWith('#') || !cur) continue;
    const [i, g] = line.trim().split(/\s+/);
    cur.rows.push([Number(i), Number(g)]);
  }
  return out;
}

test('the fixture covers every curve and both kinds of case', () => {
  const all = cases();
  assert.ok(all.length >= 27, `only ${all.length} cases -- was it regenerated?`);
  const curves = new Set(all.map((c) => c.curve));
  /* THREE. A fourth here means the fixture was generated from a build that
   * still had `Pump`. */
  assert.deepEqual([...curves].sort(), [0, 1, 2]);
  /* Two retrigger cases per curve, and three curves. */
  const retrig = all.filter((c) => c.retrigger > 0);
  assert.equal(retrig.length, 6, 'the retrigger cases are missing from the fixture');
  for (const c of all) assert.ok(c.rows.length > 300, `${c.name}: only ${c.rows.length} rows`);
});

test('the drawn shape is the measured envelope, for a single shot', () => {
  let checked = 0;
  for (const c of cases()) {
    if (c.retrigger > 0) continue; /* see the header */
    for (const [i, gain] of c.rows) {
      const want = 1 - duckAt(c, i / PCT);
      assert.ok(
        Math.abs(gain - want) < TOL,
        `${c.name} curve=${c.curve} sample ${i}: engine ${gain}, drawing ${want}`,
      );
      checked++;
    }
  }
  assert.ok(checked > 8000, `only checked ${checked} samples`);
});

test('the retrigger cases are left to the engine, and they do retrigger', () => {
  /* Not a skipped test: this asserts the reason for skipping is real. A
   * retrigger mid-release must turn the gain back DOWN, and the drawing model
   * by construction cannot know that happened. */
  const retrig = cases().filter((c) => c.retrigger > 0);
  for (const c of retrig) {
    const at = c.retrigger;
    const before = c.rows.filter(([i]) => i > at - 600 && i <= at).map(([, g]) => g);
    const after = c.rows.filter(([i]) => i > at && i < at + 600).map(([, g]) => g);
    if (!before.length || !after.length) continue;
    const peak = Math.max(...before);
    const trough = Math.min(...after);
    assert.ok(
      trough < peak,
      `${c.name} curve=${c.curve}: gain did not fall after the retrigger at ${at}`,
    );
    /* And it must be CONTINUOUS across the retrigger -- the anchoring. A jump
     * back to unity for one sample is the click this was built to avoid. */
    const lastBefore = before[before.length - 1];
    const firstAfter = after[0];
    assert.ok(
      Math.abs(firstAfter - lastBefore) < 0.05,
      `${c.name} curve=${c.curve}: gain jumped ${lastBefore} -> ${firstAfter} on retrigger`,
    );
  }
});

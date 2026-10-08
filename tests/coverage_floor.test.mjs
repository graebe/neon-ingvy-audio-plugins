// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The coverage floor: what the suite reaches, and what it is allowed not to.
 *
 * AGENTS.md has asked for this from the beginning -- "use coverage tests with
 * human & machine readable outputs", "target coverage: >80%" -- and the word
 * `coverage` appeared nowhere in the repository until this file.
 *
 * WHY A TEST AND NOT A BADGE. A percentage printed at the end of a build is
 * read once and then stops being read. This is the token guard's argument
 * (tests/site_tokens.test.mjs), which the repository has already settled once:
 *
 *   "This asserts agreement rather than generating the CSS from it: the same
 *    habit as the curve and envelope oracles, and for the same reason -- a
 *    generator hides a disagreement by overwriting it, where a test names it."
 *
 * So this NAMES the units below the line, in the same offence format the token
 * guard uses, rather than reporting a number that went down.
 *
 * WHY THE EXEMPTIONS ARE AUDITED TOO. An exemption is the only way to make a
 * coverage number say whatever you want, so the list of them is checked as
 * carefully as the figures: every entry must point at something that still
 * exists, and must carry a REASON rather than a note. That trick is the token
 * guard's as well -- "EXEMPT lists a token the system no longer defines" is a
 * stale excuse, and a stale excuse is how a floor quietly stops meaning
 * anything.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const here = dirname(fileURLToPath(import.meta.url));
const ROOT = join(here, '..');

const FLOORS = JSON.parse(readFileSync(join(here, 'coverage.floors.json'), 'utf8'));

/* The report, from the environment, so that this runs both under ctest and
 * under a bare `node --test`. */
const REPORT = process.env.COVERAGE_JSON ?? join(ROOT, 'build', 'coverage', 'coverage.json');

const HOW = 'Run ./scripts/coverage.sh to produce it.';

test('there is a coverage report to check', () => {
  assert.ok(existsSync(REPORT), `no report at ${REPORT}. ${HOW}`);
});

const report = existsSync(REPORT) ? JSON.parse(readFileSync(REPORT, 'utf8')) : null;

/* The verdict is computed and printed either way; `enforcing` decides only
 * whether a shortfall is a failure. It is on -- see `enforcing_note` in
 * coverage.floors.json -- and stays a switch so a deliberate, dated step back
 * to report-only is one visible line rather than an edited test. */
const enforce = (offences, message) => {
  if (offences.length === 0) return;
  if (FLOORS.enforcing) assert.fail(message);
  else console.log(`\n  NOT YET ENFORCED (coverage.floors.json: "enforcing": false)\n${message}\n`);
};

const pctOf = (u) => (u?.lines?.pct ?? null);

test('every unit meets its floor', () => {
  assert.ok(report, `no report at ${REPORT}. ${HOW}`);

  const offences = [];
  for (const [name, unit] of Object.entries(report.units)) {
    if (FLOORS.exempt[name]) continue;

    const floor = FLOORS.units[name] ?? FLOORS.floor;
    const got = pctOf(unit);

    /* A unit with no measurable lines is not a pass. It means nothing in it
     * was instrumented -- a cross-build, a header-only target, or a path the
     * report's filters are dropping -- and a silent zero-denominator is how a
     * whole plugin can disappear out of the total without anyone noticing. */
    if (got === null) {
      offences.push(`${name.padEnd(40)}  no instrumented lines  --  ` +
                    `nothing counted it, so it is neither passing nor failing`);
      continue;
    }

    if (got < floor) {
      offences.push(`${name.padEnd(40)}  ${got.toFixed(1).padStart(5)}%  ` +
                    `(floor ${floor.toFixed(1)})  --  ` +
                    `${unit.lines.hit}/${unit.lines.total} lines`);
    }
  }

  enforce(offences,
    `units below their floor:\n  ${offences.join('\n  ')}\n\n` +
    'Either test them, or add an entry to tests/coverage.floors.json "exempt" ' +
    'saying why the coverage cannot be had -- which the third test here will ' +
    'then hold you to.');
});

test('the repository as a whole meets the floor', () => {
  assert.ok(report, `no report at ${REPORT}. ${HOW}`);

  const got = pctOf(report.total);
  assert.ok(got !== null, 'the report has no measurable lines at all');

  const offences = got < FLOORS.floor
    ? [`TOTAL${' '.repeat(35)}  ${got.toFixed(1).padStart(5)}%  ` +
       `(floor ${FLOORS.floor.toFixed(1)})  --  ` +
       `${report.total.lines.hit}/${report.total.lines.total} lines`]
    : [];

  enforce(offences, `the total is below the floor:\n  ${offences.join('\n  ')}`);
});

test('the deliberate gaps are still deliberate', () => {
  assert.ok(report, `no report at ${REPORT}. ${HOW}`);

  /* This one is NOT report-only. An exemption that has gone stale is a live
   * error in the configuration rather than a number that needs time to
   * climb -- and it is the failure that would make the other two lie. */
  const known = new Set(Object.keys(report.units));

  for (const [name, why] of Object.entries(FLOORS.exempt)) {
    assert.ok(known.has(name),
      `exempt lists "${name}", which is not a unit in the report. ` +
      `Either it was renamed or it no longer exists -- a stale excuse is how ` +
      `a floor stops meaning anything. Units present: ${[...known].join(', ')}`);

    assert.ok(typeof why === 'string' && why.length > 40,
      `exempt["${name}"] needs a reason, not a note. Say what cannot be ` +
      `covered and what covers it instead.`);
  }

  /* A FILE exemption is held to the same standard: the file exists, it says
   * why, and its unit is not already exempt as a whole (then it says nothing). */
  for (const [path, why] of Object.entries(FLOORS.exempt_files ?? {})) {
    assert.ok(existsSync(join(ROOT, path)),
      `exempt_files lists "${path}", which does not exist -- a stale excuse.`);
    assert.ok(typeof why === 'string' && why.length > 40,
      `exempt_files["${path}"] needs a reason, not a note.`);
    const unit = report.exemptFiles?.find((e) => e.path === path)?.unit;
    assert.ok(!unit || !FLOORS.exempt[unit],
      `"${path}" is exempt twice -- its unit "${unit}" is exempt already`);
  }

  for (const [name, floor] of Object.entries(FLOORS.units)) {
    assert.ok(known.has(name), `units lists "${name}", which is not in the report`);
    assert.ok(typeof floor === 'number' && floor >= 0 && floor <= 100,
      `units["${name}"] is ${floor}, which is not a percentage`);
    assert.ok(!FLOORS.exempt[name],
      `"${name}" is both exempt and given a floor -- one of them is a mistake`);
  }
});

test('no first-party file is invisible to the report', () => {
  assert.ok(report, `no report at ${REPORT}. ${HOW}`);

  /*
   * THE NUMBER'S ONE REAL WAY TO LIE, and it is not a small one.
   *
   * lcov records what was LOADED. A file no test ever reaches is not 0% in the
   * report -- it is absent from it, and every percentage is computed without
   * it. So the total goes UP when an untested file is added, which is the
   * exact opposite of what the figure is read as meaning.
   *
   * The first run of this suite reported 54.4% with 1,315 lines of
   * modules/trance-gate/ui_chain.js entirely invisible, and plugins/trance-gate
   * at 100% because only Wire.cpp was linked into a test binary.
   *
   * Exempt by unit, with a reason, like any other gap.
   */
  const offences = report.absent.files
    .filter((a) => !FLOORS.exempt[a.unit])
    .map((a) => `${a.path.padEnd(52)}  never loaded by any test  (${a.language})`);

  enforce(offences,
    `first-party files absent from every tracefile:\n  ${offences.join('\n  ')}\n\n` +
    'These are not counted anywhere above, so every percentage in the report ' +
    'is an upper bound while this list is non-empty. Either reach them from a ' +
    'test, or exempt their unit in tests/coverage.floors.json with a reason.');
});

test('the report describes this repository and not a stale one', () => {
  assert.ok(report, `no report at ${REPORT}. ${HOW}`);

  /* A report generated before a rename still parses, still totals, and is
   * entirely wrong. Every file it names must still be on disk. */
  const gone = report.files
    .map((f) => f.path)
    .filter((p) => !existsSync(join(ROOT, p)));

  assert.deepEqual(gone, [],
    `the report names files that no longer exist:\n  ${gone.join('\n  ')}\n\n` +
    `It is stale. ${HOW}`);
});

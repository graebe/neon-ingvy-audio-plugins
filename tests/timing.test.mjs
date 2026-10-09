// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The timing log every build and test script writes (scripts/timing.sh), and
 * its summary (scripts/build-timings.py).
 *
 * A log nobody can read is noise, and a helper that changes what it wraps is
 * worse than none: so this runs ni_time_stage the way the scripts do -- under
 * set -euo pipefail, against a build directory with a CMakeCache.txt and a
 * .ninja_log -- into a log of its own (NI_TIMING_LOG), and holds every record
 * to the fields docs/tech/testing.md promises, the command's exit status to
 * what it returned, and the summary to the records.
 *
 *   node --test tests/timing.test.mjs
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync, spawnSync } from 'node:child_process';
import { mkdtempSync, mkdirSync, readFileSync, readdirSync, writeFileSync, existsSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
const dir = mkdtempSync(join(tmpdir(), 'ni-timing-'));
const LOG = join(dir, 'logs', 'build-timings.jsonl');
const BUILD = join(dir, 'build');
mkdirSync(BUILD);
writeFileSync(join(BUILD, 'CMakeCache.txt'),
  'CMAKE_BUILD_TYPE:STRING=RelWithDebInfo\nCMAKE_GENERATOR:INTERNAL=Ninja\n' +
  'CMAKE_OSX_ARCHITECTURES:STRING=arm64;x86_64\n');
writeFileSync(join(BUILD, '.ninja_log'), '# ninja log v6\n0\t10\t0\tfoo.o\tdeadbeef\n');

/* Three stages from one script run: a build that passes, a stage that fails
 * with 3 under set -e without ending the script, and a stage whose name needs
 * escaping. The script's own exit status is the last stage's. */
const script = `
set -euo pipefail
. "${ROOT}/scripts/timing.sh"
ni_time_stage build --build "${BUILD}" --preset dev -- sleep 0.1
ni_time_stage ctest -- bash -c 'exit 3' || echo "ctest returned $?"
ni_time_stage 'a "quoted" stage' -- true
`;
const run = spawnSync('bash', ['-c', script], {
  cwd: ROOT, encoding: 'utf8',
  env: { ...process.env, NI_TIMING_LOG: LOG, NI_TIMING_RUN: '', NI_TIMING_SCRIPT: 'tests/timing.test.mjs' },
});

const records = () => readFileSync(LOG, 'utf8').trim().split('\n').map((l) => JSON.parse(l));

test('the wrapped commands ran as they would have, and their status came back', () => {
  assert.equal(run.status, 0, run.stderr);
  assert.match(run.stdout, /ctest returned 3/);
});

test('one valid JSON record per stage, with every field', () => {
  const rs = records();
  assert.equal(rs.length, 3);
  const FIELDS = ['time', 'run', 'sha', 'branch', 'worktree', 'script', 'stage', 'preset', 'build_type',
    'generator', 'archs', 'duration_s', 'exit_code', 'ccache_hits', 'ccache_misses', 'cpus', 'load1', 'host'];
  for (const r of rs) assert.deepEqual(Object.keys(r), FIELDS);

  const [build, ctest, quoted] = rs;
  assert.match(build.time, /^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$/);
  assert.equal(build.stage, 'build');
  assert.equal(build.script, 'tests/timing.test.mjs');
  assert.equal(build.preset, 'dev');
  assert.equal(build.build_type, 'RelWithDebInfo');
  assert.equal(build.generator, 'Ninja');
  assert.equal(build.archs, 'arm64;x86_64');
  assert.equal(build.exit_code, 0);
  assert.ok(build.duration_s >= 0.09 && build.duration_s < 10, `duration ${build.duration_s}`);
  assert.ok(Number.isInteger(build.cpus) && build.cpus > 0);
  assert.ok(build.load1 === null || typeof build.load1 === 'number');
  assert.ok(build.ccache_hits === null || Number.isInteger(build.ccache_hits));
  assert.match(build.sha ?? '', /^[0-9a-f]{12}$/);

  assert.equal(ctest.exit_code, 3);
  assert.equal(ctest.build_type, null, 'no build directory, no build type');
  assert.equal(quoted.stage, 'a "quoted" stage');
  assert.equal(new Set(rs.map((r) => r.run)).size, 1, 'one run id for the whole invocation');
});

test('a build stage leaves its .ninja_log beside the log', () => {
  const copies = readdirSync(join(dirname(LOG), 'ninja'));
  assert.equal(copies.length, 1);
  assert.match(copies[0], /^\d{8}T\d{6}Z-timing\.test\.mjs-build\.ninja_log$/);
});

const python = spawnSync('python3', ['--version']).status === 0;

test('build-timings.py summarises the log, and its filters filter', { skip: !python && 'no python3' }, () => {
  /* The summary is held to a log whose durations are FIXED. The real records
   * above are timed, and on a loaded machine a stage that merely starts a
   * process can outlast the short build -- so ranking the real log tested the
   * machine's load, not the summariser, and failed now and then. The records
   * are the real ones; only their durations are set. */
  const fixed = { 'build': 5, 'ctest': 1, 'a "quoted" stage': 0.5 };
  const RANKED = join(dir, 'logs', 'ranked.jsonl');
  writeFileSync(RANKED, records().map((r) =>
    JSON.stringify({ ...r, duration_s: fixed[r.stage] })).join('\n') + '\n');
  const summary = JSON.parse(execFileSync('python3',
    ['-I', join(ROOT, 'scripts', 'build-timings.py'), '--log', RANKED, '--json'], { encoding: 'utf8' }));
  assert.equal(summary.records, 3);
  assert.deepEqual(summary.per_stage.map((s) => s.stage), ['build', 'ctest', 'a "quoted" stage']);
  assert.equal(summary.per_stage[1].failed, 1);
  assert.equal(summary.per_day.length, 1);
  assert.equal(summary.slowest[0].stage, 'build');

  const one = JSON.parse(execFileSync('python3',
    ['-I', join(ROOT, 'scripts', 'build-timings.py'), '--log', LOG, '--json', '--stage', 'ctest',
     '--since', '2000-01-01'], { encoding: 'utf8' }));
  assert.equal(one.records, 1);
  const none = JSON.parse(execFileSync('python3',
    ['-I', join(ROOT, 'scripts', 'build-timings.py'), '--log', LOG, '--json', '--since', '2999-01-01'],
    { encoding: 'utf8' }));
  assert.equal(none.records, 0);
  assert.ok(existsSync(LOG));
});

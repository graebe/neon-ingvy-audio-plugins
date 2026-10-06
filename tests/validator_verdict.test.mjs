// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The validator verdict: strict both ways, over clap-validator's own output.
 *
 * The samples are clap-validator 0.4.1's --json shape, trimmed. The manifest is
 * checked too: every entry names a real bundle, a validator this script reads,
 * a cause, and a patch that exists -- or, for the validator's own bug, none.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import { parseResults, knownFor, verdict, report, VALIDATOR } from '../scripts/validator-verdict.mjs';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');

const run = (...rows) => JSON.stringify({
  results: rows.map(([kind, name, code, details = null]) => ({
    test: { [kind]: { test: name, path: 'build/out/NIX.clap', plugin_id: 'com.graebe.NIX' } },
    status: { code, details },
    duration: { secs: 0, nanos: 1 },
  })),
});

const manifest = {
  known: [
    { bundle: 'NIX', validator: VALIDATOR, test: 'state-reproducibility-basic', cause: 'x', fixed_by: 'p' },
    { bundle: 'NIX', validator: VALIDATOR, test: 'param-conversions', cause: 'x', fixed_by: null },
    { bundle: 'NIY', validator: VALIDATOR, test: 'scan-time', cause: 'x', fixed_by: 'p' },
    { bundle: 'NIX', validator: 'pluginval', test: 'scan-time', cause: 'x', fixed_by: 'p' },
  ],
};
const judge = (text) => verdict(parseResults(text), knownFor(manifest, 'NIX'));

test('a run that fails exactly the known tests matches', () => {
  const v = judge(run(
    ['plugin-library', 'scan-time', 'success'],
    ['plugin-instance', 'state-reproducibility-basic', 'failed', 'values changed'],
    ['plugin-instance', 'param-conversions', 'crashed', 'attempt to divide by zero'],
    ['plugin-instance', 'process-audio-basic', 'skipped'],
  ));
  assert.equal(v.ok, true);
  assert.deepEqual(v.expected.map((r) => r.test), ['state-reproducibility-basic', 'param-conversions']);
  assert.match(report('NIX', v), /matches the known-failures manifest \(2 known\)/);
});

test('a failure the manifest does not list is a regression', () => {
  const v = judge(run(
    ['plugin-instance', 'state-reproducibility-basic', 'failed'],
    ['plugin-instance', 'param-conversions', 'failed'],
    ['plugin-instance', 'process-audio-out-of-place', 'failed', 'NaN in the output\nmore'],
  ));
  assert.equal(v.ok, false);
  assert.deepEqual(v.unexpected.map((r) => r.test), ['process-audio-out-of-place']);
  assert.match(report('NIX', v), /NEW {5}NIX process-audio-out-of-place \(failed\): NaN in the output$/m);
});

test('a known failure that now passes fails too, so the list must shrink', () => {
  const v = judge(run(
    ['plugin-instance', 'state-reproducibility-basic', 'success'],
    ['plugin-instance', 'param-conversions', 'failed'],
  ));
  assert.equal(v.ok, false);
  assert.deepEqual(v.fixed.map((e) => [e.test, e.now]), [['state-reproducibility-basic', 'success']]);
  assert.match(report('NIX', v), /FIXED .* now success -- remove it/);
});

test('a known test that was skipped or not run at all is not "still failing"', () => {
  const v = judge(run(['plugin-instance', 'state-reproducibility-basic', 'skipped']));
  assert.deepEqual(v.fixed.map((e) => [e.test, e.now]),
    [['state-reproducibility-basic', 'skipped'], ['param-conversions', 'not run']]);
  assert.equal(v.ok, false);
});

test('warnings are reported and never fail anything', () => {
  const v = judge(run(
    ['plugin-instance', 'state-reproducibility-basic', 'failed'],
    ['plugin-instance', 'param-conversions', 'failed'],
    ['plugin-instance', 'features-categories', 'warning', 'no category'],
  ));
  assert.equal(v.ok, true);
  assert.match(report('NIX', v), /warning NIX features-categories: no category/);
});

test('entries are per bundle and per validator', () => {
  const k = knownFor(manifest, 'NIX');
  assert.deepEqual([...k.keys()], ['state-reproducibility-basic', 'param-conversions']);
});

test('output that is not clap-validator JSON is refused, not read as a pass', () => {
  assert.throws(() => parseResults('{}'), /no "results" array/);
  assert.throws(() => parseResults('not json'));
  assert.throws(() => parseResults(JSON.stringify({ results: [{ status: { code: 'failed' } }] })),
    /without a test id/);
});

test('the real manifest is well formed, and every patch it cites exists', () => {
  const real = JSON.parse(readFileSync(join(ROOT, 'tests', 'validators.known.json'), 'utf8'));
  const bundles = new Set(['NITranceGate', 'NISideChain', 'NIListenIn', 'NISpectrogram']);
  const seen = new Set();
  for (const e of real.known) {
    const id = `${e.bundle}/${e.validator}/${e.test}`;
    assert.ok(!seen.has(id), `${id} is listed twice`);
    seen.add(id);
    assert.ok(bundles.has(e.bundle), `${id}: no such bundle`);
    assert.equal(e.validator, VALIDATOR, `${id}: only clap-validator's output is read`);
    assert.ok(typeof e.cause === 'string' && e.cause.length > 20, `${id}: say what causes it`);
    if (e.fixed_by === null) {
      assert.match(e.cause, /^clap-validator/, `${id}: with no patch, it must be the validator's own bug`);
    } else {
      assert.ok(existsSync(join(ROOT, e.fixed_by)), `${id}: ${e.fixed_by} does not exist`);
    }
  }
});

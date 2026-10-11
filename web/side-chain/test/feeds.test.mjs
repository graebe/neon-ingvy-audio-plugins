// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The three feeds, read -- first as the format sc-core's get_param documents
 * it, then as the engine actually writes it. The second half is the one that
 * matters: a positional format read one field off is a playhead drawn from the
 * fire count, and only the engine's own output can say which field is which.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { parseUi, parseParams, parseStageMs } from '../src/lib/feeds.js';
import { P, NUM_PARAMS } from '../src/lib/msg.js';
import { NO_WASM, realEngine } from './support/engine.mjs';

test('ui: eleven positional fields and the engine\'s clock, or nothing', () => {
  const u = parseUi('1:4:500.000:0.250000:1:7:0.5000:0.00000:0:2:0.125000:1234.500');
  assert.deepEqual(u, {
    source: 1, rate: 4, msCycle: 500, sweep: 0.25, advancing: 1, fires: 7,
    duck: 0.5, key: 0, connected: 0, stage: 2, phase: 0.125, devms: 1234.5,
  });
  /* An engine older than the clock: no devms, and the playhead is carried
   * from arrival as before. */
  assert.equal(parseUi('1:4:500.000:0.250000:1:7:0.5000:0.00000:0:2:0.125000').devms, null);
  assert.equal(parseUi('1:4:500'), null, 'a short ui is dropped whole');
  assert.equal(parseUi('1:4:x:0:1:7:0:0:0:2:0'), null);
  assert.equal(parseUi(undefined), null);
});

test('params: at least the Move\'s thirteen, all numbers', () => {
  const f = parseParams('0:4:0:0:2:8:35:1:1:1:36:0:0:-60:0');
  assert.equal(f.length, 15);
  assert.equal(f[P.release], 35);
  assert.equal(f[P.note], 36);
  assert.equal(parseParams('0:4:0'), null);
  assert.equal(parseParams(`${'1:'.repeat(NUM_PARAMS - 1)}nope`), null);
});

test('stage_ms: four lengths, or nothing', () => {
  assert.deepEqual(parseStageMs('0.000:10.000:40.000:175.000'), [0, 10, 40, 175]);
  assert.equal(parseStageMs('1:2:3'), null);
});

test('the engine\'s own feeds read as the editor expects', { skip: NO_WASM }, async () => {
  const e = await realEngine();
  e.set('attack', '10');
  e.set('release', '50');
  e.set('depth', '0.75');
  e.set('trigger_note', '38');
  e.advance(300);

  const params = parseParams(e.get('params'));
  assert.ok(params, `params did not parse: ${e.get('params')}`);
  assert.equal(params[P.attack], 10);
  assert.equal(params[P.release], 50);
  assert.equal(params[P.depth], 0.75);
  assert.equal(params[P.note], 38);
  assert.equal(params[P.source], 0);

  const ui = parseUi(e.get('ui'));
  assert.ok(ui, `ui did not parse: ${e.get('ui')}`);
  /* 120 bpm at the default 1/4: one beat is a cycle of 500 ms. */
  assert.equal(ui.rate, 4);
  assert.ok(Math.abs(ui.msCycle - 500) < 1e-6, `ms_cycle ${ui.msCycle}`);
  assert.equal(ui.advancing, 1);
  assert.ok(ui.sweep >= 0 && ui.sweep <= 1, `sweep ${ui.sweep}`);
  assert.ok(ui.fires >= 1, 'the cycle has fired');
  assert.ok(ui.stage >= 0 && ui.stage <= 4);

  const ms = parseStageMs(e.get('stage_ms'));
  /* Each stage's ms is its percentage of the 500 ms cycle. */
  assert.deepEqual(ms, [0, 50, 40, 250]);
  e.destroy();
});

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The fake Move behaves as Schwung's manager does: feeds first, every declared
 * value on subscribe, a write echoed and truncated, the feeds re-read after a
 * write and on a heartbeat, and only what moved sent.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createFakeSchwung, feedsOf } from '../src/lib/fake-schwung.js';

const CHAIN = [
  { key: 'depth', type: 'float', min: 0, max: 1 },
  { key: 'ui', type: 'string', access: 'read' },
  { key: 'shape', type: 'canvas', extra_keys: ['ui', 'rate'] },
  { key: 'web_ui', type: 'string', access: 'read', extra_keys: ['ui', 'params', 'state'] },
];

function rig() {
  let tick = null;
  const timers = { setInterval: (fn) => { tick = fn; return 1; }, clearInterval: () => { tick = null; } };
  const values = { depth: '1', ui: 'u0', params: 'p0', web_ui: '1', rate: '4' };
  let clock = 0;
  const engine = {
    set: (k, v) => { values[k] = v; values.params = `p:${k}=${v}`; },
    get: (k) => (k === 'state' ? null : values[k] ?? null),
    advance: (ms) => { clock += ms; values.ui = `u${Math.floor(clock / 100)}`; },
  };
  const fake = createFakeSchwung({ chainParams: CHAIN, engine, timers });
  const seen = [];
  const run = (ms) => { for (let t = 0; t < ms; t += 20) tick?.(); };
  return { fake, seen, run, values };
}

test('the feeds are the extras that are not declared params', () => {
  assert.deepEqual(feedsOf(CHAIN), ['rate', 'params', 'state']);
});

test('subscribing pushes the feeds, then every declared value', async () => {
  const { fake, seen } = rig();
  fake.bridge.onParamChange((p) => seen.push(p));
  assert.deepEqual(seen, [
    { 'fx1:rate': '4', 'fx1:params': 'p0' },
    { 'fx1:depth': '1', 'fx1:ui': 'u0', 'fx1:web_ui': '1' },
  ]);
  assert.equal(await fake.bridge.getParam('fx1:depth'), '1');
  assert.equal(await fake.bridge.getParam('fx2:depth'), null);
  assert.equal((await fake.bridge.getChainParams()).length, 4);
  assert.equal(await fake.bridge.getHierarchy(), null);
  fake.stop();
});

test('a write is echoed, truncated, and the feeds follow within 150 ms', () => {
  const { fake, seen, run } = rig();
  fake.bridge.onParamChange((p) => seen.push(p));
  seen.length = 0;
  fake.bridge.setParam('fx1:depth', '0.5');
  fake.bridge.setParam('fx2:depth', '0.1');
  fake.device('depth', 'x'.repeat(300));
  assert.deepEqual(seen.map((p) => Object.keys(p)), [['fx1:depth'], ['fx1:depth']]);
  assert.equal(seen[1]['fx1:depth'].length, 255);
  seen.length = 0;
  run(160);
  assert.deepEqual(seen, [{ 'fx1:params': `p:depth=${'x'.repeat(300)}` }], 'params moved; rate did not');
  fake.stop();
});

test('a value the engine changes by itself arrives only on the heartbeat', () => {
  const { fake, seen, run, values } = rig();
  fake.bridge.onParamChange((p) => seen.push(p));
  seen.length = 0;
  values.rate = '5';
  run(480);
  assert.deepEqual(seen, []);
  run(40);
  assert.deepEqual(seen, [{ 'fx1:rate': '5' }]);
  fake.stop();
});

test('nothing is pushed before a page subscribes', () => {
  const { fake, seen, run } = rig();
  fake.device('depth', '0.2');
  run(1000);
  assert.deepEqual(seen, []);
  fake.stop();
  run(100);
});

test('with a latency, every push arrives that much later, in its own time', () => {
  const later = [];
  const timers = {
    setInterval: () => 1, clearInterval: () => {},
    setTimeout: (fn, ms) => later.push([ms, fn]),
  };
  const values = { depth: '1', web_ui: '1', rate: '4' };
  const engine = { set: (k, v) => { values[k] = v; }, get: (k) => values[k] ?? null, advance: () => {} };
  let n = 0;
  const fake = createFakeSchwung({ chainParams: CHAIN, engine, timers, latency: () => [300, 40][n++ % 2] });
  const seen = [];
  fake.bridge.onParamChange((p) => seen.push(p));
  assert.deepEqual(seen, [], 'nothing arrives at once');
  /* The feeds went out first, 300 ms late; the values second, 40 ms late --
   * and so arrive first, as two pushes do on a real network. */
  assert.deepEqual(later.map(([ms]) => ms), [300, 40]);
  later.sort((a, b) => a[0] - b[0]).forEach(([, fn]) => fn());
  assert.deepEqual(seen, [{ 'fx1:depth': '1', 'fx1:web_ui': '1' }, { 'fx1:rate': '4' }]);
  fake.stop();
});

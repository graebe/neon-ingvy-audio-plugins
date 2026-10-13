// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The transport to a Move, against a bridge that behaves as
 * /static/schwung-remote-api.js does in an iframe: values pushed as
 * '<component>:<key>' strings, chain_params on request, writes as setParam.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createSchwungTransport } from '../src/lib/schwung.js';

const DECLARED = [
  { key: 'source', type: 'enum', options: ['Cycle', 'MIDI'], wire_format: 'index', default: '0' },
  { key: 'depth', type: 'float', min: 0, max: 1, default: 1, step: 0.01, unit: '%' },
  { key: 'shape', type: 'canvas' },
];

function fakeBridge({ component = 'fx1', chain = DECLARED, nullFirst = 0 } = {}) {
  const b = { component, sent: [], listener: null, asked: 0 };
  b.onParamChange = (fn) => { b.listener = fn; };
  b.setParam = (k, v) => b.sent.push([k, v]);
  b.getChainParams = async () => (b.asked++ < nullFirst ? null : chain);
  b.push = (params) => b.listener(params);
  return b;
}

function inboundLog() {
  const log = [];
  return {
    log,
    param: (i, v) => log.push(['param', i, v]),
    message: (t, s) => log.push(['msg', t, s]),
    bytes: () => {},
  };
}

const opts = (bridge, extra = {}) => ({
  bridge,
  params: ['source', 'depth', null],
  feeds: { ui: 64, params: 65 },
  defaultsTag: 113,
  setTextTag: 121,
  wait: async () => {},
  ...extra,
});

test('values and feeds arrive under the page\'s component only', async () => {
  const b = fakeBridge();
  const t = createSchwungTransport(opts(b));
  const i = inboundLog();
  assert.equal(await t.attach(i), true);
  assert.deepEqual(i.log, [['msg', 113, '0:1:']], 'the declared defaults, normalised');
  i.log.length = 0;
  b.push({ 'fx1:depth': '0.5', 'fx2:depth': '0.1', 'fx1:ui': '0:4:500', 'fx1:web_ui': '1', 'fx1:other': '3' });
  assert.deepEqual(i.log, [['param', 1, 0.5], ['msg', 1, '50 %'], ['msg', 64, '0:4:500']]);
});

test('what arrives before chain_params is applied once it does', async () => {
  const b = fakeBridge({ nullFirst: 2 });
  const t = createSchwungTransport(opts(b));
  const i = inboundLog();
  b.onParamChange = (fn) => { b.listener = fn; fn({ 'fx1:source': '1' }); };
  assert.equal(await t.attach(i), true);
  assert.equal(b.asked, 3, 'asked again while it was null');
  assert.deepEqual(i.log.slice(1), [['param', 0, 1], ['msg', 0, 'MIDI']]);
});

test('a page whose chain_params never comes stays quiet', async () => {
  const b = fakeBridge({ chain: [] });
  const t = createSchwungTransport(opts(b));
  const i = inboundLog();
  assert.equal(await t.attach(i), false);
  b.push({ 'fx1:depth': '0.5' });
  t.setParam(1, 0.5);
  assert.deepEqual(i.log, []);
  assert.deepEqual(b.sent, []);
});

test('writes go out as the module\'s wire values, typed text parsed', async () => {
  const b = fakeBridge({ component: 'master_fx:fx2' });
  const seen = [];
  const t = createSchwungTransport(opts(b, { messages: { 96: (text, write) => { seen.push(text); write('cursor', 3); write('step', 'On'); } } }));
  await t.attach(inboundLog());
  t.beginGesture(1);
  t.setParam(1, 0.25);
  t.endGesture(1);
  t.setParam(0, 1);
  t.setParam(2, 1);              /* no Move parameter behind it */
  t.sendMessage(121, '1:80');
  t.sendMessage(121, '1:junk');
  t.sendMessage(96, '3:1');
  t.sendMessage(99, 'x');        /* a message nothing handles */
  assert.deepEqual(b.sent, [
    ['master_fx:fx2:depth', '0.25'], ['master_fx:fx2:source', '1'], ['master_fx:fx2:depth', '0.8'],
    ['master_fx:fx2:cursor', '3'], ['master_fx:fx2:step', 'On'],
  ]);
  assert.deepEqual(seen, ['3:1']);
});

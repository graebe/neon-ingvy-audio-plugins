// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The whole path in node: the editor's transport (the kit's, with this
 * module's table) against the kit's fake Move, with the real engine behind
 * it. What the editor is told, after what the Move did.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createFakeSchwung } from '@ultraviolet/ui/fake-schwung';
import { createSchwungTransport } from '@ultraviolet/ui/schwung';
import { wasmEngine } from '../src/lib/wasm-engine.js';
import { KEYS, FEEDS, MESSAGES, expandParams } from '../src/lib/move.js';
import { MSG, P } from '../src/lib/msg.js';
import { decodeUi } from '../src/lib/readouts.js';
import { loadEngine, chainParams } from './support/engine.mjs';

const capi = await loadEngine();
/* The readout carries the engine's f32 at %.9g: 1.6 reads 1.60000002. */
const close = (a, b, msg) => assert.ok(Math.abs(a - b) < 1e-6, `${msg ?? ''} ${a} != ${b}`);

async function rig() {
  let tick = null;
  const timers = { setInterval: (fn) => { tick = fn; return 1; }, clearInterval: () => { tick = null; } };
  const engine = wasmEngine(capi);
  const fake = createFakeSchwung({ chainParams: chainParams(), engine, timers });
  const sent = [];
  const bridge = { ...fake.bridge, setParam: (k, v) => { sent.push([k, v]); fake.bridge.setParam(k, v); } };
  const t = createSchwungTransport({
    bridge: expandParams(bridge), params: KEYS, feeds: FEEDS, messages: MESSAGES,
    defaultsTag: MSG.defaults, setTextTag: MSG.setText, wait: async () => {},
  });
  const values = new Map();
  const texts = new Map();
  const feeds = new Map();
  await t.attach({
    param: (i, v) => values.set(i, v),
    message: (tag, text) => (tag < KEYS.length ? texts.set(tag, text) : feeds.set(tag, text)),
    bytes: () => {},
  });
  const run = (ms) => { for (let x = 0; x < ms; x += 20) tick?.(); };
  return { fake, engine, t, values, texts, feeds, sent, run };
}

test('a slot recall on the Move brings every value of the new slot', async () => {
  const r = await rig();
  /* Slot 4 (index 3) gets its own sound, from the Move's side. */
  r.fake.device('slot', '3');
  r.fake.device('attack', '50');
  r.fake.device('rate', '1/8');
  r.fake.device('slot', '0');
  r.run(200);
  assert.equal(r.texts.get(P.rate), '1/16', 'slot 1 again');
  close(r.values.get(P.attack), 1.6 / 200);

  /* The recall: one write, `slot`, and nobody writing the rest. */
  r.fake.device('slot', '3');
  r.run(200);
  close(r.values.get(P.slot), 3 / 7);
  assert.equal(r.texts.get(P.rate), '1/8', 'the Rate came with the slot');
  close(r.values.get(P.attack), 50 / 200, 'and the Attack');
  r.fake.stop();
});

test('a step clicked in the editor reaches the engine and comes back in `ui`', async () => {
  const r = await rig();
  r.t.sendMessage(MSG.setStep, '1:1');
  r.t.sendMessage(MSG.setStep, '2:2');
  r.run(200);
  assert.deepEqual(r.sent.slice(-4), [['fx1:cursor', '1'], ['fx1:step', 'On'],
                                      ['fx1:cursor', '2'], ['fx1:step', 'Tie']]);
  const u = decodeUi(r.feeds.get(MSG.uiState));
  assert.equal(u.steps[1], true);
  assert.equal(u.ties[2], true);
  assert.equal(u.cursor, 2);
  r.fake.stop();
});

test('a knob\'s write is the module\'s wire value, and the feed agrees', async () => {
  const r = await rig();
  r.t.setParam(P.amount, 0.25);
  r.t.setParam(P.length, 31 / 127);
  r.run(200);
  assert.deepEqual(r.sent.slice(-2), [['fx1:amount', '0.25'], ['fx1:length', '31']]);
  assert.equal(decodeUi(r.feeds.get(MSG.uiState)).length, 32);
  r.fake.stop();
});

test('the patch arrives as a feed, for the plots', async () => {
  const r = await rig();
  const state = r.feeds.get(MSG.state);
  assert.ok(state && state.startsWith('{'), 'the state readout is the patch');
  r.fake.stop();
});

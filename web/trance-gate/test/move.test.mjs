// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The editor's table to the Move (src/lib/move.js), against the module's own
 * declaration: every key it names is one the device serves, every value it
 * writes is one the module reads, and the editor's normalisation is the one
 * the declaration gives.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { paramOf } from '@ultraviolet/ui/chain';
import { feedsOf } from '@ultraviolet/ui/fake-schwung';
import { KEYS, FEEDS, MESSAGES, STEP_MODES, PARAMS_FIELDS, paramsWires, expandParams } from '../src/lib/move.js';
import { MSG, P, NUM_PARAMS, lengthNorm, MAX_LENGTH } from '../src/lib/msg.js';
import { STAGE_MAX_PCT } from '../src/lib/stages.js';
import { chainParams } from './support/engine.mjs';

const CHAIN = chainParams();
const entry = (key) => CHAIN.find((e) => e.key === key);

test('every parameter the editor has is a declared value on the Move', () => {
  assert.equal(KEYS.length, NUM_PARAMS);
  for (const [i, key] of KEYS.entries()) {
    assert.ok(key, `parameter ${i} has no key`);
    const e = entry(key);
    assert.ok(e, `${key} is not in tg-move's CHAIN_PARAMS`);
    assert.ok(paramOf(e), `${key} is declared, but not as a value`);
  }
  assert.equal(new Set(KEYS).size, KEYS.length, 'two parameters on one key');
});

test('the feeds are the web_ui declaration\'s extra keys, and nothing else', () => {
  assert.deepEqual(Object.keys(FEEDS).sort(), feedsOf(CHAIN).sort());
  assert.deepEqual(entry('web_ui').extra_keys, ['ui', 'params', 'state']);
  assert.equal(CHAIN.at(-1).key, 'web_ui', 'the editor\'s feed is the last entry');
});

test('the editor\'s normalisation is the declaration\'s', () => {
  const norm = (k, wire) => paramOf(entry(k)).norm(wire);
  /* Length: the 128-option enum, option i a length of i + 1. */
  for (const steps of [1, 16, 32, 128]) {
    assert.equal(norm('length', String(steps - 1)), lengthNorm(steps));
  }
  assert.equal(entry('length').options.length, MAX_LENGTH);
  /* The selects and switches: an index over their options. */
  assert.equal(norm('slot', '7'), 1);
  assert.equal(norm('curve', '2'), 1);
  assert.equal(norm('fade_dir', '1'), 1);
  assert.equal(norm('time_mode', '1'), 1, 'Time in % is the switch on');
  assert.equal(norm('rate', '1/16'), 7 / 12);
  /* The stages: a percentage of the gate, 0..STAGE_MAX_PCT. */
  for (const k of ['attack', 'decay', 'release']) {
    assert.equal(entry(k).min, 0);
    assert.equal(entry(k).max, STAGE_MAX_PCT, `${k}'s range is the engine's`);
  }
  /* Amount, Sustain and Fade are 0..1, so normalised IS real. */
  for (const k of ['amount', 'sustain', 'fade']) assert.equal(norm(k, '0.25'), 0.25);
});

test('a step edit is the cursor, then the value, in the module\'s words', () => {
  const sent = [];
  const write = (k, v) => sent.push([k, String(v)]);
  MESSAGES[MSG.setStep]('5:1', write);
  MESSAGES[MSG.setStep]('6:2', write);
  MESSAGES[MSG.setStep]('7:0', write);
  MESSAGES[MSG.setDepth]('5:0.5', write);
  MESSAGES[MSG.setOrder]('5:3', write);
  assert.deepEqual(sent, [
    ['cursor', '5'], ['step', 'On'],
    ['cursor', '6'], ['step', 'Tie'],
    ['cursor', '7'], ['step', 'Off'],
    ['cursor', '5'], ['step_amount', '0.5000'],
    ['cursor', '5'], ['step_order', '3'],
  ]);
  /* Every value written is one the declaration offers. */
  assert.deepEqual(entry('step').options, STEP_MODES);
  assert.ok(paramOf(entry('cursor')).norm('5') !== undefined);
});

test('a roll is Roll and then Hold, both labels the Random knob has', () => {
  const sent = [];
  MESSAGES[MSG.randomize]('', (k, v) => sent.push([k, v]));
  assert.deepEqual(sent, [['randomize', 'Roll'], ['randomize', 'Hold']]);
  assert.deepEqual(entry('randomize').options, ['Hold', 'Roll']);
});

test('a malformed message writes nothing', () => {
  const sent = [];
  const write = (k, v) => sent.push([k, v]);
  MESSAGES[MSG.setStep]('x:1', write);
  MESSAGES[MSG.setStep]('3:9', write);
  MESSAGES[MSG.setStep]('3', write);
  MESSAGES[MSG.setDepth]('3:loud', write);
  MESSAGES[MSG.setOrder]('3:0', write);
  MESSAGES[MSG.setOrder]('-1:2', write);
  assert.deepEqual(sent, []);
});

test('an amount is clamped to the step\'s range before it goes', () => {
  const sent = [];
  MESSAGES[MSG.setDepth]('0:1.7', (k, v) => sent.push([k, v]));
  assert.deepEqual(sent.at(-1), ['step_amount', '1.0000']);
});

/* What the engine writes for `params`, as tg-core's get_param formats it. */
const READOUT = '2:1:0:1:1/8T:31:0.899999976:0.5:1.60000002:16:1:16:93.75:0.5:1:0:16,32';

test('the params readout is the values its keys would echo', () => {
  assert.equal(PARAMS_FIELDS.length, 16);
  assert.deepEqual(paramsWires(READOUT), {
    slot: '2', legato: '1', time_mode: '0', curve: '1', rate: '1/8T', length: '31',
    amount: '0.899999976', hold: '0.5', attack: '1.60000002', decay: '16', sustain: '1',
    release: '16', fade: '0.5', fade_soft: '1', fade_dir: '0',
  });
  assert.equal(paramsWires('1:2:3'), null);
  /* Every field the table names is a key the editor binds. */
  for (const k of PARAMS_FIELDS.filter(Boolean)) assert.ok(KEYS.includes(k), k);
});

test('the sound\'s values come from the readout, and its echoes are dropped', () => {
  let push = null;
  const out = [];
  const bridge = { component: 'fx2', onParamChange: (fn) => { push = fn; },
                   setParam: (k, v) => out.push([k, v]) };
  const seen = [];
  const b = expandParams(bridge);
  b.onParamChange((c) => seen.push(c));

  push({ 'fx2:params': READOUT, 'fx2:ui': 'u' });
  assert.equal(seen[0]['fx2:attack'], '1.60000002');
  assert.equal(seen[0]['fx2:slot'], '2');
  assert.equal(seen[0]['fx2:ui'], 'u', 'the rest of the push is untouched');

  /* A heartbeat that repeats the readout says nothing new. */
  push({ 'fx2:params': READOUT });
  assert.deepEqual(Object.keys(seen[1]), ['fx2:params']);

  /* A recall moves the sound: the fields that moved are values. */
  push({ 'fx2:params': READOUT.replace(/^2:/, '3:').replace(':16:1:16:', ':40:1:16:') });
  assert.deepEqual(Object.keys(seen[2]).sort(), ['fx2:decay', 'fx2:params', 'fx2:slot']);

  /* An echo of one of the fifteen says nothing; any other key passes. */
  push({ 'fx2:rate': '1/2', 'fx2:cursor': '4' });
  assert.deepEqual(seen[3], { 'fx2:cursor': '4' });
  push({ 'fx2:rate': '1/2' });
  assert.equal(seen.length, 4, 'an echo alone is no push at all');
});

/* A Master FX position gets no feeds: the declared values are all there is. */
test('without a readout the echoes are the values, until the first readout', () => {
  let push;
  const bridge = { component: 'master_fx:fx1', onParamChange: (fn) => { push = fn; },
                   setParam: () => {} };
  const seen = [];
  expandParams(bridge).onParamChange((c) => seen.push(c));
  push({ 'master_fx:fx1:amount': '0.70', 'master_fx:fx1:rate': '1/8' });
  assert.deepEqual(seen[0], { 'master_fx:fx1:amount': '0.70', 'master_fx:fx1:rate': '1/8' });
  push({ 'master_fx:fx1:params': READOUT.replace(/^2:/, '0:') });
  push({ 'master_fx:fx1:amount': '0.20' });
  assert.equal(seen.length, 2, 'once there is a readout, an echo of the fifteen says nothing');
});

test('after the page writes a key, the next readout says where the engine put it', () => {
  let push = null;
  const out = [];
  const bridge = { component: 'fx1', onParamChange: (fn) => { push = fn; },
                   setParam: (k, v) => out.push([k, v]) };
  const seen = [];
  const b = expandParams(bridge);
  b.onParamChange((c) => seen.push(c));
  push({ 'fx1:params': READOUT });
  /* The page sets the Rate; before the next readout the Move recalls a slot
   * whose Rate is the one the last readout had. */
  b.setParam('fx1:rate', '1/2');
  assert.deepEqual(out, [['fx1:rate', '1/2']], 'the write goes out unchanged');
  push({ 'fx1:params': READOUT });
  assert.equal(seen[1]['fx1:rate'], '1/8T', 'the engine\'s Rate, though the readout did not move');
  /* Once: the readout after that is a heartbeat again. */
  push({ 'fx1:params': READOUT });
  assert.equal(seen[2]['fx1:rate'], undefined);
});

test('the editor\'s parameter order is the engine\'s', () => {
  /* tg-core's Param enum: Slot, Length, Rate, Legato, TimeMode, Curve, Amount,
   * Hold, Attack, Decay, Sustain, Release, Fade, FadeSoft, FadeDir. */
  assert.deepEqual(KEYS, ['slot', 'length', 'rate', 'legato', 'time_mode', 'curve', 'amount',
                          'hold', 'attack', 'decay', 'sustain', 'release',
                          'fade', 'fade_soft', 'fade_dir']);
  assert.equal(P.width, KEYS.indexOf('hold'));
});

/*
 * The parameter store: the plugin's defaults, per-parameter values, and the
 * reset that sets the default rather than zero.
 * Copyright (c) 2026 Torben Gräber. MIT.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createParams, parseDefaults } from '../src/lib/params.js';
import { SHELL_MSG } from '../src/lib/shell.js';

const b64 = (s) => btoa(String.fromCharCode(...new TextEncoder().encode(s)));
const sent = [];
globalThis.IPlugSendMsg = (m) => sent.push(m);

test('the defaults message is read field by field', () => {
  assert.deepEqual(parseDefaults('0:0.5:1', 3), [0, 0.5, 1]);
  assert.deepEqual(parseDefaults('0.25:x::2', 5), [0.25, undefined, undefined, undefined, undefined]);
  assert.deepEqual(parseDefaults('', 2), [undefined, undefined]);
});

test('a reset sets the plugin\'s default, not zero and not the first value seen', () => {
  const p = createParams(3);
  /* The session's value arrives first -- which Side-Chain used to take as the default. */
  globalThis.SPVFD(1, 0.9);
  globalThis.SAMFD(SHELL_MSG.defaults, 0, b64('0:0.008:1'));
  sent.length = 0;
  p.reset(1);
  p.dispose();
  const writes = sent.filter((m) => m.msg === 'SPVFUI');
  assert.deepEqual(writes, [{ msg: 'SPVFUI', paramIdx: 1, value: 0.008 }]);
  /* bracketed as one gesture, so the host records one undo step */
  assert.deepEqual(sent.map((m) => m.msg), ['BPCFUI', 'SPVFUI', 'EPCFUI']);
});

test('a reset before the defaults have arrived does nothing', () => {
  const p = createParams(2);
  sent.length = 0;
  p.reset(0);
  p.dispose();
  assert.deepEqual(sent, []);
});

test('values, display strings and defaults are held per parameter', () => {
  const p = createParams(2);
  globalThis.SPVFD(0, 0.3);
  globalThis.SAMFD(1, 0, b64('12.5 ms'));
  globalThis.SAMFD(SHELL_MSG.defaults, 0, b64('0.1:0.2'));
  assert.equal(p.value(0), 0.3);
  assert.equal(p.text(1), '12.5 ms');
  assert.equal(p.defaultOf(1), 0.2);
  /* out of range reads as nothing rather than throwing */
  assert.equal(p.value(9), 0);
  p.dispose();
});

test('an echo during a drag does not move the stored value back', () => {
  const p = createParams(1);
  p.begin(0);
  p.input(0, 0.6);
  globalThis.SPVFD(0, 0.55);
  assert.equal(p.value(0), 0.6);
  p.end(0);
  globalThis.SPVFD(0, 0.6);
  assert.equal(p.value(0), 0.6);
  p.dispose();
});

test('typed text goes to the plugin to be parsed', () => {
  const p = createParams(4);
  sent.length = 0;
  p.sendText(3, '40 ms');
  p.dispose();
  assert.equal(sent[0].msgTag, SHELL_MSG.setText);
  assert.equal(atob(sent[0].data), '3:40 ms');
});

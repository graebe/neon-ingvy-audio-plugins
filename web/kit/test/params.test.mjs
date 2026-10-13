// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The parameter store: the host's defaults, per-parameter values, and the
 * reset that sets the default rather than zero.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createParams, parseDefaults } from '../src/lib/params.js';
import { SHELL_MSG } from '../src/lib/shell.js';

import { recorder } from './support/transport.mjs';

test('the defaults message is read field by field', () => {
  assert.deepEqual(parseDefaults('0:0.5:1', 3), [0, 0.5, 1]);
  assert.deepEqual(parseDefaults('0.25:x::2', 5), [0.25, undefined, undefined, undefined, undefined]);
  assert.deepEqual(parseDefaults('', 2), [undefined, undefined]);
});

test('a reset sets the plugin\'s default, not zero and not the first value seen', () => {
  const t = recorder();
  const p = createParams(3);
  /* The session's value arrives first -- which Side-Chain used to take as the default. */
  t.in.param(1, 0.9);
  t.in.message(SHELL_MSG.defaults, '0:0.008:1');
  t.sent.length = 0;
  p.reset(1);
  p.dispose();
  /* bracketed as one gesture, so a host that records them records one step */
  assert.deepEqual(t.sent, [['begin', 1], ['set', 1, 0.008], ['end', 1]]);
});

test('a reset before the defaults have arrived does nothing', () => {
  const t = recorder();
  const p = createParams(2);
  p.reset(0);
  p.dispose();
  assert.deepEqual(t.sent, []);
});

test('values, display strings and defaults are held per parameter', () => {
  const t = recorder();
  const p = createParams(2);
  t.in.param(0, 0.3);
  t.in.message(1, '12.5 ms');
  t.in.message(SHELL_MSG.defaults, '0.1:0.2');
  assert.equal(p.value(0), 0.3);
  assert.equal(p.text(1), '12.5 ms');
  assert.equal(p.defaultOf(1), 0.2);
  /* out of range reads as nothing rather than throwing */
  assert.equal(p.value(9), 0);
  p.dispose();
});

test('an echo during a drag does not move the stored value back', () => {
  const t = recorder();
  const p = createParams(1);
  p.begin(0);
  p.input(0, 0.6);
  t.in.param(0, 0.55);
  assert.equal(p.value(0), 0.6);
  p.end(0);
  t.in.param(0, 0.6);
  assert.equal(p.value(0), 0.6);
  p.dispose();
});

test('typed text goes to the host to be parsed', () => {
  const t = recorder();
  const p = createParams(4);
  p.sendText(3, '40 ms');
  p.dispose();
  assert.deepEqual(t.sent, [['msg', SHELL_MSG.setText, '3:40 ms']]);
});

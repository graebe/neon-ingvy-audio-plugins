// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The editor's host, against a transport that records and can speak.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  connect, connected, onParam, onMessage, onBytes, setParam, beginGesture, endGesture, sendMessage,
} from '../src/lib/host.js';
import { recorder } from './support/transport.mjs';

test('nothing is lost or thrown before a transport is connected', () => {
  connect(null);
  const seen = [];
  const off = onParam((i, v) => seen.push([i, v]));
  setParam(1, 0.5);
  sendMessage(120);
  beginGesture(1);
  endGesture(1);
  off();
  assert.deepEqual(seen, [[1, 0.5]], 'a write still reaches the controls');
  assert.equal(connected(), null);
});

test('writes, gestures and messages reach the transport', () => {
  const t = recorder();
  beginGesture(2);
  setParam(2, 0.25);
  endGesture(2);
  sendMessage(96, '3:1');
  assert.deepEqual(t.sent, [['begin', 2], ['set', 2, 0.25], ['end', 2], ['msg', 96, '3:1']]);
  assert.equal(connected(), t);
});

test('a value the host sends reaches the listeners', () => {
  const t = recorder();
  const seen = [];
  const off = onParam((i, v) => seen.push([i, v]));
  t.in.param(3, 0.25);
  off();
  assert.deepEqual(seen, [[3, 0.25]]);
});

/*
 * THE ECHO. During a drag the UI writes 0.40, 0.45, 0.50 ...; the host echoes
 * each one back a little later. An echo of 0.40 arriving after the UI has
 * already written 0.50 moved the knob backwards under the pointer.
 */
test('an echo is ignored while that parameter\'s gesture is open', () => {
  const t = recorder();
  const seen = [];
  const off = onParam((i, v) => seen.push([i, v]));
  beginGesture(5);
  setParam(5, 0.5);
  t.in.param(5, 0.4);
  t.in.param(6, 0.9);
  endGesture(5);
  t.in.param(5, 0.5);
  off();
  assert.deepEqual(seen, [[5, 0.5], [6, 0.9], [5, 0.5]]);
});

test('two gestures on one parameter close only when both have ended', () => {
  const t = recorder();
  const seen = [];
  const off = onParam((i, v) => seen.push(v));
  beginGesture(7);
  beginGesture(7);
  endGesture(7);
  t.in.param(7, 0.1);
  endGesture(7);
  t.in.param(7, 0.2);
  off();
  assert.deepEqual(seen, [0.2]);
});

test('text and bytes go to their own listeners', () => {
  const t = recorder();
  const text = [];
  const bytes = [];
  const offT = onMessage((tag, s) => text.push([tag, s]));
  const offB = onBytes(66, (b) => bytes.push(b));
  t.in.message(64, '12.5 µs %');
  t.in.message(65);
  t.in.bytes(66, new Uint8Array([0x33, 0x00, 0xff]));
  t.in.bytes(67, new Uint8Array([1]));
  offT();
  offB();
  t.in.bytes(66, new Uint8Array([2]));
  assert.deepEqual(text, [[64, '12.5 µs %'], [65, '']]);
  assert.deepEqual(bytes.map((b) => [...b]), [[0x33, 0x00, 0xff]]);
});

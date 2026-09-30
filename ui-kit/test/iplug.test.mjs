/*
 * The iPlug2 bridge, without a WebView.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * The plugin calls SPVFD / SAMFD as globals on the page; these tests call them
 * the same way.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  onParam, onMessage, onBytes, setParam, beginGesture, endGesture,
} from '../src/lib/iplug.js';

const b64 = (bytes) => btoa(String.fromCharCode(...bytes));
const utf8 = (s) => [...new TextEncoder().encode(s)];

test('a value the host sends reaches the listeners', () => {
  const seen = [];
  const off = onParam((i, v) => seen.push([i, v]));
  globalThis.SPVFD(3, 0.25);
  off();
  assert.deepEqual(seen, [[3, 0.25]]);
});

/*
 * THE ECHO. During a drag the UI writes 0.40, 0.45, 0.50 ...; the host echoes
 * each one back a little later. An echo of 0.40 arriving after the UI has
 * already written 0.50 moved the knob backwards under the pointer.
 */
test('an echo is ignored while that parameter\'s gesture is open', () => {
  const seen = [];
  const off = onParam((i, v) => seen.push([i, v]));
  beginGesture(5);
  setParam(5, 0.5);
  globalThis.SPVFD(5, 0.4);   /* the late echo of an older write */
  globalThis.SPVFD(6, 0.9);   /* another parameter is not held */
  endGesture(5);
  globalThis.SPVFD(5, 0.5);   /* after the gesture the host is authoritative again */
  off();
  assert.deepEqual(seen, [[5, 0.5], [6, 0.9], [5, 0.5]]);
});

test('two gestures on one parameter close only when both have ended', () => {
  const seen = [];
  const off = onParam((i, v) => seen.push(v));
  beginGesture(7);
  beginGesture(7);
  endGesture(7);
  globalThis.SPVFD(7, 0.1);
  endGesture(7);
  globalThis.SPVFD(7, 0.2);
  off();
  assert.deepEqual(seen, [0.2]);
});

test('text is decoded as UTF-8, once', () => {
  const seen = [];
  const off = onMessage((tag, text) => seen.push([tag, text]));
  globalThis.SAMFD(0, 0, b64(utf8('12.5 µs %')));
  off();
  assert.deepEqual(seen, [[0, '12.5 µs %']]);
});

test('a binary tag arrives as bytes and never as text', () => {
  const text = [];
  const bytes = [];
  const offT = onMessage((tag, t) => text.push(tag));
  const offB = onBytes(66, (b) => bytes.push(b));
  globalThis.SAMFD(66, 0, b64([0x33, 0x3a, 0x00, 0xff, 0x80]));
  globalThis.SAMFD(67, 0, b64(utf8('x')));
  offT();
  offB();
  assert.deepEqual(text, [67]);
  assert.equal(bytes.length, 1);
  assert.ok(bytes[0] instanceof Uint8Array);
  assert.deepEqual([...bytes[0]], [0x33, 0x3a, 0x00, 0xff, 0x80]);
});

test('a payload that is not base64 is passed through as text', () => {
  const seen = [];
  const off = onMessage((tag, t) => seen.push(t));
  globalThis.SAMFD(1, 0, 'not base64!');
  off();
  assert.deepEqual(seen, ['not base64!']);
});

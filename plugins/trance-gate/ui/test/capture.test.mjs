// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Trance Gate's binary readouts, as ni::Scope and tg_core_render_gate
 * write them.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { decodeScope, decodeGate, SCOPE_STRIDE } from '../src/lib/capture.js';

const bytes = (header, data) =>
  Uint8Array.from([...header].map((c) => c.charCodeAt(0)).concat(data));

test('a capture decodes four bounds a column into one buffer, reused', () => {
  const b = bytes('2:1000:1:', [0, 255, 128, 128, 64, 191, 0, 0]);
  const s = decodeScope(b);
  assert.equal(s.count, 2);
  assert.equal(s.stride, SCOPE_STRIDE);
  assert.equal(s.windowMs, 1000);
  assert.equal(s.head, 1);
  assert.equal(s.data[0], -1);
  assert.equal(s.data[1], 1);
  assert.ok(Math.abs(s.data[4] - (64 / 127.5 - 1)) < 1e-6);
  const again = decodeScope(b, s.data);
  assert.equal(again.data, s.data, 'the next frame decodes into the same buffer');
});

test('a short or malformed capture is dropped whole', () => {
  assert.equal(decodeScope(bytes('2:1000:1:', [0, 0, 0, 0, 0])), null);
  assert.equal(decodeScope(bytes('x:1000:1:', [0, 0, 0, 0])), null);
  assert.equal(decodeScope('4:1000:0:00000000'), null, 'hex text is the old format');
});

test('the gate is a byte of gain a sample, a 0 byte included', () => {
  const g = decodeGate(bytes('2:2:', [0, 255, 128, 0]));
  assert.equal(g.length, 2);
  assert.equal(g.perStep, 2);
  assert.deepEqual([...g.values].map((v) => Math.round(v * 255)), [0, 255, 128, 0]);
});

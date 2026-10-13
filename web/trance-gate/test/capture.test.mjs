// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The engine's two renders, decoded, as tg_core_render_gate and
 * tg_core_render_envelope write them. render.test.mjs feeds the real ones
 * through; this holds the format to its edges.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { decodeGate, decodeEnvelope, levelAt } from '../src/lib/capture.js';

const bytes = (header, data) =>
  Uint8Array.from([...header].map((c) => c.charCodeAt(0)).concat(data));

test('the gate is a byte of gain a sample, a 0 byte included', () => {
  const g = decodeGate(bytes('2:2:', [0, 255, 128, 0]));
  assert.equal(g.length, 2);
  assert.equal(g.perStep, 2);
  assert.deepEqual([...g.values].map((v) => Math.round(v * 255)), [0, 255, 128, 0]);
});

test('a malformed gate is no gate', () => {
  assert.equal(decodeGate(bytes('x:2:', [0, 0])), null);
  assert.equal(decodeGate(bytes('2:0:', [0, 0])), null);
});

test('the envelope is the gated curve, then the ghost, a byte a sample each', () => {
  const e = decodeEnvelope(bytes('1:2:', [0, 255, 128, 64]));
  assert.equal(e.steps, 1);
  assert.equal(e.perStep, 2);
  assert.deepEqual([...e.gated].map((v) => Math.round(v * 255)), [0, 255]);
  assert.deepEqual([...e.ghost].map((v) => Math.round(v * 255)), [128, 64]);
});

test('a short envelope is dropped whole: half a curve would read as a real one', () => {
  assert.equal(decodeEnvelope(bytes('1:2:', [0, 255, 128])), null);
});

test('a level between samples is linear, and past the end is the last', () => {
  const v = Float32Array.from([0, 1, 0.5]);
  assert.equal(levelAt(v, 2, 0), 0);
  assert.equal(levelAt(v, 2, 0.25), 0.5);
  assert.equal(levelAt(v, 2, 0.5), 1);
  assert.equal(levelAt(v, 2, 9), 0.5);
  assert.equal(levelAt(null, 2, 0.5), 0);
});

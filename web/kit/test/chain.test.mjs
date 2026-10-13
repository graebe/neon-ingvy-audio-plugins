// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * chain_params entries as the editor reads them, with entries copied from the
 * modules' own declarations (engines/<p>/crates/<p>-move/src/params.rs).
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { paramOf, decimalsOf } from '../src/lib/chain.js';

const DEPTH = { key: 'depth', type: 'float', min: 0, max: 1, default: 1, step: 0.01, unit: '%' };
const DELAY = { key: 'delay', type: 'float', min: -100, max: 100, default: 0, step: 0.1, unit: '%' };
const SOURCE = { key: 'source', type: 'enum', options: ['Cycle', 'MIDI'], wire_format: 'index', default: '0' };
const RATE = { key: 'rate', type: 'enum', options: ['1/1T', '1/2', '1/16'], default: '1/16' };

test('decimals follow the step', () => {
  assert.deepEqual([0.01, 0.1, 1, 0.5, 0, undefined].map(decimalsOf), [2, 1, 0, 1, 0, 0]);
});

test('a fraction in % reads as a percentage and writes back as the fraction', () => {
  const p = paramOf(DEPTH);
  assert.equal(p.norm('0.25'), 0.25);
  assert.equal(p.text('1.00'), '100 %');
  assert.equal(p.text('0.333'), '33 %');
  assert.equal(p.wire(0.5), '0.5');
  assert.equal(p.parse('40'), '0.4');
  assert.equal(p.defaultNorm, 1);
  assert.equal(p.norm('junk'), undefined);
  assert.equal(p.text('junk'), '');
  assert.equal(p.parse('junk'), undefined);
});

test('a bipolar % is a percentage already, and clamps', () => {
  const p = paramOf(DELAY);
  assert.equal(p.norm('-100'), 0);
  assert.equal(p.norm('0'), 0.5);
  assert.equal(p.norm('250'), 1);
  assert.equal(p.text('-20'), '-20.0 %');
  assert.equal(p.wire(0.25), '-50');
  assert.equal(p.parse('12,5'), '12.5');
  assert.equal(p.defaultNorm, 0.5);
});

test('an index-wired enum writes indices and reads either', () => {
  const p = paramOf(SOURCE);
  assert.equal(p.kind, 'enum');
  assert.equal(p.norm('1'), 1);
  assert.equal(p.norm('MIDI'), 1);
  assert.equal(p.norm('7'), undefined);
  assert.equal(p.wire(0.9), '1');
  assert.equal(p.text('0'), 'Cycle');
  assert.equal(p.parse('midi'), '1');
  assert.equal(p.parse('Sidechain'), undefined);
});

test('a label-wired enum writes labels', () => {
  const p = paramOf(RATE);
  assert.equal(p.norm('1/16'), 1);
  assert.equal(p.wire(0.5), '1/2');
  assert.equal(p.defaultNorm, 1);
  assert.equal(p.norm('2'), 1, 'an index still reads');
});

test('what is not a value has no conversion', () => {
  assert.equal(paramOf({ key: 'shape', type: 'canvas' }), null);
  assert.equal(paramOf({ key: 'web_ui', type: 'string', access: 'read' }), null);
  assert.equal(paramOf(null), null);
  assert.equal(paramOf({ type: 'float' }), null);
  const flat = paramOf({ key: 'x', type: 'float', min: 3, max: 3 });
  assert.equal(flat.norm('3'), 0);
  assert.equal(flat.parse('3'), '3');
});

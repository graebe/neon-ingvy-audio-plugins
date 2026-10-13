// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The ranges and options the editor reads from the module's declaration.
 *
 * The plugin-era version of this file parsed Params.cpp and held a copy of its
 * ranges to it. There is no copy now -- declaration.js reads chain_params --
 * so what is left to pin is that it reads the right numbers, and that the
 * conversion a drag makes is exact at the ends and clamps past them: a drag
 * runs past the edge of the well constantly.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readChainParams } from '../../kit/vite.editor.mjs';
import { P } from '../src/lib/msg.js';
import { declarationOf, shapeFromNorm } from '../src/lib/declaration.js';

const decl = declarationOf(readChainParams('sc-move'));
const CONTINUOUS = ['delay', 'attack', 'hold', 'release', 'depth', 'velSens'];

test('the ranges are the module\'s, in the engine\'s units', () => {
  /* Delay goes both ways: on Cycle it is a phase, so -20 % is 80 %. */
  assert.deepEqual(decl.range(P.delay), [-100, 100]);
  assert.deepEqual(decl.range(P.attack), [0, 200]);
  assert.deepEqual(decl.range(P.hold), [0, 200]);
  assert.deepEqual(decl.range(P.release), [0, 200]);
  /* A fraction, as the engine holds it -- not the plugin's 0..100. */
  assert.deepEqual(decl.range(P.depth), [0, 1]);
  assert.deepEqual(decl.range(P.velSens), [0, 1]);
  /* A choice has options, not a range. */
  assert.equal(decl.range(P.source), null);
});

test('the options are the module\'s', () => {
  assert.deepEqual(decl.options(P.source), ['Cycle', 'MIDI']);
  assert.deepEqual(decl.options(P.curve), ['Linear', 'Exponential', 'S-Curve']);
  assert.deepEqual(decl.options(P.timeMode), ['ms', '% of cycle']);
  assert.deepEqual(decl.options(P.midiMode), ['Trigger', 'Gate']);
  assert.equal(decl.options(P.rate).length, 12);
  assert.equal(decl.options(P.channel).length, 17);
  assert.equal(decl.options(P.note).length, 128);
  /* Live's numbering, where 36 is C1 -- the default trigger. */
  assert.equal(decl.options(P.note)[36], 'C1');
  assert.deepEqual(decl.options(P.depth), []);
});

test('toNorm and fromNorm are inverses across every range, exact at the ends', () => {
  for (const name of CONTINUOUS) {
    const idx = P[name];
    const [lo, hi] = decl.range(idx);
    for (let k = 0; k <= 20; k++) {
      const v = lo + ((hi - lo) * k) / 20;
      const back = decl.fromNorm(idx, decl.toNorm(idx, v));
      assert.ok(Math.abs(back - v) < 1e-9, `${name}: ${v} -> ${back}`);
    }
    assert.equal(decl.toNorm(idx, lo), 0);
    assert.equal(decl.toNorm(idx, hi), 1);
  }
});

test('out of range clamps rather than extrapolating', () => {
  for (const name of CONTINUOUS) {
    const idx = P[name];
    const [lo, hi] = decl.range(idx);
    assert.equal(decl.toNorm(idx, lo - 1000), 0);
    assert.equal(decl.toNorm(idx, hi + 1000), 1);
    assert.equal(decl.fromNorm(idx, -5), lo);
    assert.equal(decl.fromNorm(idx, 5), hi);
  }
});

test('a parameter with nothing declared converts to nothing', () => {
  const empty = declarationOf([]);
  assert.equal(empty.toNorm(P.attack, 50), 0);
  assert.equal(empty.fromNorm(P.attack, 0.5), 0);
  assert.deepEqual(empty.options(P.source), []);
  assert.equal(declarationOf(null).entry(P.depth), undefined);
});

/*
 * THE HANDLE UNDER THE POINTER. While a handle is dragged the shape is drawn
 * from the parameters the drag writes, converted through these same ranges.
 */
test('the held shape is the parameters, in the engine\'s units', () => {
  const norm = { [P.delay]: 0.6, [P.attack]: 0.05, [P.hold]: 0.1, [P.release]: 0.25, [P.depth]: 0.8 };
  const s = shapeFromNorm((i) => norm[i], { curve: 2, cycle: true, delay: 99 }, decl);
  assert.equal(s.curve, 2);
  assert.equal(s.cycle, true);
  assert.ok(Math.abs(s.delay - 20) < 1e-9);
  assert.ok(Math.abs(s.attack - 10) < 1e-9);
  assert.ok(Math.abs(s.hold - 20) < 1e-9);
  assert.ok(Math.abs(s.release - 50) < 1e-9);
  assert.ok(Math.abs(s.depth - 0.8) < 1e-9);
});

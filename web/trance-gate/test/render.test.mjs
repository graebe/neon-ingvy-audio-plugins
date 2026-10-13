// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The real engine, as the page and the harness run it: tg_web.wasm through the
 * kit's capi.js. The page's curves are rendered from a patch the engine wrote,
 * and the harness's engine answers as the module does.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { renderCurves } from '../src/lib/renders.js';
import { wasmEngine } from '../src/lib/wasm-engine.js';
import { decodeUi, decodeEngineParams } from '../src/lib/readouts.js';
import { loadEngine } from './support/engine.mjs';

const capi = await loadEngine();

/* A patch the engine wrote: steps 0 and 2 of four on, the gate half open. */
function patch() {
  const e = wasmEngine(capi);
  e.set('length', '3');
  for (const [i, mode] of [[0, 'On'], [1, 'Off'], [2, 'On'], [3, 'Off']]) {
    e.set('cursor', String(i));
    e.set('step', mode);
  }
  e.set('hold', '0.5');
  const state = e.get('state');
  e.dispose();
  return state;
}

test('the gate curve is rendered from a patch, by the engine', () => {
  const { gate } = renderCurves(capi, patch());
  assert.ok(gate, 'the engine drew a gate');
  assert.equal(gate.length, 4);
  assert.equal(gate.perStep, 64);
  assert.equal(gate.values.length, 4 * 64);
  const at = (step, frac) => gate.values[Math.floor((step + frac) * gate.perStep)];
  assert.ok(at(0, 0.25) > 0.8, 'step 0 is open inside its width');
  assert.ok(at(0, 0.9) < 0.2, 'and shut past it');
  assert.ok(at(1, 0.5) < 0.05, 'step 1 is off');
  assert.ok(at(2, 0.25) > 0.8, 'step 2 is open');
});

test('the envelope plot\'s two curves come with it', () => {
  const { envelope } = renderCurves(capi, patch());
  assert.ok(envelope);
  assert.equal(envelope.gated.length, envelope.steps * envelope.perStep);
  assert.equal(envelope.ghost.length, envelope.gated.length);
});

test('no patch, nothing to draw', () => {
  assert.deepEqual(renderCurves(capi, ''), { gate: null, envelope: null });
});

test('the harness\'s engine answers as the module does', () => {
  const e = wasmEngine(capi);
  assert.equal(e.get('rate'), '1/16');
  assert.equal(e.get('length'), '15', 'the option index of 16');
  assert.equal(e.get('web_ui'), null, 'a key the engine does not serve');
  const p = decodeEngineParams(e.get('params'));
  assert.equal(p.rate, '1/16');
  assert.ok(p.widthMs > 0);
  e.dispose();
});

test('the harness\'s transport moves the playhead, and stops it', () => {
  const e = wasmEngine(capi);
  e.advance(20);
  const a = decodeUi(e.get('ui'));
  e.advance(500);
  const b = decodeUi(e.get('ui'));
  assert.equal(b.moving, true);
  /* 120 BPM at 1/16: 125 ms a step, so half a second is four steps. */
  assert.ok(Math.abs(b.msStep - 125) < 0.01, `ms a step ${b.msStep}`);
  const moved = (b.phase - a.phase + 16) % 16;
  assert.ok(Math.abs(moved - 4) < 0.2, `moved ${moved} steps`);
  e.stop();
  e.advance(100);
  assert.equal(decodeUi(e.get('ui')).moving, false);
  e.dispose();
});

test('Roll rerolls the slot and Hold does nothing', () => {
  const e = wasmEngine(capi);
  const before = e.get('pattern');
  e.set('randomize', 'Hold');
  assert.equal(e.get('pattern'), before);
  let changed = false;
  for (let i = 0; i < 4 && !changed; i++) {
    e.set('randomize', 'Roll');
    changed = e.get('pattern') !== before;
  }
  assert.ok(changed, 'a roll deals a new pattern');
  e.dispose();
});

test('a step\'s arrival rank is written at the cursor', () => {
  const e = wasmEngine(capi);
  e.set('pattern', 'F');
  e.set('cursor', '3');
  e.set('step_order', '1');
  const u = decodeUi(e.get('ui'));
  assert.equal(u.orders[3], 1, 'step 3 arrives first');
  assert.deepEqual(u.orders.slice(0, 4).sort(), [1, 2, 3, 4]);
  e.dispose();
});

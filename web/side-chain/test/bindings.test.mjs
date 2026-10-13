// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The binding table against the module's declaration.
 *
 * A KEY THE DECLARATION DOES NOT HAVE IS A DEAD CONTROL ON THE DEVICE: the
 * transport finds no entry for it, normalises nothing, writes nothing, and the
 * knob turns under the user's finger with no effect and no error anywhere.
 * And a parameter the module declares that no control reaches is a setting
 * the page cannot make. Both are checked here against sc-move's params.rs --
 * the string the device serves, read by the same parser the build uses.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readChainParams } from '@ultraviolet/ui/vite';
import { KEYS, P, NUM_PARAMS, FEEDS, MSG } from '../src/lib/msg.js';
import { KNOBS, TRIGGER_ROW, SHAPE_ROW, HANDLES, showsFor } from '../src/lib/layout.js';
import { CYCLE, MIDI } from '../src/lib/status.js';

const declared = readChainParams('sc-move');
const byKey = new Map(declared.map((e) => [e.key, e]));
const CONTROLS = [...KNOBS, ...TRIGGER_ROW, ...SHAPE_ROW];

test('the table has one key per editor index, in the engine\'s Param order', () => {
  assert.equal(KEYS.length, NUM_PARAMS);
  assert.equal(new Set(KEYS).size, KEYS.length, 'a key is bound twice');
  /* P names every index once, 0..NUM_PARAMS-1. */
  assert.deepEqual(Object.values(P).sort((a, b) => a - b), [...KEYS.keys()]);
  /* The spellings sc-core's Param::key() gives each index. */
  assert.equal(KEYS[P.note], 'trigger_note');
  assert.equal(KEYS[P.timeMode], 'time_mode');
  assert.equal(KEYS[P.velSens], 'vel_sens');
});

test('every mapped key is a value the module declares', () => {
  for (const key of KEYS) {
    const e = byKey.get(key);
    assert.ok(e, `"${key}" is not in sc-move's chain_params`);
    assert.ok(['float', 'int', 'enum'].includes(e.type), `"${key}" is a ${e.type}, not a value`);
    assert.notEqual(e.access, 'read', `"${key}" is read-only; a control cannot write it`);
  }
});

test('every value the module declares has a key, and nothing the Move lacks does', () => {
  const values = declared.filter((e) => ['float', 'int', 'enum'].includes(e.type) && e.access !== 'read');
  for (const e of values) assert.ok(KEYS.includes(e.key), `the module declares "${e.key}" and no control binds it`);
  for (const absent of ['threshold', 'lockout']) {
    assert.ok(!KEYS.includes(absent), `"${absent}" is bound, and the Move has no such parameter`);
  }
});

test('every control that can show has a key, of the kind the control needs', () => {
  for (const c of CONTROLS) {
    const key = KEYS[c.idx];
    assert.ok(key, `"${c.label}" has no key`);
    const e = byKey.get(key);
    assert.ok(e, `"${c.label}" is bound to "${key}", which the module does not declare`);
    if (c.kind === 'select' || c.kind === 'choice') {
      assert.equal(e.type, 'enum', `"${c.label}" chooses, and "${key}" is a ${e.type}`);
    }
    if (c.kind === 'select') {
      /* The Select card: a list of twelve at most; more wants a knob. */
      assert.ok(e.options.length <= 12, `"${c.label}" offers ${e.options.length} options in a Select`);
    }
    if (c.kind === 'toggle') {
      assert.equal(e.type, 'enum');
      assert.equal(e.options.length, 2, `"${c.label}" is a switch over ${e.options.length} options`);
    }
    if (c.kind === 'stage') {
      assert.equal(e.unit, '%', `"${c.label}" reads as a stage, in % of the cycle`);
    }
  }
});

test('every parameter is reachable from some control, on some source', () => {
  const reached = new Set(CONTROLS.map((c) => c.idx));
  for (const [name, idx] of Object.entries(P)) {
    assert.ok(reached.has(idx), `${name} has no control`);
  }
});

test('the source decides which controls show, as the native editor does', () => {
  const shown = (source) => CONTROLS.filter((c) => showsFor(c, source)).map((c) => c.label);
  assert.deepEqual(shown(CYCLE), ['Depth', 'Delay', 'Attack', 'Hold', 'Release', 'Source', 'Rate',
                                  'Curve', '% of cycle']);
  assert.deepEqual(shown(MIDI), ['Depth', 'Delay', 'Attack', 'Hold', 'Release', 'Note', 'Channel',
                                 'Velocity', 'Source', 'Gate', 'Curve', '% of cycle']);
});

test('the sources are the Move\'s two, by index', () => {
  const source = byKey.get('source');
  assert.deepEqual(source.options, ['Cycle', 'MIDI']);
  assert.equal(source.wire_format, 'index');
  assert.equal(source.options[CYCLE], 'Cycle');
  assert.equal(source.options[MIDI], 'MIDI');
});

test('the handles move the stages and the depth', () => {
  assert.deepEqual(HANDLES.map((h) => KEYS[h.xIdx]), ['delay', 'attack', 'hold', 'release']);
  assert.deepEqual(HANDLES.map((h) => h.stage), ['delay', 'attack', 'hold', 'release']);
  assert.equal(KEYS[HANDLES[1].yIdx], 'depth');
});

test('the feeds are the web_ui declaration\'s extra_keys, and none is a declared key', () => {
  const webUi = declared[declared.length - 1];
  assert.equal(webUi.key, 'web_ui', 'web_ui is the last entry');
  assert.deepEqual(Object.keys(FEEDS).sort(), [...webUi.extra_keys].sort());
  for (const k of Object.keys(FEEDS)) assert.ok(!byKey.has(k), `"${k}" is declared, so it would never move`);
  assert.deepEqual(Object.values(FEEDS).sort(), [MSG.uiState, MSG.params, MSG.stageMs].sort());
});

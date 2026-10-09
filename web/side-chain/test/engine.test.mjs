// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The harness's engine, and the whole path a control takes to it: the binding
 * table through the kit's transport and fake manager into the real engine,
 * and the feeds back. What the e2e tests see in a browser, without one.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readChainParams } from '@ultraviolet/ui/vite';
import { createFakeSchwung } from '@ultraviolet/ui/fake-schwung';
import { createSchwungTransport } from '@ultraviolet/ui/schwung';
import { SHELL_MSG } from '@ultraviolet/ui/shell';
import { KEYS, FEEDS, MSG, P } from '../src/lib/msg.js';
import { parseUi, parseParams } from '../src/lib/feeds.js';
import { NO_WASM, realEngine } from './support/engine.mjs';

const chainParams = readChainParams('sc-move');

/* A test's own clock for the fake manager: frames run when the test says. */
function manualTimers() {
  const fns = new Set();
  return {
    setInterval: (fn) => { fns.add(fn); return fn; },
    clearInterval: (fn) => fns.delete(fn),
    run: (n) => { for (let i = 0; i < n; i++) for (const fn of fns) fn(); },
  };
}

test('the engine answers every declared value and the shell\'s web_ui', { skip: NO_WASM }, async () => {
  const e = await realEngine();
  for (const entry of chainParams) {
    if (entry.type === 'canvas') continue;
    assert.notEqual(e.get(entry.key), null, `the engine does not answer "${entry.key}"`);
  }
  assert.equal(e.get('web_ui'), '1');
  assert.equal(e.get('no_such_key'), null);
  e.destroy();
});

test('advance runs whole blocks and keeps the remainder', { skip: NO_WASM }, async () => {
  const e = await realEngine();
  /* 1 ms is 44.1 frames, under a block: nothing yet, then it adds up. */
  e.advance(1);
  assert.equal(e.transport().beats, 0);
  e.advance(2);
  assert.ok(e.transport().beats > 0, 'three ms make a block');
  e.destroy();
});

test('the playhead moves while the transport runs, and stops with it', { skip: NO_WASM }, async () => {
  const e = await realEngine();
  e.advance(100);
  const a = parseUi(e.get('ui')).sweep;
  e.advance(100);
  const b = parseUi(e.get('ui')).sweep;
  assert.ok(b > a, `sweep ${a} -> ${b}`);

  e.transport({ running: false });
  e.advance(50);
  const stopped = parseUi(e.get('ui'));
  assert.equal(stopped.advancing, 0);
  e.advance(100);
  assert.equal(parseUi(e.get('ui')).sweep, stopped.sweep, 'a stopped transport holds the sweep');
  e.destroy();
});

test('a MIDI note fires the duck on the MIDI source', { skip: NO_WASM }, async () => {
  const e = await realEngine();
  e.set('source', '1');
  e.advance(50);
  const before = parseUi(e.get('ui')).fires;
  e.note(36, 100, 1);
  e.advance(20);
  e.note(36, 0, 1);
  e.advance(20);
  assert.equal(parseUi(e.get('ui')).fires, before + 1);
  e.destroy();
});

test('a control writes through the binding table into the engine, and the feeds come back',
     { skip: NO_WASM }, async () => {
  const e = await realEngine();
  const timers = manualTimers();
  const fake = createFakeSchwung({ chainParams, engine: e, timers });
  const params = [];
  const messages = [];
  const transport = createSchwungTransport({
    bridge: fake.bridge, params: KEYS, feeds: FEEDS,
    defaultsTag: SHELL_MSG.defaults, setTextTag: SHELL_MSG.setText,
  });
  const learned = await transport.attach({
    param: (i, v) => params.push([i, v]),
    message: (tag, text) => messages.push([tag, text]),
    bytes: () => {},
  });
  assert.equal(learned, true, 'chain_params arrived');

  /* Subscribing pushed the feeds and every declared value. */
  assert.ok(messages.some(([t]) => t === MSG.params));
  assert.ok(messages.some(([t]) => t === MSG.uiState));
  assert.ok(params.some(([i]) => i === P.attack));

  /* Attack to a quarter of its 0..200 % is 50 %. */
  transport.setParam(P.attack, 0.25);
  assert.equal(Number(e.get('attack')), 50);
  /* The Move's second source is MIDI, at the far end of a two-option list. */
  transport.setParam(P.source, 1);
  assert.equal(e.get('source'), '1');
  /* Typed text is the declaration's to read: "40 %" of Release. */
  transport.sendMessage(SHELL_MSG.setText, `${P.release}:40 %`);
  assert.equal(Number(e.get('release')), 40);

  /* The manager re-reads the feeds 150 ms after a write. */
  messages.length = 0;
  timers.run(10);
  const latest = messages.filter(([t]) => t === MSG.params).pop();
  assert.ok(latest, 'no params feed after the writes');
  const f = parseParams(latest[1]);
  assert.equal(f[P.attack], 50);
  assert.equal(f[P.release], 40);
  assert.equal(f[P.source], 1);
  fake.stop();
  e.destroy();
});

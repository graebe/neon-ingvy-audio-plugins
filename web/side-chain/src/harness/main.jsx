// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * harness.html's entry: the editor against a Move that is not there.
 *
 * The kit's fake manager (fake-schwung.js) does what Schwung's does -- the
 * push on subscribing, the echo of a write, the feeds re-read 150 ms after a
 * write and on a 500 ms heartbeat -- over the REAL engine as wasm
 * (engine.js). So what the page is developed and tested against is the
 * engine's own values and the manager's own timing, not a model of either.
 *
 * `window.__move` is the Move's side, for a test or a person at the console:
 *
 *   device(key, value)   a knob turned on the Move: written and echoed as the
 *                        manager echoes any write
 *   get(key)             what the engine holds, as the module would answer
 *   transport({ running, bpm, beats })
 *   note(note, velocity, channel)
 *   writes               every bridge.setParam the page made, [key, value]
 *
 * `?stopped` starts the transport stopped at the top of the song, so nothing
 * moves and the picture is the same every time -- what a screenshot needs.
 *
 * The kit's capi.js and fake-schwung.js are reached by path: the kit's
 * package exports neither (README, "Known gaps").
 */
import chainParams from 'virtual:chain-params';
import wasmUrl from '../../../../target/wasm32-unknown-unknown/release/sc_web.wasm?url';
import { loadCapi } from '@ultraviolet/ui/capi';
import { createFakeSchwung } from '@ultraviolet/ui/fake-schwung';
import { createEngine } from './engine.js';
import { boot } from '../boot.jsx';

const stopped = new URLSearchParams(location.search).has('stopped');
const engine = createEngine(await loadCapi(wasmUrl, 'sc'), { running: !stopped });
/* ?jitter: every push reaches the page 20 to 400 ms late, a different
 * amount each time, as on a real Move over WiFi -- what the playhead must
 * draw through without a wobble. */
const jitter = new URLSearchParams(location.search).has('jitter') ? () => 20 + Math.random() * 380 : null;
const fake = createFakeSchwung({ chainParams, engine, component: 'fx1', latency: jitter });

const writes = [];
const bridge = {
  ...fake.bridge,
  setParam(key, value) {
    writes.push([key, String(value)]);
    fake.bridge.setParam(key, value);
  },
};

window.schwungRemote = bridge;
window.__move = {
  device: fake.device,
  get: engine.get,
  transport: engine.transport,
  note: engine.note,
  writes,
};

boot(document.getElementById('root'), bridge, chainParams);

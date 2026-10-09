// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * harness.html's entry: the same editor, on a Move that is not there.
 *
 * The kit's fake Move (lib/fake-schwung.js) stands in for Schwung's manager,
 * with the module's real engine behind it -- tg_web.wasm, a separate instance
 * from the one the page renders its curves with, as the device and the page
 * are separate -- and the module's own declaration (`virtual:chain-params`,
 * read from tg-move's params.rs), so a range here cannot differ from the
 * device's. It is set as `window.schwungRemote`, exactly where the Move's
 * bridge would be, and the editor starts as it does on a Move.
 *
 * FOR A TEST, `window.__move`:
 *
 *   device(key, value)   a write from the Move's side: a knob, the jog, a
 *                        slot recall -- `device('slot', '3')`
 *   get(key)             what the engine answers now
 *   writes               every write the page sent, [key, value], in order
 *   play(), stop()       the transport
 *
 * Development only: the build's one input is web_ui.html.
 */
import chainParams from 'virtual:chain-params';
import { loadCapi } from '@ultraviolet/ui/capi';
import { createFakeSchwung } from '@ultraviolet/ui/fake-schwung';
import { wasmEngine } from './lib/wasm-engine.js';
import { start } from './boot.jsx';
import wasmUrl from '../../../target/wasm32-unknown-unknown/release/tg_web.wasm?url';

const capi = await loadCapi(wasmUrl, 'tg');
const engine = wasmEngine(capi);
const fake = createFakeSchwung({ chainParams, engine, component: 'fx1' });

/* Every write the page makes, recorded on its way to the fake manager. */
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
  get: (key) => engine.get(key),
  writes,
  play: () => engine.play(),
  stop: () => engine.stop(),
};

start(window.schwungRemote, document.getElementById('root'));

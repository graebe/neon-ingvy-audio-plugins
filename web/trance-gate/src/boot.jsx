// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The editor, started against a bridge: Schwung's own on a Move (main.jsx),
 * or the fake Move the harness builds (harness.jsx). Everything after the
 * bridge is the same in both.
 *
 *   start(bridge, root)
 *     the transport (the kit's, with this module's table: lib/move.js),
 *     connected to the editor's host before the editor renders -- so the
 *     values the manager replays on subscribe have listeners to land on --
 *     and the engine's wasm loaded beside it for the plots' curves.
 */
import { render } from 'solid-js/web';
import { createSignal } from 'solid-js';
import { connect } from '@ultraviolet/ui/host';
import { createSchwungTransport } from '@ultraviolet/ui/schwung';
import { loadCapi } from '@ultraviolet/ui/capi';
import '@ultraviolet/ui/tokens.css';
import '@ultraviolet/ui/components.css';
import './app.css';
import App from './App.jsx';
import { MSG } from './lib/msg.js';
import { KEYS, FEEDS, MESSAGES, expandParams } from './lib/move.js';
import { renderCurves } from './lib/renders.js';
import wasmUrl from '../../../target/wasm32-unknown-unknown/release/tg_web.wasm?url';
import chainParams from 'virtual:chain-params';

/* How many divisions Rate has on the Move: the declaration's, so its knob
 * steps one an arrow. */
const RATE_STEPS = chainParams.find((p) => p.key === 'rate')?.options?.length ?? 0;

export function start(bridge, root) {
  /* The renderer arrives when the wasm has: the plots draw from then on. A
   * page whose engine will not load still edits -- it only has no curves --
   * and says why in the console, where a developer will look. */
  const [renderer, setRenderer] = createSignal(null);
  loadCapi(wasmUrl, 'tg')
    .then((capi) => setRenderer(() => (state) => renderCurves(capi, state)))
    .catch((err) => console.error('NI Trance Gate: the engine did not load, so the plots stay empty', err));

  const dispose = render(() => <App renderer={renderer} rateSteps={RATE_STEPS} />, root);
  /* After the render: the editor's listeners exist, so subscribing -- which
   * makes the manager replay every value it holds -- reaches them all. */
  connect(createSchwungTransport({
    bridge: expandParams(bridge),
    params: KEYS,
    feeds: FEEDS,
    messages: MESSAGES,
    defaultsTag: MSG.defaults,
    setTextTag: MSG.setText,
  }));
  return dispose;
}

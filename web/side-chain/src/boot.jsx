// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The editor, started against a Move: the real one (index.jsx) or the
 * harness's fake (harness.jsx). One copy of the order, because the order is
 * the part that is easy to get wrong.
 *
 * RENDER FIRST, CONNECT SECOND. Subscribing makes the manager push every value
 * it holds and the module's feeds -- and the fake does it in the same call.
 * The `params` feed is sent again only when a value moves, so a push that
 * lands before the editor's listeners exist is a picture that stays empty
 * until somebody turns a knob. The params store and the feed listeners are
 * created as App renders, synchronously; the transport is connected after.
 *
 * NO BRIDGE IS NO EDITOR. Without the manager's script (a page opened from
 * disk, or from a server that is not the Move's) every control would be
 * inert, and an inert editor reads as a broken module. It says what it is and
 * where it works instead.
 */
import '@ultraviolet/ui/tokens.css';
import '@ultraviolet/ui/components.css';
import './app.css';
import { render } from 'solid-js/web';
import { connect } from '@ultraviolet/ui/host';
import { createSchwungTransport } from '@ultraviolet/ui/schwung';
import { SHELL_MSG } from '@ultraviolet/ui/shell';
import App from './App.jsx';
import { KEYS, FEEDS } from './lib/msg.js';
import { declarationOf } from './lib/declaration.js';

function NoBridge() {
  return (
    <main class="no-bridge">
      <p class="t-title">NI Side-Chain</p>
      <p class="t-hint">
        This is the module's editor for Schwung's web manager on an Ableton Move. Open the
        manager's Remote UI (move.local:7700/remote-ui) with NI Side-Chain on a track, and it
        appears in the module's section.
      </p>
    </main>
  );
}

/** Mount the editor in `root`, talking to `bridge`; `chainParams` is the
 * module's declaration this page was built with. */
export function boot(root, bridge, chainParams) {
  if (!bridge) {
    render(() => <NoBridge />, root);
    return;
  }
  render(() => <App declaration={declarationOf(chainParams)} />, root);
  connect(createSchwungTransport({
    bridge,
    params: KEYS,
    feeds: FEEDS,
    defaultsTag: SHELL_MSG.defaults,
    setTextTag: SHELL_MSG.setText,
  }));
}

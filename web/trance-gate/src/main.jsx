// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * web_ui.html's entry: the editor against the Move's own bridge.
 *
 * Schwung's manager serves /static/schwung-remote-api.js, which web_ui.html
 * loads first and which sets `window.schwungRemote` for the module in this
 * slot. Opened anywhere else -- the file on its own, a server without the
 * manager -- there is no bridge, and the page says so instead of drawing a
 * window whose every control would do nothing.
 */
import { render } from 'solid-js/web';
import { start } from './boot.jsx';

const root = document.getElementById('root');
const bridge = globalThis.window?.schwungRemote;

if (bridge) {
  start(bridge, root);
} else {
  render(() => (
    <p class="no-bridge t-value">
      NI Trance Gate's editor runs on a Move, in Schwung's web manager: open the
      module's slot there.
    </p>
  ), root);
}

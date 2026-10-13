// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * web_ui.html's entry: the editor against the Move it is served from.
 *
 * `window.schwungRemote` is the manager's bridge, defined by
 * /static/schwung-remote-api.js, which web_ui.html loads as a classic script
 * before this module runs. The declaration is the one this page was built
 * with, sc-move's own (vite.editor.mjs reads it from params.rs) -- the same
 * string the device serves, shipped in the same package.
 */
import chainParams from 'virtual:chain-params';
import { boot } from './boot.jsx';

boot(document.getElementById('root'), window.schwungRemote, chainParams);

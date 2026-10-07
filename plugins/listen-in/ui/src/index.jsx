// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Listen-In — the editor's entry point.
 */
import { render } from 'solid-js/web';
import '@ultraviolet/ui/tokens.css';
import '@ultraviolet/ui/components.css';
import './app.css';
import App from './App.jsx';

render(() => <App />, document.getElementById('root'));

/*
 * NI Listen-In — the editor's entry point.
 * Copyright (c) 2026 Torben Gräber. MIT.
 */
import { render } from 'solid-js/web';
import '@ultraviolet/ui/tokens.css';
import '@ultraviolet/ui/components.css';
import './app.css';
import App from './App.jsx';

render(() => <App />, document.getElementById('root'));

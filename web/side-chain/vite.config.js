// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The kit's one build for every Move web editor (web/kit/vite.editor.mjs has
 * what it writes and why): web_ui.html into build/web/side-chain/, with
 * sc-move's declaration as `virtual:chain-params`. The dev server is this
 * editor's own port, so two agents' servers never swap; `vite preview` takes
 * the next one.
 */
import { dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { editorConfig } from '../kit/vite.editor.mjs';

export default editorConfig({
  dir: dirname(fileURLToPath(import.meta.url)),
  module: 'side-chain',
  crate: 'sc-move',
  port: 47343,
});

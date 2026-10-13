// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Trance Gate's page on the Move. The build, the dev server (port 47341,
 * preview 47342) and the module's declaration for the harness are the kit's,
 * shared by every Move editor: web/kit/vite.editor.mjs says what each does.
 */
import { dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { editorConfig } from '../kit/vite.editor.mjs';

export default editorConfig({
  dir: dirname(fileURLToPath(import.meta.url)),
  module: 'trance-gate',
  crate: 'tg-move',
  port: 47341,
});

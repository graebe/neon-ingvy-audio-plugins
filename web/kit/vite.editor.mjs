// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Every Move web editor's build, one copy. web/<product>/vite.config.js is
 *
 *   export default editorConfig({ dir: <its directory>, module: 'trance-gate',
 *                                 crate: 'tg-move', port: 47341 });
 *
 * WHAT IT WRITES, into build/web/<module>/ -- which modules/_shared/package.sh
 * puts beside the module's .so, where Schwung's web manager serves it as
 * /api/remote-ui/module-assets/<module-id>/web_ui.html in the slot's iframe:
 *
 *   web_ui.html                  the page; it loads the manager's bridge,
 *                                /static/schwung-remote-api.js, before itself
 *   assets/ui.js, style.css      the editor, every module in one file and every
 *                                stylesheet in one (fonts inlined): the
 *                                manager rebuilds the iframe often and serves
 *                                with no-store, so a cold start is the common
 *                                case and one request beats many
 *   assets/*.wasm                an engine, where the editor renders with one
 *   assets/ui.js.LICENSE.txt     the notices of every bundled package
 *
 * Relative URLs throughout (base './'): the page lives wherever the module does.
 *
 * IN DEVELOPMENT (`vite` on the editor's own port, so two agents' servers
 * never swap) harness.html is the same editor against a fake Move with the
 * real engine (lib/fake-schwung.js); `virtual:chain-params` is the module's
 * declaration, read from the Rust source the device serves it from, so the
 * harness and the device cannot disagree about a range.
 */
import { defineConfig } from 'vite';
import solid from 'vite-plugin-solid';
import { readFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import licenses from '../../scripts/vite-licenses.mjs';

export const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..', '..');

/** The CHAIN_PARAMS string in a <p>-move crate's src/params.rs, parsed. */
export function readChainParams(crate) {
  const engine = { 'tg-move': 'trance-gate', 'sc-move': 'side-chain' }[crate];
  if (!engine) throw new Error(`no engine known for ${crate}`);
  const src = readFileSync(join(ROOT, 'engines', engine, 'crates', crate, 'src', 'params.rs'), 'utf8');
  const m = /pub const CHAIN_PARAMS: &str = r##"(.*?)"##;/s.exec(src);
  if (!m) throw new Error(`${crate}: no CHAIN_PARAMS raw string in params.rs`);
  return JSON.parse(m[1]);
}

function chainParams(crate) {
  const id = 'virtual:chain-params';
  return {
    name: 'ni-chain-params',
    resolveId: (source) => (source === id ? `\0${id}` : null),
    load: (source) => (source === `\0${id}` ? `export default ${JSON.stringify(readChainParams(crate))};` : null),
  };
}

export function editorConfig({ dir, module, crate, port }) {
  return defineConfig({
    root: dir,
    base: './',
    plugins: [solid(), licenses(), chainParams(crate)],
    build: {
      outDir: join(ROOT, 'build', 'web', module),
      emptyOutDir: true,
      /* Fonts and images inline; a .wasm never does -- it is fetched. */
      assetsInlineLimit: (file) => (file.endsWith('.wasm') ? false : 100_000_000),
      cssCodeSplit: false,
      rollupOptions: {
        input: { web_ui: join(dir, 'web_ui.html') },
        output: {
          inlineDynamicImports: true,
          entryFileNames: 'assets/ui.js',
          assetFileNames: (a) => {
            const name = (a.names && a.names[0]) || a.name || '';
            return name.endsWith('.css') ? 'assets/style.css' : 'assets/[name][extname]';
          },
        },
      },
    },
    server: { port, strictPort: true, fs: { allow: [ROOT] } },
    preview: { port: port + 1, strictPort: true },
  });
}

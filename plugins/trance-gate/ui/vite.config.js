import { defineConfig } from 'vite';
import solid from 'vite-plugin-solid';
// The notices of every package bundled into ui.js, written beside it, and a
// failed build for one THIRD_PARTY_LICENSES.md does not list.
import licenses from '../../../scripts/vite-licenses.mjs';

/*
 * WHAT THE BUILD WRITES, into ../resources/web -- which the plugin's CMake
 * globs into its bundle as web resources:
 *
 *   index.html                 the page
 *   assets/ui.js               every module, in one file (inlineDynamicImports)
 *   assets/style.css           every stylesheet, in one file (cssCodeSplit
 *                              off), with the kit's font INLINED as
 *                              a data URI -- assetsInlineLimit is absurd on
 *                              purpose, so nothing is emitted beside them
 *   assets/ui.js.LICENSE.txt   the notices of every bundled package
 *   fonts/OFL.txt              from public/, the font's licence
 *
 * Fixed file names rather than hashed ones, because the bundle is rebuilt
 * whole and a WKWebView over a custom scheme serves exactly the files that are
 * there.
 */
export default defineConfig({
  plugins: [solid(), licenses()],
  base: './',
  build: {
    outDir: '../resources/web',
    emptyOutDir: true,
    assetsInlineLimit: 100_000_000,
    cssCodeSplit: false,
    rollupOptions: {
      output: {
        inlineDynamicImports: true,
        entryFileNames: 'assets/ui.js',
        assetFileNames: 'assets/[name][extname]',
      },
    },
  },
});

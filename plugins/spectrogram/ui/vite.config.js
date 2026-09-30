import { defineConfig } from 'vite';
import solid from 'vite-plugin-solid';
// The notices of every package bundled into ui.js, written beside it, and a
// failed build for one THIRD_PARTY_LICENSES.md does not list.
import licenses from '../../../scripts/vite-licenses.mjs';

/*
 * The build lands in ../resources/web, which is what the CMakeLists globs into
 * WEB_RESOURCES and iPlug2 copies into the plugin bundle.
 *
 * EVERYTHING IS INLINED into one index.html. A WKWebView loading the page over
 * a custom scheme is not a web server: relative asset URLs resolve against a
 * bundle path, and every separate .js or .css is another chance for that to go
 * wrong silently -- a blank editor with no error anywhere. One file cannot.
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

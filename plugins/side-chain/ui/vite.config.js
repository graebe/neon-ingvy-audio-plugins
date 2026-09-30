import { defineConfig } from 'vite';
import solid from 'vite-plugin-solid';
// The notices of every package bundled into ui.js, written beside it, and a
// failed build for one THIRD_PARTY_LICENSES.md does not list.
import licenses from '../../../scripts/vite-licenses.mjs';

/*
 * EVERYTHING INLINED INTO ONE index.html, because a WKWebView on a custom
 * scheme is not a web server: a second request for assets/ui.js is a request
 * this plugin has no way to answer, and the page comes up blank with no error.
 *
 * assetsInlineLimit is absurd on purpose -- it is "never emit a separate file".
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

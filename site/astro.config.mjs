// The documentation site. Static, no framework islands yet, deployed to Pages.
//
// `base` IS THE TRAP. The site lives under /neon-ingvy-audio-plugins/, so every internal URL
// has to carry that prefix; a hand-written href="/tech/" resolves to
// graebe.github.io/tech/ and 404s on the deployed site while working perfectly
// in a local preview. Nothing here writes a leading-slash URL by hand -- see
// src/lib/paths.js -- and scripts/check-links.mjs fails the build if one
// appears anyway.
import { defineConfig } from 'astro/config';
import remarkRepoUrls from './plugins/remark-repo-urls.mjs';

export default defineConfig({
  site: 'https://graebe.github.io',
  base: '/neon-ingvy-audio-plugins',

  // Every route becomes a directory with an index.html, which is exactly what
  // GitHub Pages serves with no configuration of its own.
  trailingSlash: 'always',
  build: { format: 'directory' },

  markdown: {
    remarkPlugins: [remarkRepoUrls],
    // The ONLY Shiki theme that takes its colours from CSS variables rather
    // than baking in a palette. Anything else would put a second, unrelated
    // colour scheme on a page whose whole point is one design system.
    shikiConfig: { theme: 'css-variables' },
  },
});

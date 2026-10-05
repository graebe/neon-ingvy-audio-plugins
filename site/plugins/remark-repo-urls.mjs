/*
 * Make one Markdown file read correctly in two places.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * The content files live in the repository and are written to be read on
 * GitHub, so their links are relative paths between repository files:
 * ../ui/README.md, ../../docs/media/x.png, cmake/Schwung.cmake. Rendered on
 * this site, every one of those 404s -- and silently, because a broken link
 * looks exactly like a working one until it is clicked.
 *
 * So each is rewritten to the thing it means HERE:
 *   - a file this site publishes  -> its route
 *   - an image                    -> the staged copy under the site root
 *   - anything else in the repo   -> the file on GitHub
 *
 * Absolute URLs and bare #anchors are left alone.
 */
import { visit } from 'unist-util-visit';

const REPO = 'https://github.com/graebe/neon-ingvy-audio-plugins/blob/main';

// The routes this site actually publishes, keyed by the repository path a
// content file would link to. Everything else goes to GitHub.
const ROUTES = new Map([
  ['plugins/trance-gate/README.md', 'plugins/trance-gate/'],
  ['plugins/spectrogram/README.md', 'plugins/spectrogram/'],
  ['plugins/trance-gate/docs/live.md', 'plugins/trance-gate/#live'],
  ['plugins/trance-gate/docs/schwung.md', 'plugins/trance-gate/#schwung'],
  ['plugins/spectrogram/docs/live.md', 'plugins/spectrogram/#live'],
  ['plugins/listen-in/README.md', 'plugins/listen-in/'],
  ['plugins/listen-in/docs/live.md', 'plugins/listen-in/#live'],
  ['plugins/side-chain/README.md', 'plugins/side-chain/'],
  ['plugins/side-chain/docs/live.md', 'plugins/side-chain/#live'],
  ['plugins/side-chain/docs/schwung.md', 'plugins/side-chain/#schwung'],
  ['CHANGELOG.md', 'changelog/'],
  ['README.md', ''],
  ['THIRD_PARTY_LICENSES.md', 'licences/'],
  ['LICENSE', 'licences/'],
]);

/* Resolve a link relative to the file it appears in, as a repo-root path. */
const resolve = (from, target) => {
  const parts = from.split('/').slice(0, -1);
  for (const seg of target.split('/')) {
    if (seg === '.' || seg === '') continue;
    if (seg === '..') parts.pop();
    else parts.push(seg);
  }
  return parts.join('/');
};

export default function remarkRepoUrls() {
  return (tree, file) => {
    // The repo-root path of the file being rendered, e.g.
    // "plugins/trance-gate/docs/live.md".
    const self = (file.history?.[0] ?? '').split('/neon-ingvy-audio-plugins/').pop() ?? '';
    const base = (import.meta.env?.BASE_URL ?? '/neon-ingvy-audio-plugins/').replace(/\/*$/, '/');

    visit(tree, ['link', 'image'], (node) => {
      const url = node.url ?? '';
      if (!url || /^[a-z]+:/i.test(url) || url.startsWith('#') || url.startsWith('/')) return;

      const [path, hash = ''] = url.split('#');
      const full = resolve(self, path);

      if (ROUTES.has(full)) {
        const route = ROUTES.get(full);
        // A link that already carries its own anchor keeps it; otherwise the
        // route's own anchor (if any) stands.
        node.url = hash ? `${base}${route.split('#')[0]}#${hash}` : `${base}${route}`;
        return;
      }

      if (full.startsWith('docs/media/')) {
        node.url = `${base}${full.slice('docs/'.length)}`;
        return;
      }

      node.url = `${REPO}/${full}${hash ? `#${hash}` : ''}`;
    });
  };
}

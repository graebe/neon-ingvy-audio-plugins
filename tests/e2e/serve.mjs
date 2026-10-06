/*
 * The review harnesses, over http, for the end-to-end suite.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * WHY A SERVER AT ALL. Chrome refuses module scripts from file://, and every
 * harness loads the kit's harness/beat.js -- the fake transport that rings the
 * ground -- from a path that climbs out of the harness directory. So the pages
 * are served from the repository root, where every relative path they use
 * resolves exactly as it does for a person reviewing them by hand.
 *
 * WHY NOT A PACKAGE. Forty lines of node:http do this, and every dependency is
 * a row the licence audit has to argue about. It serves files and nothing else:
 * GET and HEAD, loopback only, never a path outside the root.
 *
 *   node tests/e2e/serve.mjs [port]     (default: $NI_E2E_PORT, else 47219)
 *
 * The port is deliberately not a default anything else uses, so a second
 * agent's dev server or a vite preview on 5173 cannot answer in its place.
 */
import { createServer } from 'node:http';
import { createReadStream, statSync } from 'node:fs';
import { extname, join, normalize } from 'node:path';
import { fileURLToPath } from 'node:url';

/* With its trailing slash, so a prefix test is a containment test. */
const ROOT = fileURLToPath(new URL('../../', import.meta.url));
const PORT = Number(process.argv[2] ?? process.env.NI_E2E_PORT ?? 47219);

const TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.json': 'application/json',
  '.svg': 'image/svg+xml',
  '.woff': 'font/woff',
  '.woff2': 'font/woff2',
  '.txt': 'text/plain; charset=utf-8',
  '.map': 'application/json',
};

const server = createServer((req, res) => {
  if (req.method !== 'GET' && req.method !== 'HEAD') {
    res.writeHead(405).end();
    return;
  }
  const url = new URL(req.url, 'http://localhost');
  /* The harnesses have no icon and Chrome asks anyway; a 404 here would be a
   * console error charged to the editor under test. */
  if (url.pathname === '/favicon.ico') {
    res.writeHead(204).end();
    return;
  }
  const path = normalize(join(ROOT, decodeURIComponent(url.pathname)));
  /* Never outside the checkout, whatever the request spells. */
  if (!path.startsWith(ROOT)) {
    res.writeHead(403).end();
    return;
  }
  let st;
  try { st = statSync(path); } catch { res.writeHead(404).end(); return; }
  if (!st.isFile()) { res.writeHead(404).end(); return; }
  res.writeHead(200, {
    'content-type': TYPES[extname(path)] ?? 'application/octet-stream',
    'content-length': st.size,
    /* Every run must see the bundle the harness build just wrote. */
    'cache-control': 'no-store',
  });
  if (req.method === 'HEAD') { res.end(); return; }
  createReadStream(path).pipe(res);
});

server.listen(PORT, '127.0.0.1', () => {
  console.log(`harnesses on http://127.0.0.1:${PORT}/`);
});

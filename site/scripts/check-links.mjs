/*
 * Every root-relative URL in the built site resolves.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * ASSERTED RATHER THAN CLAIMED, which is this repository's habit. The site is
 * served under /vst-library/, and the failure mode of getting that wrong is the
 * worst kind: `astro preview` applies the base too, so a hand-written
 * href="/tech/" is broken only on the deployed site, only after a push, and
 * only for whoever clicks it.
 *
 * So after the build: collect every href and src that starts with "/", and fail
 * unless it carries the base prefix AND names something that was actually
 * emitted. An internal link to a page that does not exist is caught here too.
 *
 *   node scripts/check-links.mjs
 */
import { readdirSync, readFileSync, existsSync, statSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join, relative } from 'node:path';

const SITE = dirname(dirname(fileURLToPath(import.meta.url)));
const DIST = join(SITE, 'dist');
const BASE = '/vst-library/';

/*
 * No build, no verdict. Exit 2 rather than 1: ctest reads that as a SKIP, so a
 * C++ developer running the suite without having built the site is told what is
 * missing instead of being handed a red test they did not cause.
 */
if (!existsSync(DIST)) {
  console.log('check-links: no site/dist -- run `npm run build --workspace site` first (skipping)');
  process.exit(2);
}

const walk = (dir, out = []) => {
  for (const name of readdirSync(dir)) {
    const p = join(dir, name);
    if (statSync(p).isDirectory()) walk(p, out);
    else if (p.endsWith('.html')) out.push(p);
  }
  return out;
};

/* A URL resolves if dist holds that file, or that directory's index.html. */
const resolves = (url) => {
  const path = decodeURIComponent(url.slice(BASE.length).split(/[?#]/)[0]);
  if (path === '') return true;
  const target = join(DIST, path);
  if (existsSync(target) && statSync(target).isFile()) return true;
  return existsSync(join(target, 'index.html'));
};

const pages = walk(DIST);
const failures = [];

for (const page of pages) {
  const html = readFileSync(page, 'utf8');
  const where = relative(DIST, page);
  for (const m of html.matchAll(/(?:href|src)="(\/[^"]*)"/g)) {
    const url = m[1];
    if (!url.startsWith(BASE)) {
      failures.push(`${where}: "${url}" does not start with ${BASE}`);
    } else if (!resolves(url)) {
      failures.push(`${where}: "${url}" resolves to nothing in dist/`);
    }
  }
}

if (failures.length) {
  console.error(`check-links: ${failures.length} broken in ${pages.length} pages\n`);
  for (const f of failures) console.error('  ' + f);
  process.exit(1);
}
console.log(`check-links: ${pages.length} pages, every root-relative URL resolves`);

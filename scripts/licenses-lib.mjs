// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What THIRD_PARTY_LICENSES.md lists, read the same way by everything that
 * checks it.
 *
 * AN ENTRY IS A TABLE ROW WHOSE FIRST CELL NAMES THE THING IN BACKTICKS:
 *
 *   | `solid-js` | **MIT**, © 2016-2025 Ryan Carniato |
 *   | `tg-core`, `tg-capi`, `tg-move` | `engines/trance-gate` | ... |
 *
 * Every backticked name in a first cell counts, so one row may list several
 * crates. Prose does not count: a name mentioned in a paragraph is not a
 * notice anyone will find. Rows are grouped by the `## ` heading above them,
 * so a check can ask what one section lists and hold it to what ships.
 */
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

export const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
export const NOTICES = join(ROOT, 'THIRD_PARTY_LICENSES.md');

/* Map of section heading -> Map of listed name -> the whole row. */
export function listedBySection(markdown = readFileSync(NOTICES, 'utf8')) {
  const sections = new Map();
  let current = sections.set('', new Map()).get('');
  for (const line of markdown.split('\n')) {
    const h = /^## (.+)$/.exec(line);
    if (h) {
      current = new Map();
      sections.set(h[1].trim(), current);
      continue;
    }
    const row = /^\|([^|]*)\|/.exec(line);
    if (!row || /^[\s:-]*$/.test(row[1])) continue;
    for (const m of row[1].matchAll(/`([^`]+)`/g)) current.set(m[1], line);
  }
  return sections;
}

/* Every listed name, whichever section it is in. */
export function listedNames(markdown) {
  const all = new Set();
  for (const names of listedBySection(markdown).values())
    for (const n of names.keys()) all.add(n);
  return all;
}

/* The one section whose heading starts with `prefix`. */
export function section(sections, prefix) {
  const hits = [...sections.keys()].filter((k) => k.startsWith(prefix));
  if (hits.length !== 1)
    throw new Error(`THIRD_PARTY_LICENSES.md: expected one "## ${prefix}..." section, found ${hits.length}`);
  return sections.get(hits[0]);
}

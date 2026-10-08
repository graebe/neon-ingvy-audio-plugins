// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What THIRD_PARTY_LICENSES.md lists, read the same way by everything that
 * checks it.
 *
 * AN ENTRY IS A TABLE ROW WHOSE FIRST CELL NAMES THE THING IN BACKTICKS:
 *
 *   | `zlib` | 1.3.2, in `juce_core` | **Zlib**, © 1995-2026 Jean-loup Gailly and Mark Adler |
 *   | `tg-core`, `tg-capi`, `tg-move` | `engines/trance-gate` | ... |
 *
 * Every backticked name in a first cell counts, so one row may list several
 * crates. Prose does not count: a name mentioned in a paragraph is not a
 * notice anyone will find, and neither does a line inside a fenced block,
 * which is where the generated Rust section quotes licence texts. Rows are
 * grouped by the `## ` heading above them, so a check can ask what one
 * section lists and hold it to what ships.
 */
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

export const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
export const NOTICES = join(ROOT, 'THIRD_PARTY_LICENSES.md');

/* Each line of the file with the `## ` heading it sits under, outside fenced
 * blocks: the one walk both readers below share. */
function* linesBySection(markdown) {
  let heading = '';
  let fenced = false;
  for (const line of markdown.split('\n')) {
    if (/^```/.test(line)) { fenced = !fenced; continue; }
    if (fenced) continue;
    const h = /^## (.+)$/.exec(line);
    if (h) heading = h[1].trim();
    yield [heading, line];
  }
}

/* An entry row's cells, or null for a header, a separator or prose. */
function entryCells(line) {
  if (!line.startsWith('|')) return null;
  const cells = line.split('|').slice(1, -1).map((c) => c.trim());
  return cells.length && /`[^`]+`/.test(cells[0]) ? cells : null;
}

/* Map of section heading -> Map of listed name -> the whole row. */
export function listedBySection(markdown = readFileSync(NOTICES, 'utf8')) {
  const sections = new Map([['', new Map()]]);
  for (const [heading, line] of linesBySection(markdown)) {
    if (!sections.has(heading)) sections.set(heading, new Map());
    if (!entryCells(line)) continue;
    for (const m of /^\|([^|]*)\|/.exec(line)[1].matchAll(/`([^`]+)`/g))
      sections.get(heading).set(m[1], line);
  }
  return sections;
}

/* The entry rows of the one section whose heading starts with `prefix`, as
 * arrays of cells -- for a section whose rows say more than a name, such as a
 * crate's version. */
export function rowsOf(prefix, markdown = readFileSync(NOTICES, 'utf8')) {
  const rows = [];
  const headings = new Set();
  for (const [heading, line] of linesBySection(markdown)) {
    if (!heading.startsWith(prefix)) continue;
    headings.add(heading);
    const cells = entryCells(line);
    if (cells) rows.push(cells);
  }
  if (headings.size !== 1)
    throw new Error(`THIRD_PARTY_LICENSES.md: expected one "## ${prefix}..." section, found ${headings.size}`);
  return rows;
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

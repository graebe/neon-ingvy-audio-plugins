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

/* Cargo's feature resolution, through normal edges only, as a build of the
 * workspace does it: every member with its default features; a dependency
 * with the features its declaration names, `default` unless it opted out, and
 * what the depending package's own features ask of it (`x/f`, and `x?/f` once
 * `x` is on). An optional dependency is on only when a feature enables it
 * strongly -- `dep:x`, `x/f`, or the implicit feature `x` -- never `x?/f`.
 *
 * Returns every package reached, with the features asked of it -- before its
 * own [features] table expands them, which happens here on each visit. Grows
 * monotonically to a fixed point, so a package is revisited only when its set
 * grew. */
export function resolveFeatures(roots, packages, nodes) {
  const bare = (t) => (t ?? '').replace(/\s+/g, '');
  const features = new Map();
  const todo = [];
  const want = (id, asked) => {
    let set = features.get(id);
    const fresh = !set;
    if (fresh) features.set(id, (set = new Set()));
    const before = set.size;
    for (const f of asked) set.add(f);
    if (fresh || set.size > before) todo.push(id);
  };
  for (const id of roots) want(id, ['default']);

  while (todo.length) {
    const id = todo.pop();
    const pkg = packages.get(id);
    const node = nodes.get(id);
    const table = pkg.features ?? {};
    const optional = new Set(pkg.dependencies.filter((d) => d.optional)
      .map((d) => d.rename ?? d.name));

    /* This package's own features, closed over the table, and what they ask
     * of its dependencies. */
    const strong = new Set();
    const asks = new Map();            // dependency -> features asked of it
    const ask = (dep, f) => (asks.get(dep) ?? asks.set(dep, new Set()).get(dep)).add(f);
    const own = new Set();
    const stack = [...features.get(id)];
    while (stack.length) {
      const f = stack.pop();
      if (own.has(f)) continue;
      own.add(f);
      if (!(f in table)) {
        if (optional.has(f)) strong.add(f);   // the implicit feature of a dependency
        continue;
      }
      for (const v of table[f]) {
        if (v.startsWith('dep:')) {
          strong.add(v.slice(4));
        } else if (v.includes('/')) {
          const [dep, feature] = v.split('/');
          if (dep.endsWith('?')) {
            ask(dep.slice(0, -1), feature);
          } else {
            strong.add(dep);
            ask(dep, feature);
          }
        } else {
          stack.push(v);
        }
      }
    }

    for (const d of node?.deps ?? []) {
      const name = packages.get(d.pkg).name;
      for (const k of d.dep_kinds) {
        if (k.kind !== null || noPlatform(k.target)) continue;
        const decls = pkg.dependencies.filter((dep) =>
          dep.kind === null && dep.name === name && bare(dep.target) === bare(k.target));
        if (!decls.length)
          throw new Error(`cargo metadata resolves ${pkg.name} -> ${name} (${k.target ?? 'every platform'}), ` +
                          `which ${pkg.name}'s manifest does not declare`);
        for (const dep of decls) {
          const local = dep.rename ?? dep.name;
          if (dep.optional && !strong.has(local)) continue;
          want(d.pkg, [
            ...dep.features,
            ...(dep.uses_default_features ? ['default'] : []),
            ...(asks.get(local) ?? []),
          ]);
        }
      }
    }
  }
  return features;
}

/* Whether a dependency's target holds on no platform, by its shape alone: a
 * cfg() with no predicate in it -- only all(), any() and not() of nothing --
 * that comes out false, as `cfg(any())` does. krates draws the line at the
 * same place. Anything that names a predicate is some platform's, and a bare
 * target triple is one. */
export function noPlatform(target) {
  const cfg = /^cfg\((.*)\)$/s.exec(target ?? '');
  const tokens = cfg?.[1].match(/[A-Za-z_][\w-]*|"[^"]*"|\S/g) ?? [];
  if (!tokens.length || tokens.some((t) => !['all', 'any', 'not', '(', ')', ','].includes(t)))
    return false;
  let i = 0;
  const expr = () => {
    const op = tokens[i];
    i += 2;                       // the operator and its '('
    const args = [];
    while (tokens[i] !== ')') {
      args.push(expr());
      if (tokens[i] === ',') i++;
    }
    i++;                          // its ')'
    return op === 'all' ? args.every(Boolean) : op === 'any' ? args.some(Boolean) : !args[0];
  };
  return !expr();
}

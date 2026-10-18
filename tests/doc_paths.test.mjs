// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Every repository path the documentation names exists.
 *
 * WHY THIS IS A TEST. The documentation is rendered as the site and read by
 * every agent that works here as its contract, and prose does not fail a
 * build: a refactor that moves a file leaves the sentence that named it
 * pointing at nothing (shell_state.h and engines/shell/include were cited for
 * weeks after they went). design_paths.test.mjs does this for design/; this
 * does it for the documents.
 *
 * WHAT IS CHECKED, in README.md, AGENTS.md and every Markdown file under
 * docs/, plugins/ and modules/ -- except the ADRs (docs/adr), which are dated
 * records and name what existed when they were decided:
 *
 *   `engines/a/b.rs`     a code span whose first segment is a top-level
 *                        directory of the repository, from the root
 *   [text](../a/b.md)    a relative Markdown link, from the file's own
 *                        directory (an #anchor and a ?query dropped)
 *
 * A span or link that names something built rather than tracked (build/,
 * target/, dist/, node_modules/, a cargo/include header) is skipped, as is a
 * placeholder (<product>, {a,b}, $VAR). A glob is checked up to its last fixed
 * directory, and a :line suffix or prose punctuation is dropped.
 *
 *   node --test tests/doc_paths.test.mjs
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { existsSync, readdirSync, readFileSync, statSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join, relative, resolve } from 'node:path';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');

const documents = () =>
  execFileSync('git', ['ls-files', '-z', '--', 'README.md', 'AGENTS.md', 'docs', 'plugins', 'modules'],
    { cwd: ROOT, encoding: 'utf8' })
    .split('\0')
    .filter((f) => f.endsWith('.md') && !f.startsWith('docs/adr/'));

/* The top-level directories git tracks: a code span starting with one of
 * them is a path from the root; anything else (`juce/Processor.h`, a header
 * cbindgen writes) is a name, not a path. */
const TOP = new Set(
  readdirSync(ROOT, { withFileTypes: true })
    .filter((d) => d.isDirectory() && !d.name.startsWith('.'))
    .map((d) => d.name)
    .filter((d) => execFileSync('git', ['ls-files', '--', d], { cwd: ROOT, encoding: 'utf8' }).length > 0),
);
const BUILT = /(^|\/)(build[^/]*|target|dist|node_modules|out|rust-lib|cargo\/include)(\/|$)/;
const PLACEHOLDER = /[<>{}$]|\.\.\./;

/* The part that has to exist: a :line suffix and prose punctuation dropped,
 * a glob cut back to the directory it searches. */
const fixedPart = (ref) => {
  const path = ref.replace(/:\d+(-\d+)?(,\d+)*$/, '').replace(/[.,:;)]+$/, '');
  const glob = path.search(/[*?]/);
  return glob < 0 ? path : path.slice(0, Math.max(0, path.lastIndexOf('/', glob)));
};

function references(file) {
  const refs = [];
  const text = readFileSync(join(ROOT, file), 'utf8');
  text.split('\n').forEach((line, i) => {
    for (const [, span] of line.matchAll(/`([^`\s]+)`/g)) {
      const first = span.split('/')[0];
      if (!span.includes('/') || !TOP.has(first)) continue;
      if (BUILT.test(span) || PLACEHOLDER.test(span)) continue;
      refs.push({ line: i + 1, path: join(ROOT, fixedPart(span)) });
    }
    for (const [, target] of line.matchAll(/\]\(([^)\s]+)\)/g)) {
      if (/^[a-z]+:/i.test(target) || target.startsWith('#') || target.startsWith('/')) continue;
      const path = target.replace(/[#?].*$/, '');
      if (!path || BUILT.test(path) || PLACEHOLDER.test(path)) continue;
      refs.push({ line: i + 1, path: resolve(dirname(join(ROOT, file)), fixedPart(path)) });
    }
  });
  return refs;
}

test('the documents are found', () => {
  const docs = documents();
  assert.ok(docs.includes('README.md') && docs.some((f) => f.startsWith('docs/tech/')),
    `found ${docs.length} documents`);
});

test('every repository path a document names exists', () => {
  const missing = [];
  for (const file of documents()) {
    if (!statSync(join(ROOT, file), { throwIfNoEntry: false })?.isFile()) continue;
    for (const { line, path } of references(file))
      if (!existsSync(path)) missing.push(`${file}:${line}  ${relative(ROOT, path)}`);
  }
  assert.deepEqual(missing, [], `paths the documentation names that do not exist:\n  ${missing.join('\n  ')}`);
});

/*
 * Every path into design/ names something that is there.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * WHY THIS IS A TEST. The design folder is a mirror of two published artifacts
 * -- the Ultraviolet system in design/scheme/project and the plugin layout
 * canvas in design/designs -- and a mirror gets moved when the artifacts do.
 * The last move (design/files/project became design/scheme/project) left
 * functional readers behind: the icon glob, the token and field oracles. They
 * spell the path three ways, and a search for one spelling misses the others:
 * the oracles read through join(ROOT, 'design', ...), and the icon glob climbs
 * to it from its own folder (../../../design/...). A comment that names a
 * vanished file is the same failure, only slower: the next reader follows it
 * to nothing.
 *
 * WHAT IS CHECKED: every line of every text file git tracks, except the files
 * under design/ (the artifacts' own, byte for byte, which name paths inside
 * THEM) and this one (whose examples are not paths). Each reference
 *
 *   design/a/b            root-relative, in code, prose or a comment
 *   ../../design/a/b      relative to the naming file's own directory, as an
 *   ./design/a/b          import, a Vite glob or a Markdown link spells it
 *   'design', 'a', 'b'    path.join() arguments, from the root as
 *                         join(ROOT, ...) takes them
 *
 * must exist once resolved. A reference ends at whitespace, a quote, a
 * bracket, a comma, a semicolon or a backslash; a full stop or colon after it
 * is prose and dropped; a glob is checked up to its last fixed directory. A
 * design/ that follows a path character (plugins/x/design/, a URL's /design/)
 * is some other folder, and not checked.
 *
 *   node --test tests/design_paths.test.mjs
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { existsSync, readFileSync, statSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join, relative, resolve } from 'node:path';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
const SELF = relative(ROOT, fileURLToPath(import.meta.url));

/* Not preceded by a path character, so plugins/x/design/ and a URL's /design/
 * are not taken for the root's folder; and the path stops at whitespace, a
 * quote, a bracket or an escape (`\n` inside a template string). */
const SLASHED = /(?<![\w./-])design\/[^\s`'"()<>\[\]{},;\\]*/g;
/* The same, climbing to it with ./ and ../ from the file's own directory --
 * how an import or a Vite glob spells it. */
const RELATIVE = /(?<![\w./-])(?:\.\.?\/)+design\/[^\s`'"()<>\[\]{},;\\]*/g;
const JOINED = /(['"])design\1(?:\s*,\s*(['"])[^'"\n]+\2)+/g;

const tracked = () =>
  execFileSync('git', ['ls-files', '-z'], { cwd: ROOT, encoding: 'utf8' })
    .split('\0')
    .filter((f) => f && !f.startsWith('design/') && f !== SELF);

/* The part of a reference that has to exist: prose punctuation trimmed off the
 * end, and a glob cut back to the directory it searches. */
const fixedPart = (ref) => {
  const path = ref.replace(/[.:]+$/, '');
  const glob = path.search(/[*?]/);
  return glob < 0 ? path : path.slice(0, path.lastIndexOf('/', glob));
};

test('the old mirror location is gone', () => {
  assert.equal(existsSync(join(ROOT, 'design', 'files')), false,
    'design/files exists: the design system lives in design/scheme/project now');
});

test('every design/ path a tracked file names exists', () => {
  const missing = [];
  for (const file of tracked()) {
    const abs = join(ROOT, file);
    // A submodule is a tracked directory, and a file deleted but not yet
    // staged is tracked and absent: neither has text to read.
    if (!statSync(abs, { throwIfNoEntry: false })?.isFile()) continue;
    const src = readFileSync(abs, 'utf8');
    if (src.includes('\0')) continue;  // binary
    src.split('\n').forEach((line, i) => {
      const refs = [
        ...[...line.matchAll(SLASHED)].map((m) => join(ROOT, fixedPart(m[0]))),
        ...[...line.matchAll(RELATIVE)].map((m) =>
          resolve(dirname(abs), fixedPart(m[0]))),
        ...[...line.matchAll(JOINED)].map((m) =>
          join(ROOT, fixedPart(m[0].match(/[^'",\s]+/g).join('/')))),
      ];
      for (const path of refs) {
        if (!existsSync(path))
          missing.push(`${file}:${i + 1}  ${relative(ROOT, path)}`);
      }
    });
  }
  assert.deepEqual(missing, [],
    `paths into design/ that do not exist:\n  ${missing.join('\n  ')}`);
});

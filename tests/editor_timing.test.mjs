/*
 * No editor behaviour may depend on display frames or page visibility.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * WHY THIS IS A RULE. An editor is iPlug2's WKWebView inside a host's window,
 * and whether WebKit calls that page visible is WebKit's guess about a window
 * it does not own. When it guesses hidden it services no requestAnimationFrame
 * at all: the Spectrogram's columns arrived and were never drawn, because its
 * repaint waited for a frame (tests/editor_host.mm caught it in a real host,
 * in every format). The plugin's messages still arrive in a hidden page, and
 * timers still run, so everything an editor does hangs off those.
 *
 * So these may not appear in the kit or in any editor's source:
 *
 *   requestAnimationFrame, cancelAnimationFrame   a display-frame clock
 *   visibilitychange, visibilityState,
 *   document.hidden                               behaviour keyed to visibility
 *
 * Comments are not code: a comment explaining the rule may name them.
 *
 *   node --test tests/editor_timing.test.mjs
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, readdirSync, statSync, existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join, relative } from 'node:path';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');

/*
 * NOT YET, AND ONLY UNTIL feature/ground-tempo MERGES. The animated ground's
 * field and its component still run on frames and pause on visibility; that
 * branch replaces both with the host's tempo and owns the conversion. Strict
 * both ways: an exempt file that no longer breaks the rule fails this test, so
 * the exemption is deleted the day it stops being needed.
 */
const EXEMPT = new Set([
  'ui-kit/src/lib/field.js',
  'ui-kit/src/components/Ground.jsx',
]);

const FORBIDDEN = [
  /\brequestAnimationFrame\b/,
  /\bcancelAnimationFrame\b/,
  /\bvisibilitychange\b/,
  /\bvisibilityState\b/,
  /\bdocument\s*\??\.\s*hidden\b/,
];

/*
 * The source with its comments blanked and everything else -- strings
 * included, because addEventListener('visibilitychange') is the usage -- kept.
 * A character scanner rather than a regex, so a `//` inside a string or a
 * regular expression is not taken for a comment.
 */
export function stripComments(src) {
  let out = '';
  let i = 0;
  let prev = '';                       /* the last significant code character */
  while (i < src.length) {
    const c = src[i];
    const n = src[i + 1];
    if (c === '/' && n === '/') {
      while (i < src.length && src[i] !== '\n') i++;
      continue;
    }
    if (c === '/' && n === '*') {
      const end = src.indexOf('*/', i + 2);
      const stop = end < 0 ? src.length : end + 2;
      out += src.slice(i, stop).replace(/[^\n]/g, ' ');
      i = stop;
      continue;
    }
    if (c === '"' || c === "'" || c === '`') {
      let j = i + 1;
      while (j < src.length && src[j] !== c) j += src[j] === '\\' ? 2 : 1;
      out += src.slice(i, j + 1);
      i = j + 1;
      prev = c;
      continue;
    }
    /* A regular expression literal, where an operand cannot be. */
    if (c === '/' && (prev === '' || '(,=:[!&|?{};+-*%<>~^'.includes(prev))) {
      let j = i + 1;
      let inClass = false;
      while (j < src.length && src[j] !== '\n') {
        if (src[j] === '\\') { j += 2; continue; }
        if (src[j] === '[') inClass = true;
        else if (src[j] === ']') inClass = false;
        else if (src[j] === '/' && !inClass) break;
        j++;
      }
      out += src.slice(i, j + 1);
      i = j + 1;
      prev = '/';
      continue;
    }
    out += c;
    if (!/\s/.test(c)) prev = c;
    i++;
  }
  return out;
}

/** Each forbidden use in `src`, as "line N: token". */
export function violations(src) {
  const lines = stripComments(src).split('\n');
  const out = [];
  lines.forEach((line, k) => {
    for (const re of FORBIDDEN) {
      const m = re.exec(line);
      if (m) out.push(`line ${k + 1}: ${m[0]}`);
    }
  });
  return out;
}

function sources(dir) {
  if (!existsSync(dir)) return [];
  const out = [];
  for (const name of readdirSync(dir)) {
    const p = join(dir, name);
    if (statSync(p).isDirectory()) out.push(...sources(p));
    else if (/\.(m?js|jsx)$/.test(name)) out.push(p);
  }
  return out;
}

const pluginsDir = join(ROOT, 'plugins');
const FILES = [
  ...sources(join(ROOT, 'ui-kit', 'src')),
  ...readdirSync(pluginsDir).flatMap((p) => sources(join(pluginsDir, p, 'ui', 'src'))),
].map((p) => relative(ROOT, p).split('\\').join('/'));

test('the scanner finds each forbidden use, and not in a comment', () => {
  assert.deepEqual(violations('const id = requestAnimationFrame(f);'),
                   ['line 1: requestAnimationFrame']);
  assert.deepEqual(violations("raf = globalThis.cancelAnimationFrame;"),
                   ['line 1: cancelAnimationFrame']);
  assert.deepEqual(violations("doc.addEventListener('visibilitychange', f);"),
                   ['line 1: visibilitychange']);
  assert.deepEqual(violations('if (document?.hidden) return;'), ['line 1: document?.hidden']);
  assert.deepEqual(violations('x = document.visibilityState;'), ['line 1: visibilityState']);
  assert.deepEqual(violations('/* requestAnimationFrame */ a();\n// document.hidden'), []);
  assert.deepEqual(violations("const u = 'http://x'; requestAnimationFrame(f);"),
                   ['line 1: requestAnimationFrame']);
  assert.deepEqual(violations('const r = /\\/\\//; requestAnimationFrame(f);'),
                   ['line 1: requestAnimationFrame']);
});

test('it reads the kit and every editor', () => {
  assert.ok(FILES.some((f) => f.startsWith('ui-kit/src/lib/')), 'no kit sources found');
  for (const p of ['trance-gate', 'side-chain', 'spectrogram', 'listen-in']) {
    assert.ok(FILES.some((f) => f.startsWith(`plugins/${p}/ui/src/`)), `no ${p} sources found`);
  }
});

test('no editor behaviour depends on display frames or page visibility', () => {
  const found = [];
  for (const f of FILES) {
    if (EXEMPT.has(f)) continue;
    for (const v of violations(readFileSync(join(ROOT, f), 'utf8'))) found.push(`${f} ${v}`);
  }
  assert.deepEqual(found, [], 'use a timer, or the plugin\'s own messages, instead');
});

test('every exemption is still needed', () => {
  for (const f of EXEMPT) {
    assert.ok(FILES.includes(f), `${f} is exempt but no longer exists -- drop it from EXEMPT`);
    assert.notDeepEqual(violations(readFileSync(join(ROOT, f), 'utf8')), [],
                        `${f} keeps the rule now -- drop it from EXEMPT`);
  }
});

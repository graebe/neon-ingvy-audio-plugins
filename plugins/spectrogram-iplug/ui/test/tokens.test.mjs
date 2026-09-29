/*
 * The token guard: no colour may be spelled outside uv.css.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * THE SAME GUARD THE TRANCE GATE CARRIES, and this window needs it MORE than
 * that one does: a canvas cannot use a CSS variable, so the intensity ramp is
 * the most natural place in either plugin for five hex triplets to be typed
 * inline and quietly stop tracking the system. lib/ramp.js reads them back out
 * of the stylesheet instead, and this test is what keeps it that way.
 *
 * THE JUCE BUILD HAD THIS AND THE WEB PORT LOST IT.
 *
 * Uv.h carried a note -- "a ctest target fails the build if a colour literal
 * appears in any other source file" -- and the reason is not tidiness. When the
 * design system moves, the diff has to be against ONE file; a colour typed into
 * a drawing routine is a value that has silently stopped tracking the system.
 *
 * It had already happened four times over by the time this was written:
 *
 *   .pad.on            the led glow, retyped inline
 *   .readout.editing   the focus ring, retyped at a different alpha
 *   Knob.jsx           the arc glow, at 0.75 against the system's 0.45
 *   .pad-dip           on-uv at a quarter alpha, as a bare rgba()
 *
 * Every one still looked right. The arc was the tell: nobody would notice 0.75
 * against 0.45 by eye, and nobody did.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readdirSync, readFileSync, statSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join, relative } from 'node:path';

const SRC = join(dirname(fileURLToPath(import.meta.url)), '..', 'src');

/* uv.css IS the token file, so it is the one place a colour may be written.
 * index.html and the vite config carry none. */
const ALLOWED = new Set(['uv.css']);

/* #rgb/#rrggbb/#rrggbbaa, and the functional notations. Not `#` in a URL or an
 * SVG id -- those are followed by a letter run that is not hex-only, which the
 * word boundary and the length classes already exclude. */
const COLOUR = /#[0-9a-fA-F]{3,8}\b|\b(?:rgba?|hsla?|hwb|lab|lch|oklab|oklch|color)\s*\(/;

/* Comments explain the tokens constantly and must not trip the guard: a line
 * whose colour sits inside a comment is prose, not a value. */
const stripComments = (text) =>
  text.replace(/\/\*[\s\S]*?\*\//g, '').replace(/^\s*\/\/.*$/gm, '');

function walk(dir, out = []) {
  for (const name of readdirSync(dir)) {
    const p = join(dir, name);
    if (statSync(p).isDirectory()) walk(p, out);
    else if (/\.(css|jsx?|mjs)$/.test(name)) out.push(p);
  }
  return out;
}

test('every colour is spelled in uv.css and nowhere else', () => {
  const offences = [];
  for (const file of walk(SRC)) {
    const rel = relative(SRC, file);
    if (ALLOWED.has(rel)) continue;
    stripComments(readFileSync(file, 'utf8')).split('\n').forEach((line, i) => {
      const m = line.match(COLOUR);
      if (m) offences.push(`${rel}:${i + 1}  ${m[0]}  --  ${line.trim().slice(0, 72)}`);
    });
  }
  assert.deepEqual(offences, [],
    `colour literals outside uv.css:\n  ${offences.join('\n  ')}\n\n` +
    'Add a token to uv.css and reference it with var(). See the note at the ' +
    'top of this file for why a literal that "looks right" is still a bug.');
});

/* The other half: a var() that no token defines renders as nothing at all --
 * an invisible element rather than an error, which is the worst failure mode
 * a stylesheet has. */
test('every var() the UI references is defined in uv.css', () => {
  /* NOT anchored to the line start: the spacing scale is declared six to a
   * line (`--s1: 4px; --s2: 8px; ...`) and an anchored pattern sees only the
   * first of them. A `var(--x)` reference cannot be mistaken for a declaration
   * because it is followed by `)` rather than `:`. */
  const tokens = new Set(
    [...readFileSync(join(SRC, 'uv.css'), 'utf8').matchAll(/(--[\w-]+)\s*:/g)]
      .map((m) => m[1]));

  const missing = new Set();
  for (const file of walk(SRC)) {
    const text = stripComments(readFileSync(file, 'utf8'));
    for (const m of text.matchAll(/var\(\s*(--[\w-]+)/g))
      if (!tokens.has(m[1])) missing.add(`${relative(SRC, file)}: ${m[1]}`);
  }
  assert.deepEqual([...missing], [], `undefined tokens:\n  ${[...missing].join('\n  ')}`);
});

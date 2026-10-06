// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The native UI's token guard: the generated tokens are current, and no other
 * source of the native UI spells a colour.
 *
 * THE JUCE EDITOR HAD THIS ONCE, the web port lost it, and the web kit got it
 * back (ui-kit/test/tokens.test.mjs, with the four literals that had crept in
 * by then). This is the same rule for the JUCE kit and editors, and the reason
 * has not changed: when the design system moves, the diff has to be against
 * ONE file. A colour typed into a paint routine is a value that has quietly
 * stopped tracking the system -- and it still looks right, which is why a
 * test has to be the one to notice.
 *
 * TWO HALVES:
 *
 *   1. plugins/_shared/ui/src/UvTokens.h and the grain tile are exactly what
 *      scripts/gen-tokens.mjs writes from the design mirror. A re-synced
 *      design that nobody regenerated from fails here, by name.
 *   2. No .h/.cpp under the kit (plugins/_shared/ui/src and its gallery) or an
 *      editor (plugins/<product>/editor) spells a colour: a 0x hex word, a
 *      juce::Colours name, a Colour or PixelARGB built from number literals,
 *      a Colour::from...() of literals or of a string, or a "#rrggbb" in a
 *      string. UvTokens.h is the one file that may.
 *
 * Comments are prose and are not read: they discuss colours constantly.
 * Colours DERIVED from tokens -- withAlpha, interpolatedWith, a ramp sampled
 * between spec-0 and spec-4 -- are not literals and pass. `juce::Colour()` is
 * the absence of a colour (transparent), not a spelling of one, and passes.
 *
 *   node --test tests/ui_tokens_native.test.mjs
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { existsSync, readdirSync, readFileSync, statSync } from 'node:fs';
import { join, relative, basename } from 'node:path';
import { ROOT, generate, stale } from '../scripts/gen-tokens.mjs';

/* ------------------------------------------------------------ fresh -- */

test('the generated tokens are what scripts/gen-tokens.mjs writes now', () => {
  const out = stale(generate()).map((p) => relative(ROOT, p));
  assert.deepEqual(out, [],
    `stale generated files:\n  ${out.join('\n  ')}\n\n` +
    'The design mirror (design/scheme/project) and these files disagree. Run\n' +
    '  node scripts/gen-tokens.mjs\nand commit what it writes.');
});

/* ------------------------------------------------------------ guard -- */

const KIT = join(ROOT, 'plugins', '_shared', 'ui');
const PLUGINS = join(ROOT, 'plugins');

/* The trees whose code draws: the kit's sources and gallery, every editor. */
function trees() {
  const out = [join(KIT, 'src'), join(KIT, 'gallery')];
  for (const p of readdirSync(PLUGINS)) {
    const editor = join(PLUGINS, p, 'editor');
    if (existsSync(editor)) out.push(editor);
  }
  return out;
}

function walk(dir, out = []) {
  if (!existsSync(dir)) return out;
  for (const name of readdirSync(dir)) {
    const p = join(dir, name);
    if (statSync(p).isDirectory()) walk(p, out);
    else if (/\.(h|hpp|cpp|mm|inl)$/.test(name)) out.push(p);
  }
  return out;
}

/* The one file that may spell a colour. */
const ALLOWED = new Set(['UvTokens.h']);

/*
 * Comments out, strings kept: a "#a259ff" in a string literal is a colour on
 * its way to being drawn, and the guard has to see it. A // inside a string
 * (a URL) must not eat the rest of the line, so strings are skipped over
 * while comments are cut; line breaks inside a block comment survive, so the
 * line numbers reported are the file's.
 */
export function stripComments(src) {
  let out = '';
  for (let i = 0; i < src.length;) {
    const c = src[i], d = src[i + 1];
    if (c === '/' && d === '*') {
      const end = src.indexOf('*/', i + 2);
      const stop = end < 0 ? src.length : end + 2;
      out += src.slice(i, stop).replace(/[^\n]/g, ' ');
      i = stop;
    } else if (c === '/' && d === '/') {
      const end = src.indexOf('\n', i);
      const stop = end < 0 ? src.length : end;
      out += ' '.repeat(stop - i);
      i = stop;
    } else if (c === '"' || c === '\'') {
      let j = i + 1;
      while (j < src.length && src[j] !== c && src[j] !== '\n') j += src[j] === '\\' ? 2 : 1;
      out += src.slice(i, j + 1);
      i = j + 1;
    } else {
      out += c;
      i += 1;
    }
  }
  return out;
}

/*
 * Each way C++ can spell a colour, with what it is called in a failure. A
 * constructor is caught as a temporary -- Colour (12, 8, 24) -- and as a
 * declaration -- Colour c (12, 8, 24) -- with or without a cast on each
 * channel.
 */
const CAST = String.raw`(?:\(\s*(?:juce::)?uint8\s*\)\s*)?`;
export const SPELLINGS = [
  ['a hex word', /\b0[xX][0-9a-fA-F]{6,8}\b/],
  ['a juce::Colours name', /\bColours\s*::\s*\w+/],
  ['a Colour of number literals',
   new RegExp(String.raw`\bColour(?:\s+\w+)?\s*[({]\s*${CAST}\d+\s*,\s*${CAST}\d+\s*,`)],
  ['a Colour of a number', /\bColour(?:\s+\w+)?\s*[({]\s*\d/],
  ['a PixelARGB of number literals', /\bPixel(?:ARGB|RGB|Alpha)(?:\s+\w+)?\s*[({]\s*\d/],
  ['Colour::from... of literals', /\bColour\s*::\s*from(?:RGB|RGBA|HSV|HSL|FloatRGBA)\s*\(\s*[-.\d]/],
  ['Colour::fromString', /\bColour\s*::\s*fromString\b/],
  ['a #rrggbb in a string', /"[^"\n]*#[0-9a-fA-F]{6}(?:[0-9a-fA-F]{2})?\b[^"\n]*"/],
];

export function offences(file, src) {
  const found = [];
  stripComments(src).split('\n').forEach((line, i) => {
    for (const [what, re] of SPELLINGS) {
      const m = line.match(re);
      if (m) {
        found.push(`${file}:${i + 1}  ${what}: ${m[0]}`);
        break;
      }
    }
  });
  return found;
}

test('no native UI source spells a colour; UvTokens.h is the only one that may', () => {
  const files = trees().flatMap((t) => walk(t));
  assert.ok(files.some((f) => basename(f) === 'UvTokens.h'),
    'UvTokens.h is not where the guard looks: the trees above have moved');
  const found = [];
  for (const f of files) {
    if (ALLOWED.has(basename(f))) continue;
    found.push(...offences(relative(ROOT, f), readFileSync(f, 'utf8')));
  }
  assert.deepEqual(found, [],
    `colours spelled outside UvTokens.h:\n  ${found.join('\n  ')}\n\n` +
    'Use a token: uv::tok::colour::<name> (or a colour derived from one, such as\n' +
    '.withAlpha()). A colour the system does not have is a design question, not a\n' +
    'literal: it goes to the design system first, then through gen-tokens.');
});

/*
 * THE GUARD GUARDED. A pattern that silently stopped matching -- a typo in a
 * regex -- would pass every tree forever, so each spelling is shown one it
 * must catch, and the comment stripper the prose it must not.
 */
test('the guard catches every spelling it lists, and no comment', () => {
  const caught = (src) => offences('x.cpp', src).length > 0;
  const must = [
    'auto c = juce::Colour (0xff060410);',
    'g.setColour (0xffefe3ff);',
    'g.setColour (juce::Colours::white);',
    'juce::Colour c (12, 8, 24);',
    'juce::Colour c { (juce::uint8) 12, (juce::uint8) 8, (juce::uint8) 24 };',
    'juce::PixelARGB p (255, 6, 4, 16);',
    'auto c = juce::Colour::fromRGB (6, 4, 16);',
    'auto c = juce::Colour::fromFloatRGBA (0.1f, 0.2f, 0.3f, 1.0f);',
    'auto c = juce::Colour::fromString ("ff060410");',
    'auto svg = "<path stroke=\\"#efe3ff\\"/>";',
  ];
  for (const src of must) assert.ok(caught(src), `missed: ${src}`);

  const mustNot = [
    '/* bg-000 is #060410, which is 0xff060410 */ int x = 1;',
    '// juce::Colours::white would be wrong here',
    'g.setColour (uv::tok::colour::ink.withAlpha (0.5f));',
    'auto clear = juce::Colour();',
    'auto mix = a.interpolatedWith (b, 0.25f);',
    'auto url = "https://example.org/a//b"; int y = 2;',
    'constexpr int kSlots = 8; auto label = "Slot #1";',
  ];
  for (const src of mustNot) assert.ok(!caught(src), `false alarm: ${src}`);
});

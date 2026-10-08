// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The documentation site's token guard: no colour is spelled outside
 * site/src/uv/tokens.css, every var() it references is defined, and the file
 * agrees with the design system it transcribes.
 *
 * WHY A GUARD. When the design system moves, the diff has to be against ONE
 * file; a colour typed into a page or a stylesheet is a value that has
 * silently stopped tracking the system. The web editors this guard was
 * written for had four by the time it arrived -- an arc glow at 0.75 against
 * the system's 0.45 among them, which no eye was going to catch. The native
 * editors have their own guard over the generated UvTokens.h
 * (tests/ui_tokens_native.test.mjs); this one is the site's.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { existsSync, readdirSync, readFileSync, statSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join, relative, basename } from 'node:path';

/* The site's own sources. Its Markdown CONTENT is not here and must not be:
 * a product's README may quote a hex triplet in prose, and that is writing
 * about a colour rather than spelling one. */
const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
const TREES = [join(ROOT, 'site', 'src')];
const TOKENS = join(ROOT, 'site', 'src', 'uv', 'tokens.css');

/* tokens.css IS the token file, so it is the one place a colour may be
 * written. */
const ALLOWED = new Set(['tokens.css']);

/* #rgb/#rrggbb/#rrggbbaa, and the functional notations. Not `#` in a URL or an
 * SVG id -- those are followed by a letter run that is not hex-only, which the
 * word boundary and the length classes already exclude. */
const COLOUR = /#[0-9a-fA-F]{3,8}\b|\b(?:rgba?|hsla?|hwb|lab|lch|oklab|oklch|color)\s*\(/;

/*
 * AND THE SHAPE A COLOUR TAKES IN A CANVAS: three channels in brackets. The
 * Spectrogram's clash overlay fell back to `[255, 176, 0]` -- amber, spelled
 * as numbers, where no hex or rgb() could catch it. Any bracketed triple of
 * 0..255 integers with a channel past 9 reads as one; a list like [0, 1, 2]
 * does not.
 */
const TRIPLE = /\[\s*(\d{1,3})\s*,\s*(\d{1,3})\s*,\s*(\d{1,3})\s*\]/g;
const colourTriple = (line) => {
  for (const m of line.matchAll(TRIPLE)) {
    const c = [m[1], m[2], m[3]].map(Number);
    if (c.every((v) => v <= 255) && c.some((v) => v > 9)) return m[0];
  }
  return null;
};

/* Comments explain the tokens constantly and must not trip the guard: a line
 * whose colour sits inside a comment is prose, not a value. */
const stripComments = (text) =>
  text.replace(/\/\*[\s\S]*?\*\//g, '').replace(/^\s*\/\/.*$/gm, '');

function walk(dir, out = []) {
  if (!existsSync(dir)) return out;
  for (const name of readdirSync(dir)) {
    const p = join(dir, name);
    if (statSync(p).isDirectory()) walk(p, out);
    /* .astro carries a <style> block, which is CSS in every way that matters
     * to this test. */
    else if (/\.(css|jsx?|mjs|astro)$/.test(name)) out.push(p);
  }
  return out;
}

test('every colour is spelled in tokens.css and nowhere else', () => {
  const offences = [];
  for (const file of TREES.flatMap((t) => walk(t))) {
    const rel = relative(ROOT, file);
    if (ALLOWED.has(basename(rel))) continue;
    stripComments(readFileSync(file, 'utf8')).split('\n').forEach((line, i) => {
      const m = line.match(COLOUR)?.[0] ?? colourTriple(line);
      if (m) offences.push(`${rel}:${i + 1}  ${m}  --  ${line.trim().slice(0, 72)}`);
    });
  }
  assert.deepEqual(offences, [],
    `colour literals outside tokens.css:\n  ${offences.join('\n  ')}\n\n` +
    'Add a token to site/src/uv/tokens.css and reference it with var(). See ' +
    'the note at the top of this file for why a literal that "looks right" ' +
    'is still a bug.');
});

/* The other half: a var() that no token defines renders as nothing at all --
 * an invisible element rather than an error, which is the worst failure mode
 * a stylesheet has. */
test('every var() the UI references is defined in tokens.css', () => {
  /* NOT anchored to the line start: the spacing scale is declared six to a
   * line (`--s1: 4px; --s2: 8px; ...`) and an anchored pattern sees only the
   * first of them. A `var(--x)` reference cannot be mistaken for a declaration
   * because it is followed by `)` rather than `:`. */
  const tokens = new Set(
    [...readFileSync(TOKENS, 'utf8').matchAll(/(--[\w-]+)\s*:/g)]
      .map((m) => m[1]));

  /*
   * A custom property a source file DECLARES counts as defined, and that is
   * not a loophole. The test above already forbids a colour literal anywhere
   * but tokens.css, so a locally declared property can only hold a size, a
   * length or a var() reaching back into the system -- and the site genuinely
   * needs sizes the system has none of, because Ultraviolet describes controls
   * in a fixed window and a scrolling document has a paragraph.
   *
   * What this does NOT permit is a second palette, which is the thing worth
   * preventing.
   */
  const files = TREES.flatMap((t) => walk(t));
  for (const file of files)
    for (const m of stripComments(readFileSync(file, 'utf8')).matchAll(/(--[\w-]+)\s*:/g))
      tokens.add(m[1]);

  const missing = new Set();
  for (const file of files) {
    const text = stripComments(readFileSync(file, 'utf8'));
    for (const m of text.matchAll(/var\(\s*(--[\w-]+)/g))
      if (!tokens.has(m[1])) missing.add(`${relative(ROOT, file)}: ${m[1]}`);
  }
  assert.deepEqual([...missing], [], `undefined tokens:\n  ${[...missing].join('\n  ')}`);
});

/*
 * ------------------------------------------------------------------------
 * THE OTHER HALF: tokens.css against the design system itself.
 *
 * The two tests above stop a colour being spelled outside tokens.css. Neither
 * says the values in tokens.css are the SYSTEM's -- so the whole UI could
 * agree with itself and disagree with Ultraviolet, which is the drift that
 * matters once a design file exists in the repository at all.
 *
 * design/scheme/project/tokens.json is that file, vendored from the published
 * system. This asserts agreement rather than generating the CSS from it: the
 * same habit as the curve and envelope oracles, and for the same reason -- a
 * generator hides a disagreement by overwriting it, where a test names it.
 */
const SYSTEM = JSON.parse(
  readFileSync(join(ROOT, 'design', 'scheme', 'project', 'tokens.json'), 'utf8'));

/** Every `--name: value` in tokens.css, comments stripped. */
const cssTokens = () => {
  const out = new Map();
  for (const m of stripComments(readFileSync(TOKENS, 'utf8'))
                    .matchAll(/(--[\w-]+)\s*:\s*([^;]+);/g))
    out.set(m[1], m[2].trim());
  return out;
};

/* The system's own names map onto the CSS custom properties one to one, except
 * where the CSS shortens a family prefix the JSON spells out. More than one
 * spelling may be right -- the system names the hairline twice, in its size
 * family and its stroke family -- so this returns candidates. */
const cssNames = (name) => [
  '--' + name,
  '--' + name.replace(/^space-/, 's'),
  '--' + name.replace(/^stroke-/, ''),
];

/*
 * COMPARE VALUES, NOT SPELLINGS, and that is not laxness -- it is the only way
 * this test can coexist with the one above it.
 *
 * tokens.css is written with var() references (`0 0 0 1px var(--uv)`) because
 * the colour guard forbids spelling a colour twice; the system's JSON has no
 * variables and writes every value out. And it writes colours as #rrggbbaa
 * where the CSS says rgba(). Both files are right; a string comparison would
 * fail on every one of them and teach us to stop looking.
 */
const expandVars = (value, css, depth = 0) =>
  depth > 8 ? value
    : value.replace(/var\(\s*(--[\w-]+)\s*\)/g,
        (whole, ref) => css.has(ref) ? expandVars(css.get(ref), css, depth + 1) : whole);

/*
 * #rgb / #rrggbb / #rrggbbaa and rgb()/rgba() to one canonical form.
 *
 * PIPE-SEPARATED, NOT COMMA-SEPARATED, because the caller splits a value on
 * commas to compare a shadow's layers one at a time -- and rgba() carries
 * commas of its own, so a comma-joined canonical form gets shattered by the
 * very split it has to survive.
 */
const canonColour = (v) => {
  let m = /^#([0-9a-f]{3,8})$/i.exec(v);
  if (m) {
    let h = m[1];
    if (h.length === 3 || h.length === 4) h = [...h].map((c) => c + c).join('');
    const [r, g, b] = [0, 2, 4].map((i) => parseInt(h.slice(i, i + 2), 16));
    const a = h.length === 8 ? parseInt(h.slice(6, 8), 16) / 255 : 1;
    return `rgb|${r}|${g}|${b}|${a.toFixed(2)}`;
  }
  m = /^rgba?\(([^)]+)\)$/i.exec(v);
  if (m) {
    const p = m[1].split(/[,\s/]+/).filter(Boolean).map(Number);
    const a = p.length > 3 ? p[3] : 1;
    return `rgb|${p[0]}|${p[1]}|${p[2]}|${a.toFixed(2)}`;
  }
  return null;
};

/*
 * Split on the commas that separate a shadow's LAYERS, not the ones inside an
 * rgba(). Expanding var(--uv-glow) puts an rgba() into the middle of the
 * value, so a naive split on every comma tore "rgba(162, 89, 255, 0.5)" into
 * four pieces and the comparison could never succeed -- which looked like a
 * real disagreement about the focus ring and was a parsing bug.
 */
const splitTop = (v) => {
  const out = [];
  let depth = 0, start = 0;
  for (let i = 0; i < v.length; i++) {
    const c = v[i];
    if (c === '(') depth++;
    else if (c === ')') depth--;
    else if (c === ',' && depth === 0) { out.push(v.slice(start, i).trim()); start = i + 1; }
  }
  out.push(v.slice(start).trim());
  return out;
};

const norm = (v, css) => {
  /*
   * Whitespace inside parentheses goes FIRST, so a function call is one token
   * however it was written. The value is later split on spaces to canonicalise
   * each piece, and "rgba(162, 89, 255, 0.5)" has spaces of its own -- leaving
   * them turns one colour into four tokens, none of which is a colour.
   */
  const x = expandVars(String(v).trim(), css).toLowerCase()
    .replace(/\s+/g, ' ')
    .replace(/\(([^)]*)\)/g, (_, inner) => '(' + inner.replace(/\s+/g, '') + ')');
  /* A bare number in the system's size family means pixels. */
  const asColour = canonColour(x);
  if (asColour) return asColour;
  /* Compare each whitespace-separated piece, so a shadow's colours canonicalise
   * inside it: "0 0 8px #a259ff80" and "0 0 8px rgba(162,89,255,.5)" agree. */
  return splitTop(x).map((part) =>
    part.split(' ').map((tok) => canonColour(tok) ?? tok.replace(/^(\d+)px$/, '$1'))
        .join(' ')).join(', ');
};

/*
 * KNOWN AND DELIBERATE DIFFERENCES, each with a reason. This list is the point
 * of the test: anything NOT here has to match, and adding to it is a decision
 * somebody makes on purpose rather than a diff nobody sees.
 */
const EXEMPT = new Map([
  ['glow-led',
   'The system states it at full alpha; the CSS draws it at 0.55, chosen ' +
   'against the JUCE original whose halo() faded with distance. Reported ' +
   'rather than reconciled -- it is a judgement about a blur, not a typo.'],
]);

test('every design-system token is in tokens.css, with the same value', () => {
  const css = cssTokens();
  const wrong = [], missing = [];

  for (const [family, block] of Object.entries(SYSTEM)) {
    if (!block || typeof block !== 'object' || !Array.isArray(block.tokens)) continue;
    for (const t of block.tokens) {
      if (EXEMPT.has(t.name)) continue;
      const keys = cssNames(t.name).filter((k) => css.has(k));
      if (!keys.length) {
        missing.push(`${t.name} (expected ${cssNames(t.name).join(' or ')})`);
        continue;
      }

      /* A colour's value may be per-theme; Ultraviolet has one theme. */
      const want = typeof t.value === 'object'
        ? Object.values(t.value)[0] : String(t.value);
      /* Any spelling of the name that agrees is agreement. */
      if (!keys.some((k) => norm(css.get(k), css) === norm(want, css)))
        wrong.push(`${keys[0]}: css ${css.get(keys[0])} vs system ${want}`);
    }
  }

  assert.deepEqual(missing, [], `tokens the system defines and tokens.css lacks:\n  ${missing.join('\n  ')}`);
  assert.deepEqual(wrong, [],
    `tokens.css disagrees with design/scheme/project/tokens.json:\n  ${wrong.join('\n  ')}\n\n` +
    'Re-vendor the design file, or change the CSS to match it. If the ' +
    'difference is deliberate, add it to EXEMPT above WITH ITS REASON.');
});

test('the deliberate differences are still deliberate', () => {
  /* An exemption for a token the system no longer defines is a stale excuse. */
  const names = new Set(Object.values(SYSTEM)
    .filter((b) => b && Array.isArray(b.tokens))
    .flatMap((b) => b.tokens.map((t) => t.name)));
  for (const [name, why] of EXEMPT) {
    assert.ok(names.has(name), `EXEMPT lists "${name}", which the system no longer defines`);
    assert.ok(why.length > 40, `EXEMPT["${name}"] needs a reason, not a note`);
  }
});

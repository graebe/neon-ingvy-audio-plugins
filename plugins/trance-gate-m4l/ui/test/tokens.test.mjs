/*
 * The M4L palette against the system's. Copyright (c) 2026 Torben Gräber. MIT.
 *
 * ui/tokens.js is the one file outside tokens.css allowed to spell a colour,
 * because mgraphics takes RGBA floats and cannot read a stylesheet. This is
 * what stops that permission becoming a second palette: every value there is
 * parsed back out of ui-kit/src/tokens.css and compared.
 *
 * It is the same arrangement as curves.js against the engine's shape table,
 * and it exists for the reason the kit's own guard gives -- a violet that is
 * slightly the wrong violet is not something an eye catches in a device row
 * 169 pixels tall, beside a plugin window showing the right one.
 *
 *   node --test ui/test/tokens.test.mjs
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import { T, TOKEN_SOURCE, PAD_DIP, UV_GLOW } from '../tokens.js';

const here = dirname(fileURLToPath(import.meta.url));
const TOKENS = process.env.TG_TOKENS_CSS
  ?? join(here, '..', '..', '..', '..', 'ui-kit', 'src', 'tokens.css');

/* --name: value; from the stylesheet, comments and all -- a declaration is
 * unambiguous even inside one, because a comment cannot contain a `;` that
 * follows a `--name:` pair in this file's style. */
const css = readFileSync(TOKENS, 'utf8');
const declared = Object.fromEntries(
  [...css.matchAll(/(--[\w-]+)\s*:\s*([^;]+);/g)]
    .map((m) => [m[1], m[2].trim()]));

const hexToRgba = (hex, a = 1) => [
  parseInt(hex.slice(1, 3), 16) / 255,
  parseInt(hex.slice(3, 5), 16) / 255,
  parseInt(hex.slice(5, 7), 16) / 255,
  a,
];

const close = (got, want, what) => {
  assert.equal(got.length, 4, `${what}: not an RGBA quad`);
  for (let i = 0; i < 4; i++)
    assert.ok(Math.abs(got[i] - want[i]) < 1e-9,
      `${what}: component ${i} is ${got[i]}, tokens.css says ${want[i]}`);
};

test('every M4L colour still equals the token it mirrors', () => {
  for (const [key, prop] of Object.entries(TOKEN_SOURCE)) {
    const value = declared[prop];
    assert.ok(value, `${prop} is not declared in tokens.css`);
    assert.match(value, /^#[0-9a-fA-F]{6}$/,
      `${prop} is "${value}" -- this test only knows #rrggbb`);
    assert.ok(T[key], `tokens.js has no entry for ${key}`);
    close(T[key], hexToRgba(value), `${key} (${prop})`);
  }
});

test('tokens.js declares no colour the system does not', () => {
  for (const key of Object.keys(T))
    assert.ok(TOKEN_SOURCE[key], `${key} has no --token it claims to mirror`);
});

/*
 * The two derived values. Both are stated as rules in tokens.css prose rather
 * than as their own custom properties, so they are checked against the token
 * they are derived FROM plus the alpha the system specifies.
 */
test('the playhead dip is on-uv at a quarter alpha, not uv-deep', () => {
  close(PAD_DIP, hexToRgba(declared['--on-uv'], 0.25), 'PAD_DIP');
  /* The rule that makes it worth a test: uv-deep is never a fill. */
  const deep = hexToRgba(declared['--uv-deep'], 0.25);
  assert.ok(PAD_DIP.slice(0, 3).join() !== deep.slice(0, 3).join(),
    'PAD_DIP must not be uv-deep -- the system reserves it for glow');
});

test('the glow is uv-deep at half alpha, matching --uv-glow', () => {
  close(UV_GLOW, hexToRgba(declared['--uv-deep'], 0.5), 'UV_GLOW');
  /* And --uv-glow itself says so, in rgba() form. */
  const m = declared['--uv-glow'].match(/rgba\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*([\d.]+)/);
  assert.ok(m, '--uv-glow is not in the functional notation this test reads');
  close(UV_GLOW, [+m[1] / 255, +m[2] / 255, +m[3] / 255, +m[4]], 'UV_GLOW vs --uv-glow');
});

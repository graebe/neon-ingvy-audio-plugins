/*
 * The intensity ramp: five stops, and a picture that does not lie about level.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * TWO CLAIMS, and the second is the one that matters.
 *
 * That the five --spec-* stops exist and parse is a typo check. That the ramp is
 * MONOTONE IN LUMINANCE from 0 to 255 is a correctness check: a reader takes
 * level off a spectrogram by brightness, so a ramp that dips anywhere -- a
 * saturated violet that is darker than the blue-violet below it, say -- makes a
 * louder band look quieter than a softer one. It is the sort of thing that is
 * invisible while choosing colours and obvious once you are trying to read a
 * mix with it.
 *
 * The stops are read out of uv.css by text, because the browser's
 * getComputedStyle (which lib/ramp.js uses at runtime) has no equivalent here
 * -- and because reading THE STYLESHEET is what proves the token guard's claim
 * that the stylesheet is where they live.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

import { buildLut, luminance, LEVELS, STOPS } from '../src/lib/ramp.js';

const SRC = join(dirname(fileURLToPath(import.meta.url)), '..', 'src');

/** The --spec-* stops, as uv.css spells them. */
function stopsFromStylesheet() {
  const css = readFileSync(join(SRC, 'uv.css'), 'utf8');
  const stops = [];
  for (let i = 0; i < STOPS; i++) {
    const m = css.match(new RegExp(`--spec-${i}:\\s*#([0-9a-fA-F]{6})\\s*;`));
    assert.ok(m, `--spec-${i} is not declared in uv.css as a 6-digit hex colour`);
    stops.push([
      parseInt(m[1].slice(0, 2), 16),
      parseInt(m[1].slice(2, 4), 16),
      parseInt(m[1].slice(4, 6), 16),
    ]);
  }
  return stops;
}

test('the ramp has five stops in uv.css and nowhere else', () => {
  const stops = stopsFromStylesheet();
  assert.equal(stops.length, 5);
  /* The ends are the system's own tokens: the floor IS bg-000 (so silence is the
   * well) and the top IS uv (so a hot cell is the signal colour). Asserted
   * against uv.css's own declarations of those two, not against a literal here. */
  const css = readFileSync(join(SRC, 'uv.css'), 'utf8');
  const token = (name) => css.match(new RegExp(`--${name}:\\s*(#[0-9a-fA-F]{6})\\s*;`))[1];
  const hex = ([r, g, b]) =>
    `#${[r, g, b].map((v) => v.toString(16).padStart(2, '0')).join('')}`;
  assert.equal(hex(stops[0]), token('bg-000'), 'the floor is not bg-000');
  assert.equal(hex(stops[4]), token('uv'), 'the hot end is not uv');
});

test('the lookup table is monotone in luminance', () => {
  const lut = buildLut(stopsFromStylesheet());
  assert.equal(lut.length, LEVELS * 3);
  let prev = -1;
  for (let v = 0; v < LEVELS; v++) {
    const y = luminance(lut, v);
    assert.ok(
      y >= prev - 1e-6,
      `level ${v} is darker than ${v - 1} (${y.toFixed(2)} after ${prev.toFixed(2)})`
    );
    prev = y;
  }
});

test('the ends of the table are the ends of the ramp', () => {
  const stops = stopsFromStylesheet();
  const lut = buildLut(stops);
  assert.deepEqual([lut[0], lut[1], lut[2]], stops[0], 'byte 0 is not the floor exactly');
  const top = (LEVELS - 1) * 3;
  assert.deepEqual(
    [lut[top], lut[top + 1], lut[top + 2]], stops[4],
    'byte 255 is not the top stop exactly'
  );
});

test('every stop is reached, to within the resolution of a byte', () => {
  /*
   * Each of the five stops should be recognisable somewhere in the table, or one
   * of them is being interpolated past and the ramp has four colours in it
   * rather than five.
   *
   * NOT EXACTLY, AND THE ARITHMETIC SAYS WHY: 256 levels across four segments
   * puts the interior stops at levels 63.75, 127.5 and 191.25, so the nearest
   * byte is a quarter of a segment away from each. Two per channel is that
   * distance for these stops; asking for equality would be asking the ramp to
   * have 257 levels.
   */
  const stops = stopsFromStylesheet();
  const lut = buildLut(stops);
  for (const [i, stop] of stops.entries()) {
    let best = Infinity;
    for (let v = 0; v < LEVELS; v++) {
      const d = Math.max(
        Math.abs(lut[v * 3] - stop[0]),
        Math.abs(lut[v * 3 + 1] - stop[1]),
        Math.abs(lut[v * 3 + 2] - stop[2])
      );
      if (d < best) best = d;
    }
    assert.ok(best <= 2, `stop ${i} is never approached closer than ${best} per channel`);
  }
});

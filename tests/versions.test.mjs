/*
 * One version per product, spelled once and checked everywhere.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * A PRODUCT'S VERSION APPEARS IN THREE OR FOUR FILES, in three languages:
 * config.h as a string AND as packed hex, module.json, and every crate of its
 * engine. Nothing held them together, and they had already come apart -- the
 * Spectrogram shipped `PLUG_VERSION_STR "1.0.0"` against crates that said
 * 0.1.0, so the plugin and the analyzer inside it disagreed about what they
 * were.
 *
 * versions.json decides; this asserts. Checked rather than GENERATED, which is
 * this repository's habit: the curve and envelope oracles pin the UI against
 * the engine's own output rather than deriving one from the other, and
 * tokens.css is tested against the design system rather than emitted from it.
 * A generator hides a disagreement by overwriting it; a test names it.
 *
 *   node --test tests/versions.test.mjs
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
const read = (...p) => readFileSync(join(ROOT, ...p), 'utf8');
const VERSIONS = JSON.parse(read('versions.json'));

/*
 * Every product, and everything that spells its version. `crates` are that
 * product's engine -- a crate belongs to exactly one product, which is also
 * why the two engines never depend on each other.
 */
const PRODUCTS = {
  'trance-gate': {
    config: 'plugins/trance-gate/config.h',
    module: 'modules/trance-gate/module.json',
    crates: ['tg-core', 'tg-capi', 'tg-move'].map(
      (c) => `engines/trance-gate/crates/${c}/Cargo.toml`),
  },
  spectrogram: {
    config: 'plugins/spectrogram/config.h',
    crates: ['spectro-core', 'spectro-capi'].map(
      (c) => `engines/spectro/crates/${c}/Cargo.toml`),
  },
};

/* iPlug2 packs the version as major<<16 | minor<<8 | patch. */
const packed = (v) => {
  const [a, b, c] = v.split('.').map(Number);
  return (a << 16) | (b << 8) | c;
};

test('versions.json names every product and nothing else', () => {
  const named = Object.keys(VERSIONS).filter((k) => !k.startsWith('__'));
  assert.deepEqual(named.sort(), Object.keys(PRODUCTS).sort(),
    'versions.json and this test disagree about what the products are');
  for (const [name, v] of Object.entries(VERSIONS)) {
    if (name.startsWith('__')) continue;
    assert.match(v, /^\d+\.\d+\.\d+(-[\w.]+)?$/, `${name}: "${v}" is not a version`);
  }
});

for (const [product, where] of Object.entries(PRODUCTS)) {
  const want = VERSIONS[product];

  test(`${product}: config.h agrees (${want})`, () => {
    const h = read(where.config);
    const str = /#define\s+PLUG_VERSION_STR\s+"([^"]+)"/.exec(h)?.[1];
    assert.equal(str, want, `PLUG_VERSION_STR in ${where.config}`);

    /* The HEX is the one a host actually compares when deciding whether a
     * saved project was made by an older build, so a stale one is worse than a
     * stale string: it is silently wrong rather than visibly wrong. */
    const hex = /#define\s+PLUG_VERSION_HEX\s+(0x[0-9a-fA-F]+)/.exec(h)?.[1];
    assert.ok(hex, `no PLUG_VERSION_HEX in ${where.config}`);
    assert.equal(Number(hex), packed(want),
      `PLUG_VERSION_HEX is ${hex}; ${want} packs to 0x${packed(want).toString(16).padStart(8, '0')}`);
  });

  if (where.module) {
    test(`${product}: module.json agrees (${want})`, () => {
      assert.equal(JSON.parse(read(where.module)).version, want);
    });
  }

  test(`${product}: every crate of its engine agrees (${want})`, () => {
    for (const path of where.crates) {
      assert.ok(existsSync(join(ROOT, path)), `${path} is missing`);
      const v = /^version\s*=\s*"([^"]+)"/m.exec(read(path))?.[1];
      assert.equal(v, want, `${path}`);
    }
  });
}

/*
 * NOT CHECKED, DELIBERATELY: release.json.
 *
 * It records what has been PUBLISHED -- the release workflow rewrites it when
 * a tag is pushed -- so it is behind the tree whenever there are unreleased
 * changes, which is most of the time. Asserting it against versions.json would
 * fail on every bump between a version being decided and the tag that ships
 * it, which is exactly when nobody wants a red build.
 *
 * What IS worth knowing is that it never gets AHEAD: a published version the
 * source does not contain means the workflow released something this tree
 * cannot rebuild.
 */
test('release.json has not published a version this tree lacks', () => {
  const rel = JSON.parse(read('release.json'));
  const tree = VERSIONS['trance-gate'];
  const num = (v) => v.split('-')[0].split('.').map(Number);
  const [a, b, c] = num(rel.version);
  const [x, y, z] = num(tree);
  const ahead = a > x || (a === x && (b > y || (b === y && c > z)));
  assert.ok(!ahead,
    `release.json publishes ${rel.version} but versions.json says ${tree} -- ` +
    'the workflow released something this tree cannot rebuild.');
});

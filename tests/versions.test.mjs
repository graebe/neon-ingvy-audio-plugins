// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * One version per product, spelled once and checked everywhere.
 *
 * A PRODUCT'S VERSION APPEARS IN SEVERAL PLACES, in several languages: the
 * plugin's build, the bundle's Info.plist and moduleinfo.json, module.json,
 * and every crate of its engine. Nothing held them together, and they had
 * already come apart once -- the Spectrogram shipped "1.0.0" against crates
 * that said 0.1.0, so the plugin and the analyzer inside it disagreed about
 * what they were.
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
 * Every product, and everything that spells its version: its plugin's build
 * (`juce`), a Schwung module's module.json and its engine's crates. `crates`
 * are that product's engine -- a crate belongs to exactly one PRODUCT engine,
 * which is also why the two product engines never depend on each other.
 *
 * BOTH `juce` AND `crates` ARE OPTIONAL, and two entries are why. Listen-In is
 * a plugin whose engine is the house transport rather than one of its own, so
 * it has a build and no crates. audio-bus is that transport: crates, and no
 * bundle, because it is not a plugin at all -- it ships INSIDE two of them.
 *
 * It is versioned here anyway. A crate whose version nothing checks is a crate
 * that will eventually disagree with itself, which is the exact failure this
 * whole file exists to prevent.
 */
const PRODUCTS = {
  /* On the JUCE shell: its build takes the version from versions.json itself
   * (cmake/NiJucePlugin.cmake), so what is checked is that it asks for its
   * own -- and, given the built bundles, what they say. */
  'trance-gate': {
    juce: { cmake: 'plugins/trance-gate/CMakeLists.txt', bundle: 'NITranceGate' },
    module: 'modules/trance-gate/module.json',
    crates: ['tg-core', 'tg-capi', 'tg-move'].map(
      (c) => `engines/trance-gate/crates/${c}/Cargo.toml`),
  },
  spectrogram: {
    juce: { cmake: 'plugins/spectrogram/CMakeLists.txt', bundle: 'NISpectrogram' },
    crates: ['spectro-core', 'spectro-recv', 'spectro-capi'].map(
      (c) => `engines/spectro/crates/${c}/Cargo.toml`),
  },
  'listen-in': {
    juce: { cmake: 'plugins/listen-in/CMakeLists.txt', bundle: 'NIListenIn' },
    /* No crates: its engine is audio-bus, which is versioned on its own below
     * because a Spectrogram will link the same library. */
  },
  'audio-bus': {
    /* No build: not a plugin. */
    crates: ['bus-core', 'bus-capi'].map(
      (c) => `engines/audio-bus/crates/${c}/Cargo.toml`),
  },
  /*
   * The ground's beat clock. Not a plugin, and unlike audio-bus not even a
   * static library of its own: ground-capi is an rlib that each product's capi
   * crate absorbs, because one archive per plugin is an invariant here (see
   * cmake/NiRust.cmake). It ships inside ALL FOUR products, which is the
   * strongest version of the reason audio-bus is listed -- a crate that
   * disagreed with itself would disagree in four places at once.
   */
  ground: {
    crates: ['ground-core', 'ground-capi'].map(
      (c) => `engines/ground/crates/${c}/Cargo.toml`),
  },
  /* The directory is `modules/side-chain` and the module ID inside it is
   * `ni-side-chain`: charlesvestal/schwung-ducker already owns `ducker` on the
   * device. The path is what this test needs; the ID is module.json's business. */
  'side-chain': {
    juce: { cmake: 'plugins/side-chain/CMakeLists.txt', bundle: 'NISideChain' },
    module: 'modules/side-chain/module.json',
    crates: ['sc-core', 'sc-capi', 'sc-move'].map(
      (c) => `engines/side-chain/crates/${c}/Cargo.toml`),
  },
};

/*
 * TWO SCHEMES, AND THE TEST HAS TO KNOW BOTH.
 *
 * The date scheme is v<YYYY.MM.DD>.<subversion> -- "v2026.09.29.1". Semver is
 * the old one and the Spectrogram is still on it; both are accepted per product
 * rather than the repository being converted in one go, because converting the
 * Spectrogram means touching files another branch is working in.
 */
const DATE_RE = /^v(\d{4})\.(\d{2})\.(\d{2})\.(\d+)$/;
const SEMVER_RE = /^\d+\.\d+\.\d+(-[\w.]+)?$/;

/*
 * EVERY SPELLING, DERIVED FROM THE ONE STRING.
 *
 * Three consumers cannot hold a date version literally, so each has a form of
 * its own -- and the point of deriving them here is that nobody has to remember
 * the rules: cmake/NiJucePlugin.cmake derives the same ones for a JUCE build.
 *
 *   display  what a DAW shows, and what versions.json says
 *   numeric  three integers: Cargo (semver) and CFBundleShortVersionString
 *   cargo    numeric plus the subversion as BUILD METADATA -- legal semver,
 *            ignored in comparison, and not lost
 *   packed   major<<16 | minor<<8 | patch: the ordering the build number keeps
 *   bundle   the packed number's three fields in decimal, for CFBundleVersion
 *   schwung  display without its "v", for module.json and release.json
 *
 * THE SUBVERSION LIVES IN THE LOW BITS OF THE PATCH: day*8+sub. A second
 * release on one day has to move the packed number or a host cannot tell it
 * from the first, and there is no fourth field. Eight a day is the ceiling and
 * a ninth is refused below rather than wrapping into the next day.
 *
 * CFBundleVersion IS THE PACKED NUMBER, SPELLED OUT: y.m.(day*8+sub), so
 * v2026.09.29.3 is 2026.9.235. It is the build number macOS and the installer
 * compare, and it had been the same "2026.9.29" for .2 and .3 -- two different
 * builds claiming one build number. It allows at most three integers, so the
 * subversion cannot be a fourth field; folding it into the third the way the
 * packed form does keeps it monotonic.
 * CFBundleShortVersionString stays the human-facing date (2026.9.29).
 *
 * THE SCHWUNG SPELLING DROPS THE "v". Schwung Manager compares a module's
 * installed module.json version with release.json's by parseInt() on each
 * dotted part, and parseInt("v2026") is NaN, read as 0 -- so with the "v" the
 * year is ignored and v2027.01.01.1 sorts before v2026.12.31.1. See
 * scripts/release.mjs, which is what writes release.json.
 */
const spellings = (v) => {
  const d = DATE_RE.exec(v);
  if (!d) {
    const [a, b, c] = v.split('.').map(Number);
    return { display: v, numeric: v, cargo: v, packed: (a << 16) | (b << 8) | c,
             bundle: v, schwung: v };
  }
  const [, y, mo, day, sub] = d.map(Number);
  const numeric = `${y}.${mo}.${day}`;
  return {
    display: v,
    numeric,
    cargo: `${numeric}+${sub}`,
    packed: (y << 16) | (mo << 8) | (day * 8 + sub),
    bundle: `${y}.${mo}.${day * 8 + sub}`,
    schwung: v.slice(1),
    sub,
    day,
  };
};
const packed = (v) => spellings(v).packed;

test('versions.json names every product and nothing else', () => {
  const named = Object.keys(VERSIONS).filter((k) => !k.startsWith('__'));
  assert.deepEqual(named.sort(), Object.keys(PRODUCTS).sort(),
    'versions.json and this test disagree about what the products are');
  for (const [name, v] of Object.entries(VERSIONS)) {
    if (name.startsWith('__')) continue;
    assert.ok(DATE_RE.test(v) || SEMVER_RE.test(v),
      `${name}: "${v}" is neither v<YYYY.MM.DD>.<sub> nor a semver`);
  }
});

/*
 * THE CEILING, ASSERTED RATHER THAN COMMENTED. day*8+sub has to stay inside a
 * byte, so the subversion cannot exceed 7 -- and a ninth release in one day
 * would otherwise wrap into the next day's number and read as OLDER than it is.
 * Refusing it here is the difference between a build that fails and a version a
 * host quietly mis-orders.
 */
test('a date version fits the packed form it has to produce', () => {
  for (const [name, v] of Object.entries(VERSIONS)) {
    if (name.startsWith('__')) continue;
    const s = spellings(v);
    if (s.sub === undefined) continue;
    assert.ok(s.sub <= 7,
      `${name}: subversion ${s.sub} cannot be packed -- eight releases a day is the ceiling`);
    const patch = s.day * 8 + s.sub;
    assert.ok(patch <= 255, `${name}: patch byte ${patch} overflows`);
  }
});

for (const [product, where] of Object.entries(PRODUCTS)) {
  const want = VERSIONS[product];

  test(`${product}: it is either a bundle or a crate, and says which`, () => {
    assert.ok(where.juce || where.crates,
      `${product} names neither a plugin build nor any crates, so ` +
      `nothing about its version is actually checked`);
  });

  if (where.juce) {
    test(`${product}: its JUCE build asks for its own version`, () => {
      const call = /ni_add_juce_plugin\(\s*(\S+)/.exec(read(where.juce.cmake))?.[1];
      assert.equal(call, product,
        `${where.juce.cmake}: ni_add_juce_plugin's first argument is the versions.json key`);
    });

    /*
     * WHAT THE BUILT BUNDLE SAYS, when there is one: NI_BUNDLES names
     * build/out in the full tier (versions_bundles). The NUMERIC forms, not
     * the display one: both plist keys are up to three integers, the date
     * for the Finder and the packed day for the build number, which the
     * installer compares -- and the date as the VST3 class's version. The
     * identifier and the executable are the bundle's name, which is what
     * a host and the release staging find it by.
     */
    const out = process.env.NI_BUNDLES;
    if (out)
    test(`${product}: the built ${where.juce.bundle}.vst3 agrees (${want})`, () => {
      const res = join(out, `${where.juce.bundle}.vst3`, 'Contents');
      const x = readFileSync(join(res, 'Info.plist'), 'utf8');
      const key = (k) => new RegExp(`<key>${k}</key>\\s*<string>([^<]*)</string>`).exec(x)?.[1];
      assert.equal(key('CFBundleShortVersionString'), spellings(want).numeric, 'CFBundleShortVersionString');
      assert.equal(key('CFBundleVersion'), spellings(want).bundle, 'CFBundleVersion');
      assert.equal(key('CFBundleExecutable'), where.juce.bundle, 'CFBundleExecutable');
      assert.ok(key('CFBundleIdentifier')?.endsWith(`.${where.juce.bundle}`),
        `CFBundleIdentifier "${key('CFBundleIdentifier')}" does not end in ".${where.juce.bundle}"`);
      const info = readFileSync(join(res, 'Resources', 'moduleinfo.json'), 'utf8');
      assert.match(info, new RegExp(`"Version":\\s*"${spellings(want).numeric.replaceAll('.', '\\.')}"`),
        'moduleinfo.json Version');
    });
  }

  if (where.module) {
    test(`${product}: module.json agrees (${want})`, () => {
      assert.equal(JSON.parse(read(where.module)).version, spellings(want).schwung,
        `${where.module}: the Schwung spelling has no "v" -- see spellings() above`);
    });
  }

  if (where.crates)
  test(`${product}: every crate of its engine agrees (${want})`, () => {
    assert.ok(where.crates.length, `${product}: an empty crate list checks nothing`);
    for (const path of where.crates) {
      assert.ok(existsSync(join(ROOT, path)), `${path} is missing`);
      const v = /^version\s*=\s*"([^"]+)"/m.exec(read(path))?.[1];
      /* CARGO IS STRICT SEMVER: no "v", no leading zeros, three components. The
       * subversion rides as build metadata, which cargo accepts and ignores. */
      assert.equal(v, spellings(want).cargo, `${path}`);
    }
  });
}

/*
 * release.json: WHAT HAS BEEN PUBLISHED TO THE SCHWUNG CATALOG.
 *
 * It is NOT held equal to versions.json. It records what has been PUBLISHED --
 * scripts/release.mjs rewrites it when a module tag is released -- so it is
 * behind the tree whenever there are unreleased changes, which is most of the
 * time, and asserting equality would fail on every bump between a version
 * being decided and the tag that ships it.
 *
 * What IS checked: that every entry is one the manager can use -- keyed by a
 * module this tree has, spelled the Schwung way, pointing at the tag and asset
 * the release workflow would have produced for that version -- and that none
 * is AHEAD of the tree, which would mean something was released that this tree
 * cannot rebuild.
 *
 * ONE LEGACY ENTRY IS ALLOWED TO BE SEMVER: trance-gate 1.0.1, published
 * before the date scheme existed and still the newest module release there is.
 * It stays until the first date-scheme tag replaces it; rewriting it by hand
 * would point the catalog at a release that does not exist.
 */
const MODULES = Object.fromEntries(Object.entries(PRODUCTS)
  .filter(([, w]) => w.module)
  .map(([product, w]) => [JSON.parse(read(w.module)).id, product]));
const PUBLISHED_SEMVER = { 'trance-gate': ['1.0.1'] };
const parts = (v) => v.split('-')[0].split('.').map(Number);
const ahead = (a, b) => {
  const [x, y] = [parts(a), parts(b)];
  for (let i = 0; i < Math.max(x.length, y.length); i++) {
    if ((x[i] ?? 0) !== (y[i] ?? 0)) return (x[i] ?? 0) > (y[i] ?? 0);
  }
  return false;
};

test('release.json: every published module entry is well formed and not ahead', () => {
  const rel = JSON.parse(read('release.json'));
  const repo = /https:\/\/github\.com\/([^/]+\/[^/]+)\/releases\/download\//
    .exec(rel.download_url ?? '')?.[1];
  assert.ok(repo, 'release.json: the top-level download_url names no GitHub release');
  assert.ok(rel.modules && typeof rel.modules === 'object',
    'release.json has no "modules" map -- the multi-module form Schwung Manager reads');

  const check = (where, product, moduleId, e) => {
    assert.ok(e.version && e.download_url, `${where}: needs version and download_url`);
    const legacy = PUBLISHED_SEMVER[product]?.includes(e.version);
    assert.ok(legacy || /^\d{4}\.\d{2}\.\d{2}\.\d+(-beta\.\d+)?$/.test(e.version),
      `${where}: "${e.version}" is not the Schwung spelling of a date version (no "v")`);
    /* Both schemes tag as <product>-v<schwung spelling>: 1.0.1 was tagged
     * trance-gate-v1.0.1, and a date version's "v" is its own. */
    const tag = `${product}-v${e.version}`;
    assert.equal(e.download_url,
      `https://github.com/${repo}/releases/download/${tag}/${moduleId}-module.tar.gz`,
      `${where}: download_url is not what the release workflow publishes for ${e.version}`);
    assert.ok(!ahead(e.version, spellings(VERSIONS[product]).schwung),
      `${where} publishes ${e.version} but versions.json says ${VERSIONS[product]} -- ` +
      'the workflow released something this tree cannot rebuild');
  };

  for (const [id, entry] of Object.entries(rel.modules)) {
    const product = MODULES[id];
    assert.ok(product, `release.json publishes "${id}", which is no module.json's id`);
    check(`modules.${id}`, product, id, entry);
    for (const [ch, e] of Object.entries(entry.channels ?? {})) {
      assert.ok(['stable', 'beta'].includes(ch), `modules.${id}: unknown channel "${ch}"`);
      check(`modules.${id}.channels.${ch}`, product, id, e);
    }
    if (entry.channels?.stable)
      assert.equal(entry.version, entry.channels.stable.version,
        `modules.${id}: the entry's version and its stable channel disagree`);
  }

  /* The top level mirrors the Trance Gate for managers that predate the
   * modules map -- see LEGACY_TOP_LEVEL in scripts/release.mjs. */
  const tg = rel.modules['trance-gate'];
  assert.ok(tg, 'release.json: no trance-gate entry for the top level to mirror');
  assert.equal(rel.version, tg.version, 'top-level version does not mirror trance-gate');
  assert.equal(rel.download_url, tg.download_url, 'top-level download_url does not mirror trance-gate');
  assert.deepEqual(rel.channels ?? {}, tg.channels ?? {}, 'top-level channels do not mirror trance-gate');
});

/*
 * AND THE PUBLISHER IS NEON INGVY, IN EVERY NAME A HOST SHOWS.
 *
 * AGENTS.md: the publisher is "Neon Ingvy" and a product is "NI <name>". The
 * company is the shell's, once for every product (cmake/NiJucePlugin.cmake);
 * the name a host lists and the bundle's are each product's. The bundle
 * identifier keeps com.graebe deliberately: changing it would orphan every
 * saved project.
 */
test('every plugin is published by Neon Ingvy under an NI name', () => {
  assert.match(read('cmake', 'NiJucePlugin.cmake'), /COMPANY_NAME\s+"Neon Ingvy"/,
    'cmake/NiJucePlugin.cmake: COMPANY_NAME');
  for (const [, where] of Object.entries(PRODUCTS)) {
    if (!where.juce) continue;
    const cmake = read(where.juce.cmake);
    assert.match(/\bNAME\s+"([^"]*)"/.exec(cmake)?.[1] ?? '', /^NI /, `${where.juce.cmake}: NAME`);
    assert.equal(/\bTARGET\s+(\S+)/.exec(cmake)?.[1], where.juce.bundle, `${where.juce.cmake}: TARGET`);
    assert.match(where.juce.bundle, /^NI[A-Z]/, `${where.juce.bundle}: a bundle is NI<Name>`);
  }
});

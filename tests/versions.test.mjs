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
import { readFileSync, existsSync, readdirSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
const read = (...p) => readFileSync(join(ROOT, ...p), 'utf8');
const VERSIONS = JSON.parse(read('versions.json'));

/*
 * Every product, and everything that spells its version. `crates` are that
 * product's engine -- a crate belongs to exactly one PRODUCT engine, which is
 * also why the two product engines never depend on each other.
 *
 * BOTH `config` AND `crates` ARE OPTIONAL, and the two entries at the bottom
 * are why. Listen-In is a plugin whose engine is the house transport rather
 * than one of its own, so it has a config.h and no crates. audio-bus is that
 * transport: crates, and no config.h, no plists and no bundle, because it is
 * not a plugin at all -- it ships INSIDE two of them.
 *
 * It is versioned here anyway. A crate whose version nothing checks is a crate
 * that will eventually disagree with itself, which is the exact failure this
 * whole file exists to prevent.
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
    crates: ['spectro-core', 'spectro-recv', 'spectro-capi'].map(
      (c) => `engines/spectro/crates/${c}/Cargo.toml`),
  },
  'listen-in': {
    config: 'plugins/listen-in/config.h',
    /* No crates: its engine is audio-bus, which is versioned on its own below
     * because a Spectrogram will link the same library. */
  },
  'audio-bus': {
    /* No config: not a plugin. */
    crates: ['bus-core', 'bus-capi'].map(
      (c) => `engines/audio-bus/crates/${c}/Cargo.toml`),
  },
  /* The directory is `modules/side-chain` and the module ID inside it is
   * `ni-side-chain`: charlesvestal/schwung-ducker already owns `ducker` on the
   * device. The path is what this test needs; the ID is module.json's business. */
  'side-chain': {
    config: 'plugins/side-chain/config.h',
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
 * the rules. See the long note in plugins/trance-gate/config.h.
 *
 *   display  what a DAW shows, and what versions.json says
 *   numeric  three integers, for Cargo (semver) and the plists (CFBundle*)
 *   cargo    numeric plus the subversion as BUILD METADATA -- legal semver,
 *            ignored in comparison, and not lost
 *   packed   major<<16 | minor<<8 | patch, which is what a host compares
 *
 * THE SUBVERSION LIVES IN THE LOW BITS OF THE PATCH: day*8+sub. A second
 * release on one day has to move the packed number or a host cannot tell it
 * from the first, and there is no fourth field. Eight a day is the ceiling and
 * a ninth is refused below rather than wrapping into the next day.
 */
const spellings = (v) => {
  const d = DATE_RE.exec(v);
  if (!d) {
    const [a, b, c] = v.split('.').map(Number);
    return { display: v, numeric: v, cargo: v, packed: (a << 16) | (b << 8) | c };
  }
  const [, y, mo, day, sub] = d.map(Number);
  const numeric = `${y}.${mo}.${day}`;
  return {
    display: v,
    numeric,
    cargo: `${numeric}+${sub}`,
    packed: (y << 16) | (mo << 8) | (day * 8 + sub),
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
    assert.ok(where.config || where.crates,
      `${product} names neither a config.h nor any crates, so nothing about ` +
      `its version is actually checked`);
  });

  if (where.config)
  test(`${product}: config.h agrees (${want})`, () => {
    const h = read(where.config);
    const str = /#define\s+PLUG_VERSION_STR\s+"([^"]+)"/.exec(h)?.[1];
    assert.equal(str, spellings(want).display,
      `PLUG_VERSION_STR in ${where.config}`);

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
      assert.equal(JSON.parse(read(where.module)).version, spellings(want).display);
    });
  }

  /*
   * THE Info.plists ARE A FOURTH SPELLING, and they are literal -- iPlug2
   * substitutes nothing into them, so they stay at whatever they were
   * generated with. They did: both products' plists still said 1.0.0 after
   * versions.json moved, and the Spectrogram's had said 1.0.0 all along while
   * its config.h said 0.1.0.
   *
   * This is what the OS and the Finder report, and `AudioUnit Version` is what
   * an AU host compares -- so a stale plist is a plugin that tells the host one
   * version and the user another.
   */
  if (where.config)
  test(`${product}: every Info.plist agrees (${want})`, () => {
    const dir = join(ROOT, dirname(where.config), 'resources');
    const plists = readdirSync(dir).filter((f) => f.endsWith('.plist'));
    assert.ok(plists.length, `no plists in ${dir}`);

    for (const f of plists) {
      const x = readFileSync(join(dir, f), 'utf8');
      const key = (k) =>
        new RegExp(`<key>${k}</key>\\s*<string>([^<]*)</string>`).exec(x)?.[1];

      /*
       * THE NUMERIC FORM, NOT THE DISPLAY ONE. CFBundleShortVersionString is
       * specified as up to three integers, so "v2026.09.29.1" does not belong
       * in it -- the Finder and the installer read these.
       */
      for (const k of ['CFBundleShortVersionString', 'CFBundleVersion']) {
        const got = key(k);
        if (got !== undefined) assert.equal(got, spellings(want).numeric, `${f}: ${k}`);
      }
      const au = key('AudioUnit Version');
      if (au !== undefined)
        assert.equal(Number(au), packed(want),
          `${f}: AudioUnit Version is ${au}; ${want} packs to 0x${packed(want).toString(16).padStart(8, '0')}`);

      /*
       * AND THE AudioComponents DICT'S OWN version, WHICH IS THE ONE A HOST
       * READS.
       *
       * "AudioUnit Version" above is a second spelling of the same number and
       * only this one reaches a host's component registry -- so the two can
       * disagree, and they did: the dict said 65536 (0x00010000) while the key
       * above it had been kept current through three version bumps. A stale
       * number here is a plugin that tells Logic it is version 1.0.0 whatever
       * else it says, which is the kind of wrong that shows up as "my host will
       * not pick up the new build".
       */
      const comp = /<key>AudioComponents<\/key>[\s\S]*?<key>version<\/key>\s*<integer>(\d+)<\/integer>/
        .exec(x)?.[1];
      if (comp !== undefined)
        assert.equal(Number(comp), packed(want),
          `${f}: AudioComponents version is ${comp}, wanted ${packed(want)}`);

      /* The human-readable one, so it carries the DISPLAY string as written --
       * a date version already begins with its own "v". */
      const info = key('CFBundleGetInfoString');
      if (info !== undefined) {
        const d = spellings(want).display;
        const wanted = d.startsWith('v') ? d : `v${d}`;
        assert.ok(info.includes(wanted), `${f}: CFBundleGetInfoString says "${info}"`);
      }
    }
  });

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

/*
 * THE AU's FACTORY SYMBOL, WHICH IS NOT A VERSION BUT FAILS THE SAME WAY.
 *
 * The AudioComponents dict names the factory function the host calls, by symbol
 * name, and config.h's AUV2_FACTORY is what the binary exports. Nothing else
 * checks that those two agree: the build succeeds either way, and the failure is
 * a host that scans the component, lists it, and then cannot instantiate it.
 *
 * It was found by renaming the plugin -- config.h moved and the plist did not --
 * and the only reason it did not ship is that the AU render test happened to load
 * an older bundle still installed under the same four-character IDs. That is a
 * warning about the test, not a defence of it, so this is checked from the source
 * instead.
 */
test('the AU plist names the factory the binary exports', () => {
  for (const [product, where] of Object.entries(PRODUCTS)) {
    /* audio-bus is crates-only -- no config.h, no plists, no bundle. */
    if (!where.config) continue;
    const dir = join(ROOT, dirname(where.config), 'resources');
    const au = readdirSync(dir).filter((f) => f.endsWith('-AU-Info.plist'));
    if (!au.length) continue;                      /* the Spectrogram has one too */
    const h = read(where.config);
    const factory = /#define\s+AUV2_FACTORY\s+(\w+)/.exec(h)?.[1];
    assert.ok(factory, `${where.config}: no AUV2_FACTORY`);
    for (const f of au) {
      const x = readFileSync(join(dir, f), 'utf8');
      const named = /<key>factoryFunction<\/key>\s*<string>([^<]*)<\/string>/.exec(x)?.[1];
      assert.equal(named, factory,
        `${f} calls ${named}, config.h exports ${factory} -- the host would list it and fail to open it`);
    }
  }
});

/*
 * AND THE BUNDLE IDENTIFIERS END IN BUNDLE_NAME.
 *
 * iPlug2 builds the identifier as DOMAIN.MFR.<type>.BUNDLE_NAME and the AU looks
 * its own bundle up by it to find the Cocoa view -- config.h says a mismatch
 * returns NULL from CFBundleCopyBundleURL and segfaults the host at "VERIFYING
 * CUSTOM UI". BUNDLE_NAME also has to equal the CMake target, because that is
 * what names these plists. One rename, four places, and nothing was checking.
 */
test('every bundle identifier ends in BUNDLE_NAME', () => {
  for (const [product, where] of Object.entries(PRODUCTS)) {
    if (!where.config) continue;                   /* crates-only, see above */
    const h = read(where.config);
    const bundle = /#define\s+BUNDLE_NAME\s+"([^"]+)"/.exec(h)?.[1];
    assert.ok(bundle, `${where.config}: no BUNDLE_NAME`);
    const dir = join(ROOT, dirname(where.config), 'resources');
    for (const f of readdirSync(dir).filter((n) => n.endsWith('.plist'))) {
      /* The plists are named <target>-<FORMAT>-Info.plist, and the target IS
       * BUNDLE_NAME -- so the filename is the first thing that must agree. */
      assert.ok(f.startsWith(`${bundle}-`),
        `${f} is not named for BUNDLE_NAME "${bundle}"`);
      const x = readFileSync(join(dir, f), 'utf8');
      const id = /<key>CFBundleIdentifier<\/key>\s*<string>([^<]*)<\/string>/.exec(x)?.[1];
      if (id !== undefined)
        assert.ok(id.endsWith(`.${bundle}`),
          `${f}: CFBundleIdentifier "${id}" does not end in ".${bundle}"`);
      const exe = /<key>CFBundleExecutable<\/key>\s*<string>([^<]*)<\/string>/.exec(x)?.[1];
      if (exe !== undefined) assert.equal(exe, bundle, `${f}: CFBundleExecutable`);
    }
  }
});

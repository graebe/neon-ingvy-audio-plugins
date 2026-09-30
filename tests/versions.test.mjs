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
  /*
   * The ground's kick detector. Not a plugin, and unlike audio-bus not even a
   * static library of its own: ground-capi is an rlib that each product's capi
   * crate absorbs, because one archive per plugin is an invariant here (see
   * cmake/NiPlugin.cmake). It ships inside ALL FOUR products, which is the
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
 *   numeric  three integers: Cargo (semver) and CFBundleShortVersionString
 *   cargo    numeric plus the subversion as BUILD METADATA -- legal semver,
 *            ignored in comparison, and not lost
 *   packed   major<<16 | minor<<8 | patch, which is what a host compares
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
 * packed hex already does keeps it monotonic and makes it the SAME number the
 * AU's version and PLUG_VERSION_HEX carry, rather than a third encoding.
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
      assert.equal(JSON.parse(read(where.module)).version, spellings(want).schwung,
        `${where.module}: the Schwung spelling has no "v" -- see spellings() above`);
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
       * THE NUMERIC FORMS, NOT THE DISPLAY ONE. Both keys are specified as up
       * to three integers, so "v2026.09.29.1" belongs in neither -- the Finder
       * and the installer read these. The short string is the date; the build
       * number carries the subversion too (see spellings() above).
       */
      const short = key('CFBundleShortVersionString');
      if (short !== undefined)
        assert.equal(short, spellings(want).numeric, `${f}: CFBundleShortVersionString`);
      const build = key('CFBundleVersion');
      if (build !== undefined)
        assert.equal(build, spellings(want).bundle, `${f}: CFBundleVersion`);
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
 * AND THE VIEW CLASS THE AU PLIST NAMES IS THE ONE THE BINARY DEFINES.
 *
 * An AUv2 with a Cocoa editor is asked for its view by class NAME: the plist's
 * NSPrincipalClass is looked up in the bundle, and config.h's
 * AUV2_VIEW_CLASS_STR is what iPlug2 actually registers. The Trance Gate's
 * plist said TranceGate_View for as long as the bundle has been NITranceGate --
 * a rename that moved config.h and not the plist, exactly like the factory
 * above, and just as invisible to a build.
 */
test('the AU plist names the view class the binary defines', () => {
  for (const [, where] of Object.entries(PRODUCTS)) {
    if (!where.config) continue;
    const dir = join(ROOT, dirname(where.config), 'resources');
    const h = read(where.config);
    const view = /#define\s+AUV2_VIEW_CLASS_STR\s+"([^"]+)"/.exec(h)?.[1];
    const sym = /#define\s+AUV2_VIEW_CLASS\s+(\w+)/.exec(h)?.[1];
    for (const f of readdirSync(dir).filter((n) => n.endsWith('-AU-Info.plist'))) {
      assert.ok(view, `${where.config}: no AUV2_VIEW_CLASS_STR`);
      assert.equal(sym, view, `${where.config}: AUV2_VIEW_CLASS and its _STR disagree`);
      const x = readFileSync(join(dir, f), 'utf8');
      const principal = /<key>NSPrincipalClass<\/key>\s*<string>([^<]*)<\/string>/.exec(x)?.[1];
      assert.equal(principal, view,
        `${f}: NSPrincipalClass is ${principal}, config.h registers ${view}`);
    }
  }
});

/*
 * A PLUGIN ON THE SHARED-MEMORY TRANSPORT IS NOT sandboxSafe.
 *
 * sandboxSafe tells a host it may load the AU inside its sandboxed
 * out-of-process host (Logic, GarageBand, AUv3 hosts). audio-bus is POSIX
 * shared memory -- shm_open on a name every NI plugin agrees on -- and a
 * sandboxed process may not open a name outside its own app group. So in a
 * sandbox the send side publishes into nothing and the receive side reads an
 * empty bus, silently. Claiming sandboxSafe=true for those plugins promised a
 * host something that plugin cannot do; false makes the host load it in-process
 * (or not at all), which is the honest answer. An app-group name would be the
 * alternative, and it cannot be shared with hosts that do not sandbox.
 *
 * Which plugins are on the bus is read from the shells' own includes rather
 * than listed, so a new one cannot miss this.
 */
const BUS_HEADERS = ['audio_bus.h', 'spectro_recv.h'];
test('an AU that opens the shared-memory bus does not claim sandboxSafe', () => {
  for (const [, where] of Object.entries(PRODUCTS)) {
    if (!where.config) continue;
    const pdir = join(ROOT, dirname(where.config));
    const onBus = readdirSync(pdir)
      .filter((n) => /\.(h|cpp)$/.test(n))
      .some((n) => BUS_HEADERS.some((h) => readFileSync(join(pdir, n), 'utf8').includes(`#include "${h}"`)));
    if (!onBus) continue;
    const dir = join(pdir, 'resources');
    for (const f of readdirSync(dir).filter((n) => n.endsWith('-AU-Info.plist'))) {
      const x = readFileSync(join(dir, f), 'utf8');
      const safe = /<key>sandboxSafe<\/key>\s*<(true|false)\/>/.exec(x)?.[1];
      assert.equal(safe, 'false', `${f}: the plugin opens the shm bus, so it is not sandboxSafe`);
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

/*
 * AND THE PUBLISHER IS NEON INGVY, IN EVERY STRING A HOST SHOWS.
 *
 * AGENTS.md: the publisher is "Neon Ingvy" and a product is "NI <name>".
 * PLUG_MFR is what a DAW groups the plugin under and AAX_PLUG_MFR_STR is the
 * same thing for Pro Tools; the Side-Chain's AAX string still said "graebe"
 * after the others had moved. BUNDLE_MFR is deliberately NOT checked: it is a
 * component of the bundle identifier, and changing it would orphan every saved
 * project (see listen-in/config.h).
 */
test('every plugin is published by Neon Ingvy under an NI name', () => {
  for (const [, where] of Object.entries(PRODUCTS)) {
    if (!where.config) continue;
    const h = read(where.config);
    const str = (k) => new RegExp(`#define\\s+${k}\\s+"([^"]*)"`).exec(h)?.[1];
    assert.equal(str('PLUG_MFR'), 'Neon Ingvy', `${where.config}: PLUG_MFR`);
    const aax = str('AAX_PLUG_MFR_STR');
    if (aax !== undefined) assert.equal(aax, 'Neon Ingvy', `${where.config}: AAX_PLUG_MFR_STR`);
    assert.match(str('PLUG_NAME') ?? '', /^NI /, `${where.config}: PLUG_NAME`);
  }
});

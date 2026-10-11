// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The release plumbing: what a tag means, what gets built for it, and what is
 * written back to release.json.
 *
 * NONE OF THIS CAN RUN WHERE IT MATTERS BEFORE IT MATTERS. A release workflow
 * runs on a tag push and nowhere else, so its tag parsing was last exercised by
 * the release it broke: release-schwung.yml stripped "trance-gate-v" and then
 * compared "2026.09.29.3" against a module.json and a versions.json that both
 * said "v2026.09.29.3" -- a check no correct tag could pass. The parsing now
 * lives in scripts/release.mjs, and this runs it against the real tree.
 *
 *   node --test tests/release.test.mjs
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  readFileSync, readdirSync, existsSync, mkdtempSync, mkdirSync, cpSync, writeFileSync, rmSync,
} from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { execFileSync } from 'node:child_process';
import {
  ROOT, resolve, readModuleEnv, updateReleaseJson, schwungVersion, LEGACY_TOP_LEVEL, bundleOf, ZIP_OS,
  VERSION_RE, compareVersions, schwungManagerNewer, latestStableRelease, publishedReleases,
  NOTES_REF, attest, attestations, attestationFor, attested,
} from '../scripts/release.mjs';
import { plan, stage, binaryOf } from '../scripts/stage-release.mjs';
import { juceProducts, bundleResources } from '../scripts/licenses-lib.mjs';

const read = (...p) => readFileSync(join(ROOT, ...p), 'utf8');
const VERSIONS = JSON.parse(read('versions.json'));
const PRODUCTS = Object.keys(VERSIONS).filter((k) => !k.startsWith('__'));
const MODULES = readdirSync(join(ROOT, 'modules'))
  .filter((d) => existsSync(join(ROOT, 'modules', d, 'module.env')));
/* A product with a plugin: its build calls ni_add_juce_plugin
 * (scripts/release.mjs, bundleOf). */
const PLUGINS = readdirSync(join(ROOT, 'plugins'))
  .filter((d) => !d.startsWith('_') && bundleOf(ROOT, d));

/* ------------------------------------------------------------ the modules */

test('every module directory is configured for the shared scripts', () => {
  const dirs = readdirSync(join(ROOT, 'modules'), { withFileTypes: true })
    .filter((d) => d.isDirectory() && !d.name.startsWith('_')).map((d) => d.name);
  assert.deepEqual(dirs.sort(), [...MODULES].sort(),
    'a module directory without module.env cannot be built by modules/_shared/package.sh');
});

const members = [...read('Cargo.toml').matchAll(/"(engines\/[^"]+)"/g)].map((m) => m[1]);

for (const m of MODULES) {
  test(`modules/${m}: module.env agrees with module.json and the workspace`, () => {
    const env = readModuleEnv(ROOT, m);
    const json = JSON.parse(read('modules', m, 'module.json'));
    assert.ok(PRODUCTS.includes(m),
      `modules/${m} is not a product in versions.json, so no tag can release it`);
    assert.equal(env.MODULE_ID, json.id, 'MODULE_ID must be the catalog id');
    /* The chain host loads modules/audio_fx/<id>/<id>.so and never reads
     * "dsp"; they are kept equal so the field does not lie either. */
    assert.equal(json.dsp, `${json.id}.so`, 'module.json "dsp"');
    assert.ok(env.MODULE_TITLE, 'MODULE_TITLE');
    assert.equal(json.name, env.MODULE_TITLE, 'module.json "name" and MODULE_TITLE');
    assert.equal(json.author, 'Neon Ingvy', 'the publisher is Neon Ingvy');
    assert.equal(json.license, 'GPL-3.0-or-later', 'module.json "license" is the repository licence');

    const crate = members.find((p) => p.endsWith(`/${env.MODULE_CRATE}`));
    assert.ok(crate, `MODULE_CRATE ${env.MODULE_CRATE} is not a workspace member`);
    const toml = read(crate, 'Cargo.toml');
    assert.match(toml, new RegExp(`^name\\s*=\\s*"${env.MODULE_CRATE}"`, 'm'));
    /* package.sh copies lib<crate with _>.so, which only a cdylib produces. */
    assert.match(toml, /crate-type\s*=\s*\[[^\]]*"cdylib"/, `${env.MODULE_CRATE} is not a cdylib`);

    /* What modules/_shared/test.sh runs on aarch64: workspace members, the
     * vtable crate among them, and never a capi crate -- asked for by name it
     * is built with its plugin-only `shell` feature, which would then reach
     * the vtable crate's test build too. */
    const tested = (env.MODULE_TEST_CRATES ?? '').split(/\s+/).filter(Boolean);
    assert.ok(tested.includes(env.MODULE_CRATE), `MODULE_TEST_CRATES must include ${env.MODULE_CRATE}`);
    for (const c of tested) {
      assert.ok(members.some((p) => p.endsWith(`/${c}`)), `MODULE_TEST_CRATES: ${c} is not a workspace member`);
      assert.ok(!c.endsWith('-capi'), `MODULE_TEST_CRATES: ${c} would test the plugin's build of it`);
    }
    if (env.MODULE_TEST_SCRIPT) {
      assert.ok(existsSync(join(ROOT, env.MODULE_TEST_SCRIPT)), `MODULE_TEST_SCRIPT ${env.MODULE_TEST_SCRIPT}`);
    }
  });
}

/* ------------------------------------------------------------- the tags */

const tagOf = (p) => `${p}-${VERSIONS[p]}`;

for (const p of PRODUCTS) {
  const shipsSomething = PLUGINS.includes(p) || MODULES.includes(p);
  if (!shipsSomething) continue;   /* audio-bus, ground: crates inside products */
  test(`${p}: the tag for the current version resolves`, () => {
    const r = resolve(tagOf(p));
    assert.equal(r.product, p);
    assert.equal(r.version, VERSIONS[p]);
    if (PLUGINS.includes(p)) {
      assert.ok(r.bundle);
      assert.equal(r.zip_macos, `${p}-${VERSIONS[p]}-macOS.zip`);
      assert.deepEqual(Object.keys(r).filter((k) => k.startsWith('zip_')), ['zip_macos'],
        'a release is the macOS zip only');
    }
    if (MODULES.includes(p)) {
      assert.equal(r.module_version, schwungVersion(VERSIONS[p]));
      assert.equal(r.module_asset, `${readModuleEnv(ROOT, p).MODULE_ID}-module.tar.gz`);
    }
  });
}

test('a tag that disagrees with the tree is refused, and says why', () => {
  /* The exact bug: the product prefix AND the version's own "v" stripped. */
  assert.throws(() => resolve(`trance-gate-${VERSIONS['trance-gate'].slice(1)}`), /versions\.json says/);
  assert.throws(() => resolve('trance-gate-v1999.01.01.1'), /versions\.json says/);
  assert.throws(() => resolve('no-such-product-v2026.09.29.1'), /names no product/);
  /* A crate-only product has nothing to release. */
  assert.throws(() => resolve(tagOf('audio-bus')), /nothing to release/);
});

test('a module.json the tag does not match stops the release', () => {
  const dir = mkdtempSync(join(tmpdir(), 'ni-release-'));
  cpSync(join(ROOT, 'versions.json'), join(dir, 'versions.json'));
  cpSync(join(ROOT, 'modules'), join(dir, 'modules'), { recursive: true });
  const mj = join(dir, 'modules', 'side-chain', 'module.json');
  const json = JSON.parse(readFileSync(mj, 'utf8'));
  json.version = VERSIONS['side-chain'];          /* the display spelling, "v" and all */
  writeFileSync(mj, JSON.stringify(json));
  assert.throws(() => resolve(tagOf('side-chain'), dir), /module\.json says/);
});

/*
 * THE WORKFLOWS' TRIGGERS ARE THE SAME LISTS. A product that gains a module
 * but not a tag pattern would build fine and never release; this is where
 * that shows up.
 */
const tagPatterns = (wf) => {
  const y = read('.github', 'workflows', wf);
  const block = /\n\s+tags:\n((?:\s+- '[^']+'\n)+)/.exec(y)?.[1] ?? '';
  return [...block.matchAll(/- '([^']+)-v\*'/g)].map((m) => m[1]).sort();
};

test('release-schwung.yml triggers on exactly the products with a module', () => {
  assert.deepEqual(tagPatterns('release-schwung.yml'), [...MODULES].sort());
});

test('release-plugins.yml triggers on exactly the products with a plugin', () => {
  assert.deepEqual(tagPatterns('release-plugins.yml'), [...PLUGINS].sort());
});

/* --------------------------------------------------------- release.json */

const REPO = 'graebe/neon-ingvy-audio-plugins';

test('releasing every module on top of today\'s release.json keeps its shape', () => {
  let rel = JSON.parse(read('release.json'));
  for (const m of MODULES) rel = updateReleaseJson(rel, resolve(tagOf(m)), REPO, tagOf(m));

  for (const m of MODULES) {
    const id = readModuleEnv(ROOT, m).MODULE_ID;
    const want = schwungVersion(VERSIONS[m]);
    const url = `https://github.com/${REPO}/releases/download/${tagOf(m)}/${id}-module.tar.gz`;
    assert.deepEqual(rel.modules[id], {
      version: want, download_url: url,
      channels: { stable: { version: want, download_url: url } },
    });
    /* No "v": Schwung Manager's parseInt would read the year as 0. */
    assert.doesNotMatch(rel.modules[id].version, /^v/);
  }
  const legacy = rel.modules[readModuleEnv(ROOT, LEGACY_TOP_LEVEL).MODULE_ID];
  assert.equal(rel.version, legacy.version);
  assert.equal(rel.download_url, legacy.download_url);
  assert.deepEqual(rel.channels, legacy.channels);
});

test('a beta goes to its module\'s beta channel and moves nothing else', () => {
  /* The catalog as it would be BEFORE the Side-Chain's first release. The
   * real release.json is rewritten by every published module release, so a
   * test that read it as it stands would be testing the release history, not
   * the rule -- it broke the day v2026.10.08.2 put the Side-Chain in it. */
  const before = JSON.parse(read('release.json'));
  delete before.modules['ni-side-chain'];
  const r = { product: 'side-chain', module_id: 'ni-side-chain',
              module_version: '2026.10.01.1-beta.1', module_asset: 'ni-side-chain-module.tar.gz' };
  const first = updateReleaseJson(before, r, REPO, 'side-chain-v2026.10.01.1-beta.1');
  /* A first release that is a beta still gets a version, or the manager
   * rejects the entry outright. */
  assert.equal(first.modules['ni-side-chain'].version, '2026.10.01.1-beta.1');
  assert.ok(first.modules['ni-side-chain'].channels.beta);
  assert.equal(first.modules['ni-side-chain'].channels.stable, undefined);
  /* The Side-Chain never touches the legacy top level. */
  assert.equal(first.version, before.version);

  const stable = updateReleaseJson(first,
    { ...r, module_version: '2026.10.01.1' }, REPO, 'side-chain-v2026.10.01.1');
  const beta2 = updateReleaseJson(stable,
    { ...r, module_version: '2026.10.02.1-beta.1' }, REPO, 'side-chain-v2026.10.02.1-beta.1');
  assert.equal(beta2.modules['ni-side-chain'].version, '2026.10.01.1',
    'a beta must not move the version a channels-unaware manager reads');
  assert.equal(beta2.modules['ni-side-chain'].channels.beta.version, '2026.10.02.1-beta.1');
});

/* ------------------------------------------------------------ the staging */

/*
 * WHAT A RELEASE ZIP HOLDS, held to what a bundle is. The staging used to
 * loop over three formats and ask every bundle for a web editor; once the
 * bundles were VST3s with native editors, every release would have failed at
 * that step, on the tag. So the staging is a script (scripts/stage-release.mjs)
 * that reads what each product's build makes, and it is run here: on a bundle
 * laid out as each OS's build lays it out, and -- given NI_BUNDLES, in the
 * full tier -- as a dry run against the bundles this build made.
 */
const fakeBundle = (dir, p, os, { drop } = {}) => {
  const contents = join(dir, `${p.bundle}.vst3`, 'Contents');
  mkdirSync(join(contents, 'Resources'), { recursive: true });
  const bin = join(contents, binaryOf(p.bundle, os));
  mkdirSync(join(bin, '..'), { recursive: true });
  writeFileSync(bin, 'binary');
  for (const f of bundleResources(p)) if (f !== drop) writeFileSync(join(contents, 'Resources', f), f);
};
const universal = () => ['x86_64', 'arm64'];

/* Every OS scripts/stage-release.mjs can stage, released or not: the
 * cross-build kit stages Linux and Windows too, and they rejoin the releases
 * later. */
for (const os of ['macos', 'windows', 'linux']) {
  test(`every product stages for ${os}: the bundle as built, and the notices beside it`, () => {
    for (const p of juceProducts()) {
      const dir = mkdtempSync(join(tmpdir(), 'ni-stage-'));
      fakeBundle(join(dir, 'out'), p, os);
      const ok = plan(p.dir, join(dir, 'out'), { os, archs: universal });
      assert.deepEqual(ok.problems, [], `${p.dir} on ${os}`);
      assert.deepEqual(stage(ok, join(dir, 'stage')),
        ['LICENSE', 'THIRD_PARTY_LICENSES.md', `${p.bundle}.vst3`].sort());
      assert.ok(existsSync(join(dir, 'stage', `${p.bundle}.vst3`, 'Contents', binaryOf(p.bundle, os))));
      rmSync(dir, { recursive: true, force: true });
    }
  });
}

test('a bundle without a notice, without its binary, or not universal on macOS is refused, by name', () => {
  const p = juceProducts()[0];
  const dir = mkdtempSync(join(tmpdir(), 'ni-stage-'));
  fakeBundle(join(dir, 'a'), p, 'macos', { drop: 'AGPL-3.0.txt' });
  assert.deepEqual(plan(p.dir, join(dir, 'a'), { os: 'macos', archs: universal }).problems,
    [`${p.bundle}.vst3 has no Contents/Resources/AGPL-3.0.txt`]);
  assert.match(plan(p.dir, join(dir, 'a'), { os: 'macos', archs: () => ['arm64'] }).problems.join('\n'),
    /not universal: no x86_64 slice/);
  assert.match(plan(p.dir, join(dir, 'a'), { os: 'windows' }).problems.join('\n'),
    new RegExp(`no windows binary at Contents/x86_64-win/${p.bundle}\\.vst3`));
  assert.match(plan(p.dir, join(dir, 'missing'), { os: 'linux' }).problems.join('\n'), /is missing/);
  assert.match(plan('audio-bus', join(dir, 'a'), { os: 'linux' }).problems.join('\n'), /builds no plugin/);
  rmSync(dir, { recursive: true, force: true });
});

/* NI_UNIVERSAL says the build made both macOS slices, as the release preset
 * does; a one-slice build is checked for everything else. NI_PRODUCT names the
 * one product whose bundle is staged (release_bundle_<bundle>), as a product's
 * release builds that one alone. */
const BUILT = process.env.NI_BUNDLES;
const ONLY = process.env.NI_PRODUCT;
test(`${ONLY ?? 'every product'} stages from the bundles this build made (a dry run)`, { skip: !BUILT && 'NI_BUNDLES is not set' }, () => {
  const universal = process.platform === 'darwin' && process.env.NI_UNIVERSAL === '1';
  const products = juceProducts().filter((p) => !ONLY || p.dir === ONLY);
  assert.ok(products.length, `NI_PRODUCT=${ONLY} builds no bundle`);
  for (const p of products)
    assert.deepEqual(plan(p.dir, BUILT, { universal }).problems, [], `${p.dir} from ${BUILT}`);
});

test('release-plugins.yml stages with the script, on macOS only, and asks for no other format', () => {
  const y = read('.github', 'workflows', 'release-plugins.yml');
  assert.match(y, /node scripts\/stage-release\.mjs/);
  /* The build runs on macOS; the release job that only publishes stays on
   * Linux. */
  assert.match(y, /\n  build:\n(?:    .*\n)*?    runs-on: macos-15\n/, 'the build job does not run on macos-15');
  assert.doesNotMatch(y, /matrix\.|windows-2025/, 'release-plugins.yml still builds on more than macOS');
  assert.deepEqual(Object.keys(ZIP_OS), ['macos'], 'a release is the macOS zip only');
  assert.ok(y.includes('zip_macos'), 'no zip_macos in the workflow');
  assert.doesNotMatch(y, /zip_windows|zip_linux/, 'release-plugins.yml still names a Windows or Linux zip');
  assert.doesNotMatch(y, /\.component\b|\.clap\b|web\/index\.html|ui\.js\.LICENSE/,
    'release-plugins.yml still names a format or an editor no bundle has');
});

/*
 * ONE PRODUCT'S RELEASE BUILDS AND TESTS THAT PRODUCT. The build job used to
 * build everything and run the whole full tier, so one product's failing test
 * blocked every release. It hands the tag's product to `scripts/test.sh
 * product`, which builds that bundle and the tests labelled with it or
 * product:shared (cmake/NiTest.cmake) -- and nothing in the job builds, tests
 * or validates around it.
 */
test('release-plugins.yml builds, tests and validates the tagged product alone', () => {
  const y = read('.github', 'workflows', 'release-plugins.yml');
  const build = y.slice(y.indexOf('\n  build:\n'), y.indexOf('\n  release:\n'))
    .split('\n').filter((l) => !/^\s*#/.test(l)).join('\n');
  assert.match(build, /node scripts\/release\.mjs resolve "\$GITHUB_REF_NAME"/,
    'the build job does not resolve its tag with scripts/release.mjs');
  assert.match(build,
    /\n {8}env:\n {10}PRODUCT: \$\{\{ steps\.tag\.outputs\.product \}\}\n {8}run: scripts\/test\.sh product "\$PRODUCT"\n/,
    'the build job does not run scripts/test.sh product on the tag\'s product');
  assert.doesNotMatch(build, /cmake --(build|preset)|\bctest\b|validate-plugins\.sh/,
    'the build job builds, tests or validates outside scripts/test.sh product');
  assert.match(build, /node scripts\/stage-release\.mjs "\$PRODUCT" build\/out stage --os macos/,
    'the build job does not stage the bundle scripts/test.sh product built');
});

/*
 * A PRODUCT'S BUNDLE, AS `scripts/test.sh product` BUILDS AND VALIDATES IT:
 * the bundleOf the staging reads, so the run and the release name one bundle,
 * and a product with no plugin stops the run before it builds anything.
 */
test('release.mjs names the bundle of every plugin, and of nothing else', () => {
  const bundle = (p) => execFileSync(process.execPath, [join(ROOT, 'scripts', 'release.mjs'), 'bundle', p],
    { encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] }).trim();
  for (const p of PLUGINS) assert.equal(bundle(p), bundleOf(ROOT, p));
  assert.throws(() => bundle('audio-bus'), /has no plugin/);
  assert.throws(() => bundle('..'), /not a product/);
});

/* ------------------------------------------------------ scratch repos */

const git = (cwd, ...args) => execFileSync('git', args,
  { cwd, encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] }).trim();

/* A repository of its own, so nothing here writes a note into this one. */
function scratchRepo(files = { 'README.md': 'scratch\n' }) {
  const dir = mkdtempSync(join(tmpdir(), 'ni-attest-'));
  git(dir, 'init', '-q', '-b', 'main');
  git(dir, 'config', 'user.name', 'Release Test');
  git(dir, 'config', 'user.email', 'release-test@invalid');
  git(dir, 'config', 'commit.gpgsign', 'false');
  for (const [p, text] of Object.entries(files)) {
    mkdirSync(join(dir, p, '..'), { recursive: true });
    writeFileSync(join(dir, p), text);
  }
  git(dir, 'add', '-A');
  git(dir, 'commit', '-q', '-m', 'scratch');
  return dir;
}
const FIXED = { rustc: 'rustc 1.99.0 (test)', host: 'test-host', date: new Date('2026-10-10T12:00:00Z') };
/* What scripts/test.sh records for a run that left no unwaived gap. */
const CLEAN = { gaps: 0, waived: 'none', untested: 'none' };

/* ---------------------------------------------------------------- betas */

test('a beta orders below the release it precedes and above everything before', () => {
  const order = ['v2026.10.09.3', 'v2026.10.10.1-beta.1', 'v2026.10.10.1-beta.2', 'v2026.10.10.1',
                 'v2026.10.10.2-beta.1', 'v2026.10.10.2', 'v2026.10.11.1-beta.1'];
  for (let i = 0; i < order.length; i++)
    for (let j = 0; j < order.length; j++)
      assert.equal(compareVersions(order[i], order[j]), Math.sign(i - j), `${order[i]} vs ${order[j]}`);
  for (const bad of ['v2026.10.10.1-beta.0', 'v2026.10.10.1-rc.1', '2026.10.10.1', 'v1.0.1'])
    assert.ok(!VERSION_RE.test(bad), bad);
});

/*
 * SCHWUNG MANAGER'S ORDER, held to its own documentation
 * (docs/MODULE_CHANNELS.md: 1.3.0 > 1.3.0-beta.2 > 1.3.0-beta.1 > 1.2.3) and
 * to what it does with this repository's spelling -- right everywhere but a
 * later subversion's beta on the stable's own day, which resolve() refuses.
 */
test('Schwung Manager orders our betas as its versionNewer does', () => {
  const newer = [
    ['1.3.0', '1.3.0-beta.2'], ['1.3.0-beta.2', '1.3.0-beta.1'], ['1.3.0-beta.1', '1.2.3'],
    ['2026.10.10.1', '2026.10.10.1-beta.2'],          /* a beta user lands on stable */
    ['2026.10.10.1-beta.2', '2026.10.10.1-beta.1'],
    ['2026.10.11.1-beta.1', '2026.10.10.3'],          /* a later day's beta is offered */
    ['2026.10.10.2', '2026.10.10.1-beta.1'],
  ];
  for (const [a, b] of newer) {
    assert.ok(schwungManagerNewer(a, b), `${a} > ${b}`);
    assert.ok(!schwungManagerNewer(b, a), `${b} < ${a}`);
  }
  assert.ok(!schwungManagerNewer('2026.10.10.1', '2026.10.10.1'));
  /* The one it gets wrong: "2-beta" is not an integer to strconv.Atoi. */
  assert.ok(!schwungManagerNewer('2026.10.10.2-beta.1', '2026.10.10.1'));
});

/* A tree as resolve() reads it, with one product moved to `version`. */
const scratchTree = (product, version, release) => {
  const dir = mkdtempSync(join(tmpdir(), 'ni-beta-'));
  writeFileSync(join(dir, 'versions.json'), JSON.stringify({ ...VERSIONS, [product]: version }));
  cpSync(join(ROOT, 'modules'), join(dir, 'modules'), { recursive: true });
  mkdirSync(join(dir, 'plugins', product), { recursive: true });
  cpSync(join(ROOT, 'plugins', product, 'CMakeLists.txt'), join(dir, 'plugins', product, 'CMakeLists.txt'));
  const mj = join(dir, 'modules', product, 'module.json');
  writeFileSync(mj, JSON.stringify({ ...JSON.parse(readFileSync(mj, 'utf8')), version: schwungVersion(version) }));
  if (release) writeFileSync(join(dir, 'release.json'), JSON.stringify(release));
  return dir;
};
const stableAt = (id, version) => ({ modules: { [id]: { version, download_url: 'x', channels: { stable: { version, download_url: 'x' } } } } });

test('a beta tag resolves end to end: prerelease, its zip, its module version', () => {
  const v = 'v2026.10.11.1-beta.2';
  const dir = scratchTree('side-chain', v, stableAt('ni-side-chain', '2026.10.10.1'));
  const r = resolve(`side-chain-${v}`, dir);
  assert.equal(r.prerelease, true);
  assert.equal(r.zip_macos, `side-chain-${v}-macOS.zip`);
  assert.equal(r.module_version, '2026.10.11.1-beta.2');
  const rel = updateReleaseJson(stableAt('ni-side-chain', '2026.10.10.1'), r, REPO, `side-chain-${v}`);
  assert.equal(rel.modules['ni-side-chain'].channels.beta.version, '2026.10.11.1-beta.2');
  assert.equal(rel.modules['ni-side-chain'].version, '2026.10.10.1', 'a beta moves no stable field');
  rmSync(dir, { recursive: true, force: true });
});

test('a beta the manager would never offer is refused before it is built', () => {
  const v = 'v2026.10.10.2-beta.1';
  const dir = scratchTree('side-chain', v, stableAt('ni-side-chain', '2026.10.10.1'));
  assert.throws(() => resolve(`side-chain-${v}`, dir), /would order 2026\.10\.10\.2-beta\.1 below the stable/);
  /* Over an earlier day's stable, or with no stable at all, it is offered. */
  writeFileSync(join(dir, 'release.json'), JSON.stringify(stableAt('ni-side-chain', '2026.10.09.1')));
  assert.equal(resolve(`side-chain-${v}`, dir).prerelease, true);
  rmSync(join(dir, 'release.json'));
  assert.equal(resolve(`side-chain-${v}`, dir).prerelease, true);
  rmSync(dir, { recursive: true, force: true });
});

test('a beta at or below the stable the catalog offers is refused', () => {
  for (const [v, stable] of [['v2026.10.10.1-beta.1', '2026.10.10.1'], ['v2026.10.09.1-beta.3', '2026.10.10.1']]) {
    const dir = scratchTree('side-chain', v, stableAt('ni-side-chain', stable));
    assert.throws(() => resolve(`side-chain-${v}`, dir), /is not above the stable/, v);
    rmSync(dir, { recursive: true, force: true });
  }
});

/* ------------------------------------------------- the site's downloads */

test('the site links the newest published stable release, never a beta or an untagged bump', () => {
  const rel = (tag, prerelease = false) => ({ tag, prerelease });
  const releases = [
    rel('trance-gate-v1.0.1'),                     /* the semver legacy: not a date release */
    rel('trance-gate-v2026.10.08.3'),
    rel('trance-gate-v2026.10.09.1'),
    rel('trance-gate-v2026.10.10.1-beta.1', true),
    rel('trance-gate-v2026.10.10.2', true),        /* published as a prerelease */
    rel('trance-gate-v2026.10.12.1'),              /* ahead of the tree: a branch's tag */
    rel('trance-gate-extra-v2026.10.11.1'),        /* another product's prefix */
  ];
  assert.equal(latestStableRelease('trance-gate', releases, 'v2026.10.11.1'), 'v2026.10.09.1');
  assert.equal(latestStableRelease('trance-gate', releases, 'v2026.10.10.1-beta.1'), 'v2026.10.09.1');
  assert.equal(latestStableRelease('spectrogram', releases, 'v2026.10.09.1'), null);
});

test('the site links only a release that carries the zip it links', () => {
  const zip = '-macOS.zip';
  const rel = (v, assets) => ({ tag: `side-chain-${v}`, prerelease: false, assets });
  const releases = [
    rel('v2026.10.09.1', ['side-chain-v2026.10.09.1-macOS.zip', 'ni-side-chain-module.tar.gz']),
    /* The module's workflow published, the plugin's build failed. */
    rel('v2026.10.10.1', ['ni-side-chain-module.tar.gz']),
  ];
  assert.equal(latestStableRelease('side-chain', releases, 'v2026.10.10.1', [zip]), 'v2026.10.09.1');
  assert.equal(latestStableRelease('side-chain', releases, 'v2026.10.10.1'), 'v2026.10.10.1', 'nothing needed');
  /* A local build knows tags, not assets, and links them as it always did. */
  const local = releases.map((r) => ({ ...r, assets: null }));
  assert.equal(latestStableRelease('side-chain', local, 'v2026.10.10.1', [zip]), 'v2026.10.10.1');
});

test('published releases come from GitHub on the Pages runner, and from the tags elsewhere', () => {
  const dir = mkdtempSync(join(tmpdir(), 'ni-releases-'));
  const list = join(dir, 'releases.json');
  writeFileSync(list, JSON.stringify([
    { tagName: 'side-chain-v2026.10.10.1', isPrerelease: false, isDraft: false, assets: ['a.zip'] },
    { tagName: 'side-chain-v2026.10.11.1-beta.1', isPrerelease: true, isDraft: false, assets: [] },
    { tagName: 'side-chain-v2026.10.11.1', isPrerelease: false, isDraft: true, assets: ['b.zip'] },
  ]));
  assert.deepEqual(publishedReleases({ env: { NI_RELEASES: list } }), [
    { tag: 'side-chain-v2026.10.10.1', prerelease: false, assets: ['a.zip'] },
    { tag: 'side-chain-v2026.10.11.1-beta.1', prerelease: true, assets: [] },
  ]);
  const repo = scratchRepo();
  git(repo, 'tag', 'side-chain-v2026.10.10.1');
  git(repo, 'tag', 'side-chain-v2026.10.11.1-beta.1');
  assert.deepEqual(publishedReleases({ env: {}, cwd: repo }), [
    { tag: 'side-chain-v2026.10.10.1', prerelease: false, assets: null },
    { tag: 'side-chain-v2026.10.11.1-beta.1', prerelease: true, assets: null },
  ]);
  assert.deepEqual(publishedReleases({ env: {}, cwd: dir }), [], 'no checkout, no releases');
  rmSync(dir, { recursive: true, force: true });
  rmSync(repo, { recursive: true, force: true });
});

/* ----------------------------------------------- the tested-commit note */


test('a passing run is recorded on the commit it tested, one line per run', () => {
  const repo = scratchRepo();
  const sha = git(repo, 'rev-parse', 'HEAD');
  const first = attest({ tier: 'product', products: ['side-chain'], expectSha: sha,
                         results: { tests: 120, gaps: 0 }, cwd: repo, ...FIXED });
  assert.deepEqual(first, { tier: 'product', products: ['side-chain'], sha, date: '2026-10-10T12:00:00.000Z',
    host: 'test-host', rustc: 'rustc 1.99.0 (test)', result: 'pass', results: { tests: 120, gaps: 0 } });
  attest({ tier: 'full', results: CLEAN, cwd: repo, ...FIXED });
  assert.match(git(repo, 'for-each-ref', '--format=%(refname)', 'refs/notes/'), new RegExp(`^${NOTES_REF}$`));
  const records = attestations(sha, repo);
  assert.deepEqual(records.map((r) => r.tier), ['product', 'full']);

  assert.equal(attestationFor(records, sha, 'side-chain').tier, 'product');
  assert.equal(attestationFor(records.slice(0, 1), sha, 'trance-gate'), null, 'another product\'s run');
  assert.equal(attestationFor(records.slice(1), sha, 'trance-gate').tier, 'full', 'a full run covers all');
  assert.equal(attestationFor(records, 'f'.repeat(40), 'side-chain'), null, 'another commit');
  assert.equal(attestationFor([{ ...first, result: 'fail' }], sha, 'side-chain'), null);
  rmSync(repo, { recursive: true, force: true });
});

test('a run with gaps nobody waived does not release, and one with waived gaps does', () => {
  const sha = 'a'.repeat(40);
  const run = (results) => ({ tier: 'product', products: ['side-chain'], sha, result: 'pass', results });
  assert.equal(attestationFor([run({ gaps: 2, untested: 'docker,rosetta' })], sha, 'side-chain'), null);
  assert.equal(attestationFor([run({ tests: 120 })], sha, 'side-chain'), null, 'gaps not recorded');
  assert.equal(attestationFor([run({ gaps: '1' })], sha, 'side-chain'), null, 'as a string too');
  assert.ok(attestationFor([run({ gaps: 0, waived: 'docker', untested: 'docker' })], sha, 'side-chain'));
  assert.ok(attestationFor([run({ gaps: 2 }), run({ gaps: '0' })], sha, 'side-chain'), 'one clean run is enough');
});

test('a run is not recorded on a dirty tree, or when HEAD moved under it', () => {
  const repo = scratchRepo();
  const sha = git(repo, 'rev-parse', 'HEAD');
  writeFileSync(join(repo, 'README.md'), 'edited\n');
  assert.throws(() => attest({ tier: 'full', cwd: repo, ...FIXED }), /uncommitted changes/);
  git(repo, 'commit', '-q', '-am', 'moved');
  assert.throws(() => attest({ tier: 'full', expectSha: sha, cwd: repo, ...FIXED }), /HEAD moved/);
  writeFileSync(join(repo, 'new.txt'), 'untracked\n');
  assert.throws(() => attest({ tier: 'full', cwd: repo, ...FIXED }), /uncommitted changes/, 'untracked too');
  assert.throws(() => attest({ tier: 'quick', cwd: repo, ...FIXED }), /not one a release accepts/);
  assert.throws(() => attest({ tier: 'product', cwd: repo, ...FIXED }), /names its product/);
  assert.equal(git(repo, 'for-each-ref', 'refs/notes/'), '', 'nothing was recorded');
  rmSync(repo, { recursive: true, force: true });
});

test('a tag is released only from a commit with a passing run of its product', () => {
  /* What resolve() reads, committed, so the tree is clean. */
  const files = {};
  for (const p of ['versions.json', 'release.json', 'plugins/side-chain/CMakeLists.txt',
                   'modules/side-chain/module.env', 'modules/side-chain/module.json'])
    files[p] = read(...p.split('/'));
  const repo = scratchRepo(files);
  const tag = tagOf('side-chain');
  assert.throws(() => attested(tag, 'HEAD', repo), /carries no passing test run for side-chain/);
  attest({ tier: 'product', products: ['trance-gate'], results: CLEAN, cwd: repo, ...FIXED });
  assert.throws(() => attested(tag, 'HEAD', repo), /no passing test run for side-chain under \S+ \(1 note line/);
  attest({ tier: 'product', products: ['side-chain'], results: { gaps: 1, untested: 'docker' }, cwd: repo, ...FIXED });
  assert.throws(() => attested(tag, 'HEAD', repo), /none without unwaived gaps: product on test-host, gaps=1, untested=docker/);
  attest({ tier: 'product', products: ['side-chain'], results: CLEAN, cwd: repo, ...FIXED });
  assert.deepEqual(attested(tag, 'HEAD', repo).products, ['side-chain']);
  /* A note on the parent says nothing about its child. */
  writeFileSync(join(repo, 'next.txt'), 'next\n');
  git(repo, 'add', '-A');
  git(repo, 'commit', '-q', '-m', 'next');
  assert.throws(() => attested(tag, 'HEAD', repo), /no passing test run/);
  rmSync(repo, { recursive: true, force: true });
});

/* -------------------------------------------------- the workflows' pins */

const WORKFLOWS = readdirSync(join(ROOT, '.github', 'workflows')).filter((f) => f.endsWith('.yml'));
const code = (wf) => read('.github', 'workflows', wf).split('\n').filter((l) => !/^\s*#/.test(l)).join('\n');

/*
 * EVERY ACTION BY COMMIT. A tag is a pointer its owner can move, so a pinned
 * tag is still whatever that repository says today; a full commit SHA is the
 * code that was reviewed. The comment beside it says which release it is, for
 * the person who moves it.
 */
test('every action is pinned by a full commit SHA, with its version beside it', () => {
  for (const wf of WORKFLOWS) {
    for (const m of read('.github', 'workflows', wf).matchAll(/^\s*(?:-\s*)?uses:\s*(\S+)(.*)$/gm)) {
      const [, ref, rest] = m;
      if (ref.startsWith('./')) continue;   /* this repository's own reusable workflow */
      assert.match(ref, /^[\w.-]+\/[\w.-]+(?:\/[\w./-]+)?@[0-9a-f]{40}$/, `${wf}: ${ref} is not pinned by SHA`);
      assert.match(rest, /^\s+#\s*v\d+(\.\d+)*\s*$/, `${wf}: ${ref} has no "# vX.Y.Z" comment`);
    }
  }
});

test('one Rust toolchain: rust-toolchain.toml pins it and the module image installs it', () => {
  const channel = /^\s*channel\s*=\s*"([^"]+)"/m.exec(read('rust-toolchain.toml'))?.[1];
  assert.match(channel ?? '', /^\d+\.\d+\.\d+$/, 'an exact version, not "stable"');
  const docker = read('modules', '_shared', 'Dockerfile');
  assert.equal(/^ARG RUST_TOOLCHAIN=(\S+)$/m.exec(docker)?.[1], channel, 'the Dockerfile\'s RUST_TOOLCHAIN');
  assert.match(docker, /^ENV RUSTUP_TOOLCHAIN=\$RUST_TOOLCHAIN$/m);
  for (const from of docker.split('\n').filter((l) => /^FROM\s/.test(l)))
    assert.match(from, /@sha256:[0-9a-f]{64}(\s|$)/, `${from} is not pinned by digest`);
});

/* ---------------------------------------------- what a release proves */

/* Each job's lines, by its id. */
const jobs = (wf) => {
  const y = code(wf);
  const out = {};
  let job = null;
  for (const line of y.slice(y.indexOf('\njobs:\n') + 7).split('\n')) {
    const m = /^ {2}([\w-]+):\s*$/.exec(line);
    if (m) { job = m[1]; out[job] = ''; continue; }
    if (/^\S/.test(line)) job = null;
    if (job) out[job] += `${line}\n`;
  }
  return out;
};

for (const wf of ['release-plugins.yml', 'release-schwung.yml']) {
  test(`${wf} refuses an untested commit before it builds anything`, () => {
    const y = code(wf);
    const check = y.indexOf('node scripts/release.mjs attested "$GITHUB_REF_NAME"');
    assert.ok(check > 0, 'no attestation check');
    assert.match(y, /git fetch [^\n]*refs\/notes\/ni-tests:refs\/notes\/ni-tests/, 'the notes are not fetched');
    for (const build of ['scripts/test.sh product', 'modules/_shared/test.sh', 'modules/_shared/package.sh']) {
      const at = y.indexOf(build);
      if (at > 0) assert.ok(check < at, `${build} runs before the attestation check`);
    }
  });

  test(`${wf} checks the supply chain: advisories, licences, bans, sources`, () => {
    assert.match(code(wf), /cargo deny [^\n]*check advisories licenses bans sources/);
  });

  test(`${wf} deploys the site after it has published`, () => {
    const j = jobs(wf);
    const pages = Object.entries(j).find(([, b]) => /uses: \.\/\.github\/workflows\/pages\.yml/.test(b));
    assert.ok(pages, 'no job calls pages.yml');
    assert.match(pages[1], /needs: release\b/);
    assert.match(pages[1], /pages: write/);
    assert.match(pages[1], /id-token: write/);
  });
}

test('pages.yml is reusable, by hand and from a release, and never two deploys at once', () => {
  const y = code('pages.yml');
  assert.match(y, /\n  workflow_call:/);
  assert.match(y, /\n  workflow_dispatch:/);
  assert.doesNotMatch(y, /\n  release:/, 'a release published with GITHUB_TOKEN starts no workflow');
  assert.match(y, /concurrency:\n\s+group: pages\n\s+cancel-in-progress: false/);
  assert.match(y, /NI_RELEASES/, 'the download links do not come from the published releases');
});

test('release-schwung.yml tests the module on arm64 and releases the tarball it tested', () => {
  const j = jobs('release-schwung.yml');
  assert.match(j.test ?? '', /runs-on: ubuntu-24\.04-arm/);
  assert.match(j.test, /modules\/_shared\/test\.sh "\$PRODUCT"/);
  assert.match(j.test, /engines\/\$PRODUCT\/tests\/ui\.sh/, 'the Move editor is not tested');
  assert.match(j.release ?? '', /needs: \[?[^\n]*\btest\b/);
  assert.doesNotMatch(j.release, /package\.sh/, 'the release builds a tarball nobody tested');
});

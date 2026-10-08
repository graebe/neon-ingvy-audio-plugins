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
import {
  ROOT, resolve, readModuleEnv, updateReleaseJson, schwungVersion, LEGACY_TOP_LEVEL, bundleOf, ZIP_OS,
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
      assert.equal(r.zip_windows, `${p}-${VERSIONS[p]}-Windows.zip`);
      assert.equal(r.zip_linux, `${p}-${VERSIONS[p]}-Linux.zip`);
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
  const before = JSON.parse(read('release.json'));
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

for (const os of Object.keys(ZIP_OS)) {
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
 * does; a one-slice build is checked for everything else. */
const BUILT = process.env.NI_BUNDLES;
test('every product stages from the bundles this build made (a dry run)', { skip: !BUILT && 'NI_BUNDLES is not set' }, () => {
  const universal = process.platform === 'darwin' && process.env.NI_UNIVERSAL === '1';
  for (const p of juceProducts())
    assert.deepEqual(plan(p.dir, BUILT, { universal }).problems, [], `${p.dir} from ${BUILT}`);
});

test('release-plugins.yml stages with the script, on the three OSes, and asks for no other format', () => {
  const y = read('.github', 'workflows', 'release-plugins.yml');
  assert.match(y, /node scripts\/stage-release\.mjs/);
  for (const runner of ['macos-15', 'windows-2025', 'ubuntu-24.04'])
    assert.ok(y.includes(runner), `release-plugins.yml does not build on ${runner}`);
  for (const os of Object.keys(ZIP_OS)) assert.ok(y.includes(`zip_${os}`), `no zip_${os} in the workflow`);
  assert.doesNotMatch(y, /\.component\b|\.clap\b|web\/index\.html|ui\.js\.LICENSE/,
    'release-plugins.yml still names a format or an editor no bundle has');
});

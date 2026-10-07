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
import { readFileSync, readdirSync, existsSync, mkdtempSync, cpSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import {
  ROOT, resolve, readModuleEnv, updateReleaseJson, schwungVersion, LEGACY_TOP_LEVEL, bundleOf,
} from '../scripts/release.mjs';

const read = (...p) => readFileSync(join(ROOT, ...p), 'utf8');
const VERSIONS = JSON.parse(read('versions.json'));
const PRODUCTS = Object.keys(VERSIONS).filter((k) => !k.startsWith('__'));
const MODULES = readdirSync(join(ROOT, 'modules'))
  .filter((d) => existsSync(join(ROOT, 'modules', d, 'module.env')));
/* A product with a plugin: an iPlug2 config.h, or a JUCE build's
 * ni_add_juce_plugin (scripts/release.mjs, bundleOf). */
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
      assert.equal(r.zip, `${p}-${VERSIONS[p]}-macOS.zip`);
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

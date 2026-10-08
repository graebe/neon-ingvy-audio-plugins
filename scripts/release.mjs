#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What a release tag means, and the one writer of release.json.
 *
 *   node scripts/release.mjs resolve <tag>
 *       Checks the tag against the tree and prints what it releases, as
 *       key=value lines ready for $GITHUB_OUTPUT. Exits non-zero, naming the
 *       disagreement, if the tag does not match versions.json or module.json.
 *
 *   node scripts/release.mjs release-json <tag> <owner/repo> [path]
 *       Records a published Schwung module release in release.json.
 *
 * WHY THIS IS A SCRIPT AND NOT WORKFLOW SHELL. Both release workflows used to
 * parse the tag inline, and they parsed it differently: release-plugins.yml
 * kept the date scheme's leading "v" and release-schwung.yml stripped it with
 * the product prefix, so every correct Schwung tag failed its own version
 * check. Here it is parsed once, by code that tests/release.test.mjs runs.
 *
 * TAG = <product>-<version as versions.json spells it>, e.g.
 * trance-gate-v2026.09.29.3. The product is a key of versions.json; a product
 * with a plugin ships its VST3 (bundleOf: the TARGET of its
 * ni_add_juce_plugin), as a macOS zip, and one with modules/<product>/module.env
 * ships a Schwung module; a tag releases both when it has both -- they are
 * one product in two shells.
 */
import { readFileSync, writeFileSync, existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

export const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');

/*
 * THE BUNDLE A PRODUCT SHIPS, or nothing for a product with no plugin: the
 * TARGET its CMakeLists.txt gives ni_add_juce_plugin. Throws for a call
 * without one, which is a plugin that cannot be named.
 */
export function bundleOf(root, product) {
  const cmake = join(root, 'plugins', product, 'CMakeLists.txt');
  if (!existsSync(cmake)) return null;
  const text = readFileSync(cmake, 'utf8');
  if (!/ni_add_juce_plugin\s*\(/.test(text)) return null;
  const bundle = /\bTARGET\s+(\S+)/.exec(text)?.[1];
  if (!bundle) throw new Error(`plugins/${product}/CMakeLists.txt calls ni_add_juce_plugin with no TARGET`);
  return bundle;
}

/*
 * THE SCHWUNG SPELLING OF A VERSION: versions.json's, without the leading "v".
 *
 * Schwung Manager compares versions with parseInt() on each dot-separated
 * part (src/shared/store_utils.mjs, compareVersions), and parseInt("v2026") is
 * NaN, which it reads as 0. With the "v" the YEAR is ignored: v2027.01.01.1
 * would compare older than v2026.12.31.1 and no device would be offered it.
 * The installed side of that comparison is module.json's "version", so
 * module.json and release.json both carry this spelling, and
 * tests/versions.test.mjs holds module.json to it.
 */
export const schwungVersion = (v) => v.replace(/^v/, '');

const readJson = (root, p) => JSON.parse(readFileSync(join(root, p), 'utf8'));

/* modules/<m>/module.env: plain KEY=value / KEY="value" lines, sourced by bash. */
export function readModuleEnv(root, product) {
  const p = join(root, 'modules', product, 'module.env');
  if (!existsSync(p)) return null;
  const env = {};
  for (const line of readFileSync(p, 'utf8').split('\n')) {
    const m = /^([A-Z_][A-Z0-9_]*)=(?:"([^"]*)"|(\S*))\s*$/.exec(line);
    if (m) env[m[1]] = m[2] ?? m[3];
  }
  return env;
}

/* The OS a release zip is for, as its name spells it. macOS only: Linux and
 * Windows rejoin the releases later, as part of the full build. */
export const ZIP_OS = { macos: 'macOS' };

export function resolve(tag, root = ROOT) {
  const versions = readJson(root, 'versions.json');
  const products = Object.keys(versions).filter((k) => !k.startsWith('__'));
  /* Longest match first, so a product whose name prefixes another's cannot
   * claim the other's tags. */
  const product = products
    .sort((a, b) => b.length - a.length)
    .find((p) => tag.startsWith(`${p}-`));
  if (!product) throw new Error(`tag "${tag}" names no product in versions.json`);

  const version = tag.slice(product.length + 1);
  const source = versions[product];
  if (version !== source)
    throw new Error(`tag "${tag}" says ${version}; versions.json says ${source} ` +
      `-- the tag is <product>-<version exactly as versions.json spells it>`);

  const out = { product, version, prerelease: /-beta\./.test(version) };

  const bundle = bundleOf(root, product);
  if (bundle) {
    out.bundle = bundle;
    /* One zip per OS the release builds on (release-plugins.yml). */
    for (const [os, label] of Object.entries(ZIP_OS))
      out[`zip_${os}`] = `${product}-${version}-${label}.zip`;
  }

  const env = readModuleEnv(root, product);
  if (env) {
    const module = readJson(root, `modules/${product}/module.json`);
    if (module.id !== env.MODULE_ID)
      throw new Error(`modules/${product}: module.env says ${env.MODULE_ID}, module.json says ${module.id}`);
    if (module.version !== schwungVersion(version))
      throw new Error(`modules/${product}/module.json says ${module.version}; ` +
        `tag ${tag} needs ${schwungVersion(version)}`);
    out.module_id = env.MODULE_ID;
    out.module_version = schwungVersion(version);
    out.module_asset = `${env.MODULE_ID}-module.tar.gz`;
  }

  if (!out.bundle && !out.module_id)
    throw new Error(`${product} has neither a plugin in plugins/${product} nor modules/${product}/module.env -- nothing to release`);
  return out;
}

/*
 * RELEASE.JSON: WHAT HAS BEEN PUBLISHED, IN THE SHAPE SCHWUNG MANAGER READS.
 *
 * One repository, several catalog modules, so the multi-module form:
 *
 *   { "modules": { "<catalog id>": { version, download_url, channels } } }
 *
 * keyed by module.json's "id" -- the catalog entry's id, which is what the
 * manager looks the entry up by. The top-level version/download_url/channels
 * stay as well, mirroring the Trance Gate: it was this repository's only
 * module before the Side-Chain, so a catalog entry or a manager that predates
 * the modules map reads those and must keep seeing it.
 *
 * A version containing "-beta." goes to that module's beta channel and touches
 * nothing else; anything else is stable and also moves the entry's own
 * version/download_url, which is what a channels-unaware manager reads.
 */
export const LEGACY_TOP_LEVEL = 'trance-gate';

export function updateReleaseJson(rel, { product, module_id, module_version, module_asset }, repo, tag) {
  const url = `https://github.com/${repo}/releases/download/${tag}/${module_asset}`;
  const channel = /-beta\./.test(module_version) ? 'beta' : 'stable';
  const modules = { ...(rel.modules ?? {}) };
  const entry = { ...(modules[module_id] ?? {}) };
  entry.channels = { ...(entry.channels ?? {}), [channel]: { version: module_version, download_url: url } };
  if (channel === 'stable') {
    entry.version = module_version;
    entry.download_url = url;
  }
  if (!entry.version) {
    /* A module's first release is a beta: there is no stable to name, and an
     * entry without a version is one the manager rejects outright. */
    entry.version = module_version;
    entry.download_url = url;
  }
  modules[module_id] = { version: entry.version, download_url: entry.download_url, channels: entry.channels };

  const next = { ...rel, modules };
  if (product === LEGACY_TOP_LEVEL) {
    next.version = modules[module_id].version;
    next.download_url = modules[module_id].download_url;
    next.channels = modules[module_id].channels;
  }
  /* Key order is part of a readable diff: legacy fields first, then modules. */
  const { version, download_url, channels, ...rest } = next;
  return { version, download_url, channels, ...rest, modules: rest.modules };
}

function main([cmd, ...args]) {
  if (cmd === 'resolve' && args.length === 1) {
    const r = resolve(args[0]);
    for (const [k, v] of Object.entries(r)) console.log(`${k}=${v}`);
    return;
  }
  if (cmd === 'release-json' && (args.length === 2 || args.length === 3)) {
    const [tag, repo, path = join(ROOT, 'release.json')] = args;
    const r = resolve(tag);
    if (!r.module_id) throw new Error(`${r.product} ships no Schwung module`);
    const rel = existsSync(path) ? JSON.parse(readFileSync(path, 'utf8')) : {};
    const next = updateReleaseJson(rel, r, repo, tag);
    writeFileSync(path, JSON.stringify(next, null, 2) + '\n');
    console.log(`release.json now: ${JSON.stringify(next)}`);
    return;
  }
  console.error('usage: release.mjs resolve <tag>\n' +
                '       release.mjs release-json <tag> <owner/repo> [path]');
  process.exit(2);
}

if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) {
  try {
    main(process.argv.slice(2));
  } catch (e) {
    console.error(`ERROR: ${e.message}`);
    process.exit(1);
  }
}

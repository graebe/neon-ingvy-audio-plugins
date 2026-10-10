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
 *   node scripts/release.mjs bundle <product>
 *       Prints the bundle a product ships (NITranceGate for trance-gate), as
 *       `scripts/test.sh product` builds and validates it. Exits non-zero for
 *       a product with no plugin.
 *
 *   node scripts/release.mjs attest --tier full|product [--product <p>]...
 *                                   [--expect-sha <sha>] [--result k=v]...
 *       Records that a tier passed on HEAD, as a git note under
 *       refs/notes/ni-tests. scripts/test.sh runs it when `full` or
 *       `product <p>` passes. Refuses a tree with uncommitted changes, or a
 *       HEAD other than the one the run started on: a pass on either is a
 *       pass for something no commit holds.
 *
 *   node scripts/release.mjs attested <tag> [<rev>]
 *       Exits non-zero unless <rev> (HEAD by default) carries a passing note
 *       for the tag's product: its own `product` run, or a `full` run. Both
 *       release workflows ask it before they build anything.
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
import { execFileSync } from 'node:child_process';
import { hostname } from 'node:os';

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

/*
 * A DATE VERSION, AND ITS ORDER. v<YYYY.MM.DD>.<n>, or a beta of it,
 * v<YYYY.MM.DD>.<n>-beta.<k>: a PRERELEASE of that version, so it orders
 * below it and above everything before it --
 *
 *   v2026.10.10.1-beta.1 < v2026.10.10.1-beta.2 < v2026.10.10.1 < v2026.10.10.2-beta.1
 *
 * tests/versions.test.mjs derives every other spelling from the same pattern,
 * and cmake/NiJucePlugin.cmake accepts the same two forms.
 */
export const VERSION_RE = /^v(\d{4})\.(\d{2})\.(\d{2})\.(\d+)(?:-beta\.([1-9]\d*))?$/;
export const isPrerelease = (v) => /-beta\./.test(v);

export function compareVersions(a, b) {
  const key = (v) => {
    const m = VERSION_RE.exec(v);
    if (!m) throw new Error(`"${v}" is not v<YYYY.MM.DD>.<n>[-beta.<k>]`);
    /* A release sorts after every beta of it: Infinity for "no beta". */
    return [m[1], m[2], m[3], m[4], m[5] ?? Infinity].map(Number);
  };
  const [x, y] = [key(a), key(b)];
  for (let i = 0; i < x.length; i++) if (x[i] !== y[i]) return x[i] < y[i] ? -1 : 1;
  return 0;
}

/*
 * HOW SCHWUNG MANAGER ORDERS TWO VERSIONS, as it does it rather than as it
 * should: versionNewer in schwung-manager/module_channel.go (charlesvestal/
 * schwung). Ported so the one case it gets wrong is refused at the tag
 * instead of discovered on a device.
 *
 * Same base with exactly one side a prerelease: the release wins, which is
 * what makes a beta user land on stable once it catches up. Otherwise each
 * dotted part through Go's strconv.Atoi, where anything not a plain integer
 * is 0 -- and "1-beta" is not. So a beta's subversion reads as 0 against any
 * OTHER base: v2026.10.10.2-beta.1 compares older than v2026.10.10.1, and a
 * beta channel would never offer it. Across days the date decides first, so
 * that is the only case it gets wrong (see resolve()).
 */
export function schwungManagerNewer(a, b) {
  [a, b] = [a.replace(/^v/, ''), b.replace(/^v/, '')];
  /* splitPrerelease: the base before the first "-", and whether there is more. */
  const [aBase, aPre] = [a.split('-')[0], a.includes('-')];
  const [bBase, bPre] = [b.split('-')[0], b.includes('-')];
  if (aBase === bBase && aPre !== bPre) return !aPre;
  /* isNewerSemver. */
  if (a === b) return false;
  const atoi = (p) => (/^[+-]?\d+$/.test(p) ? Number(p) : 0);
  const [lp, cp] = [a.split('.').map(atoi), b.split('.').map(atoi)];
  for (let i = 0; i < lp.length && i < cp.length; i++) {
    if (lp[i] > cp[i]) return true;
    if (lp[i] < cp[i]) return false;
  }
  return lp.length > cp.length;
}

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
    if (out.prerelease) betaReachesItsChannel(root, out, tag);
  }

  if (!out.bundle && !out.module_id)
    throw new Error(`${product} has neither a plugin in plugins/${product} nor modules/${product}/module.env -- nothing to release`);
  return out;
}

/*
 * A BETA THE MANAGER WOULD NOT OFFER IS REFUSED BEFORE IT IS BUILT. The beta
 * channel offers a beta only when schwungManagerNewer() puts it above the
 * stable entry. Two kinds never get there: a beta at or below the stable
 * (v2026.10.10.1-beta.1 once 2026.10.10.1 is out), and the one case the
 * manager misorders, a beta of a later subversion on the SAME DAY as the
 * stable (v2026.10.10.2-beta.1 over 2026.10.10.1). Published, either is a
 * beta nobody is offered. Judged against the release.json of the tagged tree,
 * which is main's as of the tag.
 */
function betaReachesItsChannel(root, r, tag) {
  const p = join(root, 'release.json');
  if (!existsSync(p)) return;
  const entry = JSON.parse(readFileSync(p, 'utf8')).modules?.[r.module_id];
  const stable = entry?.channels?.stable?.version ?? entry?.version;
  if (!stable || !VERSION_RE.test(`v${stable}`)) return;
  if (schwungManagerNewer(r.module_version, stable)) return;
  if (compareVersions(r.version, `v${stable}`) <= 0)
    throw new Error(`tag ${tag}: ${r.module_version} is not above the stable ${stable} the catalog ` +
      'already offers, so the beta channel would never offer it. Tag a version above it.');
  throw new Error(`tag ${tag}: Schwung Manager would order ${r.module_version} below the stable ` +
    `${stable} (its version compare reads a beta's subversion as 0), so the beta channel would never ` +
    'offer it. Release the beta under the next day\'s date, or release it as stable.');
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

/*
 * THE NEWEST STABLE RELEASE OF A PRODUCT, for the site's download links.
 *
 * versions.json says what the tree BUILDS, which is ahead of what has been
 * released whenever a bump is not tagged yet, and may be a beta. A link
 * composed from it pointed at a release that did not exist, or offered a beta
 * as the download. So the site links the newest release that is published,
 * not a prerelease, not a beta by name, and not ahead of versions.json (a tag
 * from a branch main never took).
 *
 * AND THAT CARRIES WHAT THE LINK POINTS AT. One tag runs two workflows, and the
 * Move module's can publish the release while the plugin's build fails: a
 * release with no zip, whose download link is a 404. `needs` are the asset
 * names the link is composed from, as suffixes of the tag ("-macOS.zip"); a
 * release whose assets are known must carry every one. A local build knows
 * the tags only (assets: null), and links them as before.
 *
 * `releases` is [{ tag, prerelease, assets }], from publishedReleases().
 */
export function latestStableRelease(product, releases, ceiling, needs = []) {
  let best = null;
  for (const { tag, prerelease, assets } of releases) {
    if (prerelease || !tag.startsWith(`${product}-`)) continue;
    if (assets && !needs.every((n) => assets.includes(`${tag}${n}`))) continue;
    const v = tag.slice(product.length + 1);
    if (!VERSION_RE.test(v) || isPrerelease(v)) continue;
    if (ceiling && VERSION_RE.test(ceiling) && compareVersions(v, ceiling) > 0) continue;
    if (!best || compareVersions(v, best) > 0) best = v;
  }
  return best;
}

/*
 * WHAT HAS BEEN RELEASED. On the Pages runner, NI_RELEASES names the JSON
 * pages.yml wrote from GitHub's releases API: [{ tagName, isPrerelease,
 * isDraft, assets: [name] }], an asset listed once its upload finished. The
 * releases as GitHub publishes them, so a tag whose release failed is not
 * one. Anywhere else -- a local build of the site, the full tier's link check
 * -- the checkout's tags, a beta being one by its name, and no assets known.
 */
export function publishedReleases({ env = process.env, cwd = process.cwd() } = {}) {
  if (env.NI_RELEASES) {
    return JSON.parse(readFileSync(env.NI_RELEASES, 'utf8'))
      .filter((r) => !r.isDraft)
      .map((r) => ({ tag: r.tagName, prerelease: Boolean(r.isPrerelease), assets: r.assets ?? [] }));
  }
  try {
    return git(cwd, 'tag', '--list').split('\n').filter(Boolean)
      .map((tag) => ({ tag, prerelease: isPrerelease(tag), assets: null }));
  } catch {
    return [];
  }
}

/*
 * THE TESTED-COMMIT ATTESTATION.
 *
 * The owner's rule: everything is built and tested LOCALLY, and GitHub
 * Actions publish. A release workflow cannot see a local run, so the run
 * leaves proof on the commit it tested -- a git note, one JSON line per
 * passing run, under its own ref so it never mixes with anyone's other
 * notes -- and the workflow refuses a tag whose commit carries none.
 *
 *   { "tier": "product", "products": ["side-chain"], "sha": "...",
 *     "date": "...", "host": "...", "rustc": "rustc 1.99.0 (...)",
 *     "result": "pass", "results": { "tests": 412, "duration_s": 1650, ... } }
 *
 * One line per run, appended, so `git notes merge -s cat_sort_uniq` joins
 * two machines' notes without a conflict.
 */
export const NOTES_REF = 'refs/notes/ni-tests';
export const TIERS = ['full', 'product'];

const git = (cwd, ...args) => execFileSync('git', args,
  { cwd, encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] }).trim();

export function attestations(rev, cwd = ROOT) {
  let text;
  try {
    text = git(cwd, 'notes', `--ref=${NOTES_REF}`, 'show', rev);
  } catch {
    return [];
  }
  return text.split('\n').filter((l) => l.trim().startsWith('{')).flatMap((l) => {
    try { return [JSON.parse(l)]; } catch { return []; }
  });
}

/* A run of <product> on <sha> that passed. */
const runOf = (r, sha, product) => r.result === 'pass' && r.sha === sha &&
  (r.tier === 'full' || (r.tier === 'product' && r.products?.includes(product)));

/*
 * A RUN THAT LEFT GAPS NOBODY WAIVED DID NOT TEST THE PRODUCT. scripts/test.sh
 * records `gaps` -- the tests that did not run for want of a capability, less
 * the ones waived by name (scripts/test-summary.mjs) -- on every run, gated or
 * not, so a product run passes with them. It does not release with them: a
 * gap is waived on purpose or the run is repeated where the capability is. A
 * note from before the count was recorded says nothing about it, so it does
 * not count as none.
 */
const clean = (r) => String(r.results?.gaps) === '0';

/* The note that lets <product> be released from <sha>, or null. */
export const attestationFor = (records, sha, product) =>
  records.find((r) => runOf(r, sha, product) && clean(r)) ?? null;

export function attest({ tier, products = [], expectSha, results = {}, cwd = ROOT,
                         rustc, host = hostname(), date = new Date() }) {
  if (!TIERS.includes(tier)) throw new Error(`tier "${tier}" is not one a release accepts (${TIERS.join(', ')})`);
  if (tier === 'product' && !products.length) throw new Error('a product run names its product');
  const sha = git(cwd, 'rev-parse', 'HEAD');
  if (expectSha && sha !== expectSha)
    throw new Error(`HEAD moved during the run (${expectSha.slice(0, 12)} -> ${sha.slice(0, 12)}), ` +
      'so the run tested neither; nothing recorded');
  const dirty = git(cwd, 'status', '--porcelain');
  if (dirty)
    throw new Error('the tree has uncommitted changes, so the run tested something no commit holds; ' +
      `nothing recorded. Commit, and run it again:\n${dirty}`);
  const record = {
    tier, products, sha, date: date.toISOString(), host,
    rustc: rustc ?? execFileSync('rustc', ['--version'], { cwd, encoding: 'utf8' }).trim(),
    result: 'pass', results,
  };
  git(cwd, 'notes', `--ref=${NOTES_REF}`, 'append', '-m', JSON.stringify(record), sha);
  return record;
}

export function attested(tag, rev = 'HEAD', cwd = ROOT) {
  const { product } = resolve(tag, cwd);
  const sha = git(cwd, 'rev-parse', `${rev}^{commit}`);
  const records = attestations(sha, cwd);
  const ok = attestationFor(records, sha, product);
  const gapped = records.filter((r) => runOf(r, sha, product) && !clean(r));
  if (!ok && gapped.length)
    throw new Error(`${sha.slice(0, 12)} passed ${gapped.length} run(s) of ${product}, none without unwaived ` +
      `gaps: ${gapped.map((r) => `${r.tier} on ${r.host}, gaps=${r.results?.gaps ?? 'not recorded'}, ` +
        `untested=${r.results?.untested ?? 'not recorded'}`).join('; ')}. Run it again where those ` +
      'capabilities exist, or waive each by name (`scripts/test.sh product ' + product + ' --waive <name>`) ' +
      `so the gap is on the record, then \`git push origin ${NOTES_REF}\` and re-run the workflow's ` +
      'failed jobs (docs/tech/ci-live.md).');
  if (!ok)
    throw new Error(`${sha.slice(0, 12)} carries no passing test run for ${product} under ${NOTES_REF} ` +
      `(${records.length} note line(s) on it). Run \`scripts/test.sh product ${product}\` or ` +
      '`scripts/test.sh full` on that commit with a clean tree, then ' +
      `\`git push origin ${NOTES_REF}\`, and re-run the workflow's failed jobs -- the tag is already ` +
      'on GitHub, and pushing it again starts nothing (docs/tech/ci-live.md).');
  return ok;
}

/* --flag value pairs, repeatable ones collected. */
function flags(args, repeatable = []) {
  const out = Object.fromEntries(repeatable.map((k) => [k, []]));
  for (let i = 0; i < args.length; i += 2) {
    const k = args[i]?.replace(/^--/, '');
    if (!args[i]?.startsWith('--') || args[i + 1] === undefined) throw new Error(`bad argument ${args[i]}`);
    if (repeatable.includes(k)) out[k].push(args[i + 1]);
    else out[k] = args[i + 1];
  }
  return out;
}

function main([cmd, ...args]) {
  if (cmd === 'attest') {
    const f = flags(args, ['product', 'result']);
    const results = Object.fromEntries(f.result.map((kv) => {
      const [k, ...v] = kv.split('=');
      const value = v.join('=');
      return [k, /^\d+$/.test(value) ? Number(value) : value];
    }));
    const r = attest({ tier: f.tier, products: f.product, expectSha: f['expect-sha'], results });
    console.log(`recorded under ${NOTES_REF} on ${r.sha.slice(0, 12)}: ${JSON.stringify(r)}`);
    return;
  }
  if (cmd === 'attested' && (args.length === 1 || args.length === 2)) {
    const r = attested(args[0], args[1]);
    console.log(`tested: ${r.tier}${r.products?.length ? ` (${r.products.join(', ')})` : ''} ` +
      `on ${r.host}, ${r.date}, ${r.rustc}: ${JSON.stringify(r.results)}`);
    return;
  }
  if (cmd === 'resolve' && args.length === 1) {
    const r = resolve(args[0]);
    for (const [k, v] of Object.entries(r)) console.log(`${k}=${v}`);
    return;
  }
  if (cmd === 'bundle' && args.length === 1) {
    if (!Object.hasOwn(readJson(ROOT, 'versions.json'), args[0]) || args[0].startsWith('__'))
      throw new Error(`${args[0]} is not a product in versions.json`);
    const bundle = bundleOf(ROOT, args[0]);
    if (!bundle) throw new Error(`${args[0]} has no plugin in plugins/${args[0]}`);
    console.log(bundle);
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
                '       release.mjs release-json <tag> <owner/repo> [path]\n' +
                '       release.mjs bundle <product>\n' +
                '       release.mjs attest --tier full|product [--product <p>]... [--expect-sha <sha>] [--result k=v]...\n' +
                '       release.mjs attested <tag> [<rev>]');
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

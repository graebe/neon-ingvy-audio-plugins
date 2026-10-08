#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Everything that ships has a notice, and every notice is for something that
 * ships.
 *
 *   node scripts/check-licenses.mjs [--bundles build/out]
 *
 * THIRD_PARTY_LICENSES.md was written by hand and had drifted both ways: it
 * named bundles by names they no longer had, missed fonts and a whole engine,
 * never mentioned a JSON library a former framework compiled into every
 * plugin, and still carried eighty crates from a Rust plugin wrapper that had
 * been gone for months. This derives what ships from the tree and holds the
 * file to it, section by section:
 *
 *   framework  JUCE, at the submodule's version, and the VST3 SDK JUCE
 *              compiles in, at the version of JUCE's copy
 *   JUCE's     what juce_core and juce_graphics compile in, for the modules
 *              and switches cmake/NiJucePlugin.cmake uses
 *   allowlist  every licence in those two sections is on deny.toml's
 *              allowlist -- the one policy, which the Rust crates are held to
 *              as well -- but AGPL-3.0, which JUCE alone may carry
 *   Rust       the standard library, which every engine links
 *   font       one row per plugin bundle that embeds the kit's font, OFL.txt
 *              beside every copy of it, and Bravura's for a product that
 *              embeds that too
 *   engines    exactly the workspace's own crates, the path packages in
 *              Cargo.lock
 *   crates     exactly the crates from crates.io that ship, at their versions:
 *              cargo-about writes the section (scripts/gen-rust-notices.sh),
 *              and this recomputes what ships from `cargo metadata` and holds
 *              the two to each other -- and holds deny.toml's allowlist and
 *              targets to about.toml's, so the gate and the notices judge one
 *              graph by one rule
 *   test-only  doctest and Schwung's two module-API headers, while they are
 *              vendored
 *
 * With --bundles it also opens the built bundles and checks that each one
 * carries LICENSE, THIRD_PARTY_LICENSES.md, the AGPLv3 and Apache 2.0 texts
 * and its fonts' licences.
 */
import { readFileSync, readdirSync, existsSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { join } from 'node:path';
import { ROOT, listedBySection, rowsOf, section, juceProducts, bundleResources } from './licenses-lib.mjs';

const problems = [];
const fail = (msg) => problems.push(msg);
const read = (...p) => readFileSync(join(ROOT, ...p), 'utf8');
const sections = listedBySection();
const need = (sec, name, what) => {
  const row = sec.get(name);
  if (!row) fail(`no row for \`${name}\` -- ${what}`);
  return row ?? '';
};
const sameSet = (label, listed, shipped) => {
  for (const n of shipped) if (!listed.has(n)) fail(`${label}: \`${n}\` ships and is not listed`);
  for (const n of listed) if (!shipped.has(n)) fail(`${label}: \`${n}\` is listed and does not ship -- remove the row`);
};

/* The products, and what their bundles carry (scripts/licenses-lib.mjs). */
const JUCE_PLUGINS = juceProducts();

/* ------------------------------------------------------------ our own */
/* GPL-3.0-or-later, and the licence text exactly as the FSF publishes it
 * (https://www.gnu.org/licenses/gpl-3.0.txt): the GPL forbids changing the
 * document, and a hash is the only check that notices a changed word. The
 * copyright notice is not in it -- it is in the README and at the top of every
 * source file (tests/spdx.test.mjs). */
const GPL_3_0_SHA256 = '3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986';
if (createHash('sha256').update(readFileSync(join(ROOT, 'LICENSE'))).digest('hex') !== GPL_3_0_SHA256)
  fail('LICENSE is not the unmodified text of GPLv3 (https://www.gnu.org/licenses/gpl-3.0.txt)');

/* The texts every bundle on the JUCE shell carries beside the notices, as
 * their publishers publish them: JUCE's AGPLv3 and SheenBidi's Apache 2.0. */
const LICENCE_FILES = {
  'AGPL-3.0.txt': ['0d96a4ff68ad6d4b6f1f30f713b18d5184912ba8dd389f86aa7710db079abcb0',
                   'https://www.gnu.org/licenses/agpl-3.0.txt'],
  'Apache-2.0.txt': ['cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30',
                     'https://www.apache.org/licenses/LICENSE-2.0.txt'],
};
if (JUCE_PLUGINS.length)
  for (const [f, [sha, url]] of Object.entries(LICENCE_FILES)) {
    const at = join(ROOT, 'licenses', f);
    if (!existsSync(at)) fail(`licenses/${f} is missing -- the bundles on the JUCE shell carry it`);
    else if (createHash('sha256').update(readFileSync(at)).digest('hex') !== sha)
      fail(`licenses/${f} is not the unmodified text (${url})`);
  }

/* ------------------------------------------------------------ framework */
/* JUCE at the version the submodule declares, and the VST3 SDK at the version
 * of the copy JUCE vendors -- read from the checkout, so a submodule bump
 * without a notice update fails here. */
try {
  const fw = section(sections, 'The plugin framework');
  const shipped = new Set(JUCE_PLUGINS.length ? ['JUCE', "VST3 SDK (JUCE's copy)"] : []);
  sameSet('framework', new Set(fw.keys()), shipped);
  const juce = join(ROOT, 'external', 'JUCE');
  if (JUCE_PLUGINS.length && existsSync(join(juce, 'CMakeLists.txt'))) {
    const v = /project\(JUCE VERSION (\S+)/.exec(read('external', 'JUCE', 'CMakeLists.txt'))?.[1];
    if (!fw.get('JUCE')?.includes(`| ${v},`)) fail(`the JUCE row does not name the submodule's ${v}`);
    const types = join(juce, 'modules', 'juce_audio_processors_headless', 'format_types', 'VST3_SDK',
      'pluginterfaces', 'vst', 'vsttypes.h');
    const sdk = existsSync(types) ? /kVstVersionString\s+"VST (\S+)"/.exec(readFileSync(types, 'utf8'))?.[1] : null;
    if (!sdk) fail(`cannot read the VST3 SDK version JUCE vendors from ${types}`);
    else if (!fw.get("VST3 SDK (JUCE's copy)")?.includes(`| ${sdk},`))
      fail(`the VST3 SDK row does not name JUCE's copy, ${sdk}`);
  }
} catch (e) { fail(e.message); }

/* ------------------------------------------------------------ JUCE's own */
/* What juce_core and juce_graphics compile in from JUCE's tree, for the
 * modules and switches cmake/NiJucePlugin.cmake uses. */
try {
  const shipped = new Set(JUCE_PLUGINS.length
    ? ['zlib', 'libpng', 'IJG JPEG library', 'HarfBuzz', 'SheenBidi', 'LunaSVG', 'PlutoVG'] : []);
  const has = [...sections.keys()].some((k) => k.startsWith("JUCE's own dependencies"));
  if (shipped.size || has)
    sameSet("JUCE's dependencies", new Set(section(sections, "JUCE's own dependencies").keys()), shipped);
} catch (e) { fail(e.message); }

/* ------------------------------------------------------------ allowlist */
/* The licence of every C and C++ library a bundle carries, held to the one
 * allowlist (deny.toml, which the owner's AGENTS.md states): the row's first
 * bold licence. AGPL-3.0 is allowed for JUCE and nothing else -- GPLv3
 * section 13 is what lets the two combine, and it is not a licence a library
 * should arrive under unnoticed. */
try {
  const allow = new Set(tomlArray('deny.toml', 'licenses', 'allow'));
  for (const prefix of ['The plugin framework', "JUCE's own dependencies"]) {
    for (const [name, row] of section(sections, prefix)) {
      const licence = /\*\*([^*]+)\*\*/.exec(row.split('|').slice(-2, -1)[0] ?? '')?.[1];
      const ok = licence && (allow.has(licence) || (name === 'JUCE' && licence === 'AGPL-3.0'));
      if (!ok) fail(`\`${name}\` is under ${licence ?? 'no licence it names'}, which is not on the allowlist (deny.toml)`);
    }
  }
} catch (e) { fail(e.message); }

/* ------------------------------------------------------------ Rust std */
try {
  need(section(sections, 'The Rust standard library'), 'Rust standard library',
    'every engine links it');
} catch (e) { fail(e.message); }

/* ------------------------------------------------------------ fonts */
try {
  const fonts = section(sections, 'Bundled font');
  const shipped = new Set();
  /* Wherever the faces are kept, their licence is kept beside them, and it
   * is one text: the kit's (embedded in every editor) and the site's subset. */
  const kitOfl = join(ROOT, 'plugins', '_shared', 'ui', 'fonts', 'OFL.txt');
  const siteOfl = join(ROOT, 'site', 'src', 'uv', 'fonts', 'OFL.txt');
  for (const f of [kitOfl, siteOfl])
    if (!existsSync(f)) fail(`${f.slice(ROOT.length + 1)} is missing -- the font beside it needs it`);
  if (existsSync(kitOfl) && existsSync(siteOfl) && !readFileSync(kitOfl).equals(readFileSync(siteOfl)))
    fail('site/src/uv/fonts/OFL.txt is not the kit\'s plugins/_shared/ui/fonts/OFL.txt');
  /* A JUCE editor draws with the kit's embedded faces (plugins/_shared/ui). */
  for (const p of JUCE_PLUGINS)
    if (p.editor) shipped.add(`${p.bundle}.vst3`);
  sameSet('font bundles', new Set(fonts.keys()), shipped);

  /* The music font's section is there while a product embeds it, and only
   * then: a branch without that product has nothing to list. */
  const musical = new Set(JUCE_PLUGINS.filter((p) => p.music).map((p) => `${p.bundle}.vst3`));
  const hasMusicSection = [...sections.keys()].some((k) => k.startsWith('Bundled music font'));
  if (musical.size || hasMusicSection)
    sameSet('music font bundles', new Set(section(sections, 'Bundled music font').keys()), musical);
} catch (e) { fail(e.message); }

/* ------------------------------------------------------------ engines */
/* The workspace's own crates: every package in Cargo.lock without a source. A
 * crate from a registry is the next block's. */
try {
  const engines = section(sections, 'The engines');
  const shipped = new Set();
  for (const pkg of read('Cargo.lock').split('[[package]]').slice(1))
    if (!/^source = /m.test(pkg)) shipped.add(/^name = "([^"]+)"/m.exec(pkg)?.[1]);
  sameSet('engine crates', new Set(engines.keys()), shipped);
} catch (e) { fail(e.message); }

/* ------------------------------------------------------------ crates */
/* A string array from one of the two licence-tool configs: `key = [...]` in
 * [table], or at the top when table is ''. Enough TOML for deny.toml and
 * about.toml, which this repository writes -- not a TOML parser. */
function tomlArray(file, table, key) {
  const lines = read(file).split('\n');
  let current = '';
  for (let i = 0; i < lines.length; i++) {
    const header = /^\s*\[([^\]]+)\]\s*(#.*)?$/.exec(lines[i]);
    if (header) { current = header[1].trim(); continue; }
    if (current !== table) continue;
    const start = new RegExp(`^\\s*${key}\\s*=\\s*\\[(.*)$`).exec(lines[i]);
    if (!start) continue;
    let body = start[1];
    while (!body.includes(']') && i + 1 < lines.length) body += `\n${lines[++i]}`;
    return [...body.split(']')[0].matchAll(/"([^"]*)"/g)].map((m) => m[1]);
  }
  throw new Error(`${file} has no ${table ? `[${table}] ` : ''}${key} = [...]`);
}

/* WHAT SHIPS, BY about.toml's RULE, COMPUTED WITHOUT cargo-about: the crates
 * from a registry that the workspace reaches through normal dependencies, on
 * any platform -- never through a build or a dev one, which run on the build
 * machine. deny.toml says why no target narrows it. What this catches is a
 * Cargo.lock that moved without the notices being regenerated.
 *
 * `cargo metadata`'s resolve is wider than that graph, in three ways that
 * cargo-about and cargo-deny correct (both walk it with the krates crate), so
 * this corrects them the same way -- or the two sides would disagree about
 * crates that nothing builds:
 *
 *   - Its features are every build's at once. A node's `features` are the
 *     union over every way the workspace reaches it, build-dependencies
 *     included, where resolver 2 resolves a normal dependency's features
 *     apart from a build one's. cbindgen, every C ABI crate's
 *     build-dependency, turns on serde's `derive`; the engines never do, and
 *     nothing they build compiles serde_derive, syn, quote, proc-macro2 or
 *     unicode-ident. So the features are worked out here, from the workspace
 *     members down normal edges only: what each declaration asks for (its
 *     `features`, and `default` unless it opts out), what the parent's
 *     enabled features ask of it (`x/feature`, and `x?/feature` once x is
 *     on), and what those features turn on in turn.
 *   - It keeps an edge to an optional dependency that only a weak feature
 *     names. lexical-core's `format` asks for `lexical-write-float?/format`,
 *     which configures the writer IF something else turns it on; nothing
 *     does, and the build and `cargo tree` agree. switchedOn() asks for a
 *     strong switch instead.
 *   - It keeps an edge whose cfg() holds on no platform at all. serde_core
 *     pins serde_derive's version through `cfg(any())`, which would otherwise
 *     bring in serde_derive and the crates under it. noPlatform() drops
 *     exactly that shape and keeps every cfg() that names a platform: every
 *     platform counts. */
function shippedCrates() {
  let meta;
  try {
    meta = JSON.parse(execFileSync(process.env.CARGO ?? 'cargo',
      ['metadata', '--format-version', '1', '--locked', '--manifest-path', join(ROOT, 'Cargo.toml')],
      { encoding: 'utf8', maxBuffer: 256 << 20, stdio: ['ignore', 'pipe', 'pipe'] }));
  } catch (e) {
    throw new Error(e.code === 'ENOENT'
      ? 'cargo is not on PATH -- `. scripts/rust-env.sh` finds it'
      : `cargo metadata failed: ${e.stderr || e.message}`);
  }
  const packages = new Map(meta.packages.map((p) => [p.id, p]));
  const nodes = new Map(meta.resolve.nodes.map((n) => [n.id, n]));

  /* Each reached package's enabled features, grown until nothing moves. The
   * workspace members' are cargo's own: they are the roots, built as the
   * workspace builds them. */
  const reached = new Map(meta.workspace_members.map((id) =>
    [id, new Set(nodes.get(id)?.features ?? [])]));
  let moved = true;
  while (moved) {
    moved = false;
    for (const [id, on] of [...reached]) {
      const pkg = packages.get(id);
      for (const d of nodes.get(id)?.deps ?? []) {
        const child = packages.get(d.pkg);
        for (const k of d.dep_kinds) {
          if (k.kind !== null || noPlatform(k.target)) continue;
          for (const dep of switchedOn(pkg, on, child.name, k.target)) {
            if (!reached.has(d.pkg)) {
              reached.set(d.pkg, new Set());
              moved = true;
            }
            if (enable(child, reached.get(d.pkg), asked(pkg, on, dep))) moved = true;
          }
        }
      }
    }
  }
  return new Set([...reached.keys()].map((id) => packages.get(id))
    .filter((p) => p.source).map((p) => `${p.name} ${p.version}`));
}

/* The declarations by which `pkg`, with the features `on`, depends on the
 * package `name` through its normal dependency for `target`. A declaration
 * that is not optional always does. An optional one does only when an
 * enabled feature switches it on strongly: `dep:x`, or `x/feature` -- never
 * `x?/feature`. `pkg.features` holds the implicit `x = ["dep:x"]` of an
 * optional dependency, so one switched on by its own name is found the same
 * way. */
function switchedOn(pkg, on, name, target) {
  const bare = (t) => (t ?? '').replace(/\s+/g, '');
  const decls = pkg.dependencies.filter((dep) =>
    dep.kind === null && dep.name === name && bare(dep.target) === bare(target));
  if (!decls.length)
    throw new Error(`cargo metadata resolves ${pkg.name} -> ${name} (${target ?? 'every platform'}), ` +
                    `which ${pkg.name}'s manifest does not declare`);
  return decls.filter((dep) => {
    if (!dep.optional) return true;
    const local = dep.rename ?? dep.name;
    return [...on].some((f) => (pkg.features[f] ?? []).some((v) =>
      v === `dep:${local}` || v.startsWith(`${local}/`)));
  });
}

/* The features `pkg`, with the features `on`, asks of its dependency `dep`:
 * the declaration's own, `default` unless it opts out, and every
 * `x/feature` or `x?/feature` an enabled feature names -- x being on, a weak
 * one counts. */
function asked(pkg, on, dep) {
  const local = dep.rename ?? dep.name;
  const want = [...dep.features];
  if (dep.uses_default_features) want.push('default');
  for (const f of on) {
    for (const v of pkg.features[f] ?? []) {
      const m = /^([^/?]+)\??\/(.+)$/.exec(v);
      if (m && m[1] === local) want.push(m[2]);
    }
  }
  return want;
}

/* `want` and what it turns on within `pkg`, added to `on`. A `dep:x` or an
 * `x/feature` is about a dependency, and is read when `pkg`'s own edges are
 * walked. Whether anything was added. */
function enable(pkg, on, want) {
  let added = false;
  const todo = [...want];
  while (todo.length) {
    const f = todo.pop();
    if (on.has(f)) continue;
    on.add(f);
    added = true;
    for (const v of pkg.features[f] ?? [])
      if (!v.startsWith('dep:') && !v.includes('/')) todo.push(v);
  }
  return added;
}

/* Whether a dependency's target holds on no platform, by its shape alone: a
 * cfg() with no predicate in it -- only all(), any() and not() of nothing --
 * that comes out false, as `cfg(any())` does. krates draws the line at the
 * same place. Anything that names a predicate is some platform's, and a bare
 * target triple is one. */
function noPlatform(target) {
  const cfg = /^cfg\((.*)\)$/s.exec(target ?? '');
  const tokens = cfg?.[1].match(/[A-Za-z_][\w-]*|"[^"]*"|\S/g) ?? [];
  if (!tokens.length || tokens.some((t) => !['all', 'any', 'not', '(', ')', ','].includes(t)))
    return false;
  let i = 0;
  const expr = () => {
    const op = tokens[i];
    i += 2;                       // the operator and its '('
    const args = [];
    while (tokens[i] !== ')') {
      args.push(expr());
      if (tokens[i] === ',') i++;
    }
    i++;                          // its ')'
    return op === 'all' ? args.every(Boolean) : op === 'any' ? args.some(Boolean) : !args[0];
  };
  return !expr();
}

try {
  const allow = tomlArray('deny.toml', 'licenses', 'allow');
  const accepted = tomlArray('about.toml', '', 'accepted');
  if (allow.length !== accepted.length || !allow.every((l) => accepted.includes(l)))
    fail(`deny.toml allows ${JSON.stringify(allow)} and about.toml accepts ` +
         `${JSON.stringify(accepted)} -- one allowlist, written twice, must say one thing`);

  const listed = new Set(rowsOf('Rust crates').map(([name, version]) =>
    `${name.replace(/`/g, '')} ${version}`));
  const shipped = shippedCrates();
  for (const c of shipped)
    if (!listed.has(c)) fail(`crates: \`${c}\` ships and is not listed -- run scripts/gen-rust-notices.sh`);
  for (const c of listed)
    if (!shipped.has(c)) fail(`crates: \`${c}\` is listed and does not ship -- run scripts/gen-rust-notices.sh`);
} catch (e) { fail(e.message); }

/* ------------------------------------------------------------ test-only */
try {
  const testOnly = section(sections, 'Test-only');
  const vendored = new Set();
  if (existsSync(join(ROOT, 'external', 'doctest', 'doctest.h'))) vendored.add('doctest');
  for (const h of ['plugin_api_v1.h', 'audio_fx_api_v2.h'])
    if (existsSync(join(ROOT, 'engines', 'trance-gate', 'include', h))) vendored.add(h);
  sameSet('test-only', new Set(testOnly.keys()), vendored);
} catch (e) { fail(e.message); }

/* ------------------------------------------------------------ the bundles */
const at = process.argv.indexOf('--bundles');
if (at > 0) {
  const out = process.argv[at + 1];
  for (const p of JUCE_PLUGINS) {
    const res = join(out, `${p.bundle}.vst3`, 'Contents', 'Resources');
    for (const f of bundleResources(p))
      if (!existsSync(join(res, f))) fail(`${p.bundle}.vst3 ships without Contents/Resources/${f}`);
  }
}

if (problems.length) {
  console.error(`THIRD_PARTY_LICENSES.md and what ships disagree:\n  - ${problems.join('\n  - ')}`);
  process.exit(1);
}
console.log('every shipped dependency has a notice, and every notice is for something that ships');

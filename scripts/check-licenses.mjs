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
 * named bundles by names they no longer had (TranceGate.*, Spectrogram.*),
 * missed two plugins' fonts and a whole engine, never mentioned the JSON
 * library iPlug2 compiles into every plugin, and still carried eighty crates
 * from a Rust plugin wrapper that had been gone for months. This derives what
 * ships from the tree and holds the file to it, section by section:
 *
 *   framework  the SDK pins in scripts/fetch-sdks.sh, iPlug2 and WDL, and the
 *              JSON library iPlug2's WebView bridge includes
 *   Rust       the standard library, which every engine links
 *   editors    exactly the npm packages the editor builds bundled, read from
 *              the notice files scripts/vite-licenses.mjs writes
 *   font       one row per plugin bundle that carries the font, and OFL.txt
 *              beside every copy of it
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
 * carries LICENSE, THIRD_PARTY_LICENSES.md and its editor's notice file.
 */
import { readFileSync, readdirSync, existsSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { join } from 'node:path';
import { ROOT, listedBySection, rowsOf, section } from './licenses-lib.mjs';

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

const PLUGINS = readdirSync(join(ROOT, 'plugins'))
  .filter((d) => existsSync(join(ROOT, 'plugins', d, 'config.h')))
  .map((d) => ({
    dir: d,
    bundle: /#define\s+BUNDLE_NAME\s+"([^"]+)"/.exec(read('plugins', d, 'config.h'))?.[1],
    cmake: read('plugins', d, 'CMakeLists.txt'),
  }));

/* ------------------------------------------------------------ our own */
/* GPL-3.0-or-later, and the licence text exactly as the FSF publishes it
 * (https://www.gnu.org/licenses/gpl-3.0.txt): the GPL forbids changing the
 * document, and a hash is the only check that notices a changed word. The
 * copyright notice is not in it -- it is in the README and at the top of every
 * source file (tests/spdx.test.mjs). */
const GPL_3_0_SHA256 = '3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986';
if (createHash('sha256').update(readFileSync(join(ROOT, 'LICENSE'))).digest('hex') !== GPL_3_0_SHA256)
  fail('LICENSE is not the unmodified text of GPLv3 (https://www.gnu.org/licenses/gpl-3.0.txt)');

/* ------------------------------------------------------------ framework */
try {
  const fw = section(sections, 'The plugin framework');
  need(fw, 'iPlug2', 'every plugin is an iPlug2 plugin');
  need(fw, 'WDL', 'iPlug2 compiles WDL in');
  const pins = read('scripts', 'fetch-sdks.sh');
  const pin = (k) => new RegExp(`^${k}=(\\S+)`, 'm').exec(pins)?.[1];
  const formats = PLUGINS.map((p) => /FORMATS\s+([\s\S]*?)\n\s*UI/.exec(p.cmake)?.[1] ?? '').join(' ');
  if (/\bVST3\b/.test(formats) && !need(fw, 'VST3 SDK', 'a plugin builds VST3').includes(pin('VST3_SDK_TAG')))
    fail(`the VST3 SDK row does not name the pinned ${pin('VST3_SDK_TAG')}`);
  if (/\bCLAP\b/.test(formats)) {
    if (!need(fw, 'CLAP', 'a plugin builds CLAP').includes(pin('CLAP_SDK_TAG')))
      fail(`the CLAP row does not name the pinned ${pin('CLAP_SDK_TAG')}`);
    if (!need(fw, 'clap-helpers', 'iPlug2\'s CLAP wrapper includes it').includes(pin('CLAP_HELPERS_COMMIT').slice(0, 8)))
      fail(`the clap-helpers row does not name the pinned commit ${pin('CLAP_HELPERS_COMMIT').slice(0, 8)}`);
  }
  const bridge = 'external/iPlug2/IPlug/Extras/WebView/IPlugWebViewEditorDelegate.h';
  if (PLUGINS.some((p) => /UI\s+WEBVIEW/.test(p.cmake)) && existsSync(join(ROOT, bridge))
      && read(bridge).includes('#include "json.hpp"')) {
    const json = read('external/iPlug2/Dependencies/Extras/nlohmann/json.hpp');
    const v = /version (\d+\.\d+\.\d+)/.exec(json)?.[1];
    if (!need(fw, 'JSON for Modern C++', 'the WebView bridge compiles it into every plugin').includes(v))
      fail(`the JSON for Modern C++ row does not name the vendored ${v}`);
  }
} catch (e) { fail(e.message); }

/* ------------------------------------------------------------ Rust std */
try {
  need(section(sections, 'The Rust standard library'), 'Rust standard library',
    'every engine links it');
} catch (e) { fail(e.message); }

/* ------------------------------------------------------------ editors */
try {
  const editors = section(sections, 'The editors');
  const shipped = new Set();
  let built = 0;
  for (const p of PLUGINS) {
    const f = join(ROOT, 'plugins', p.dir, 'resources', 'web', 'assets', 'ui.js.LICENSE.txt');
    if (!existsSync(f)) continue;
    built++;
    for (const m of readFileSync(f, 'utf8').matchAll(/^(@?[^@\s]+)@\S+ -- /gm)) shipped.add(m[1]);
  }
  if (built === PLUGINS.length) {
    sameSet('editors', new Set(editors.keys()), shipped);
  } else {
    fail(`${PLUGINS.length - built} editor(s) not built, so what they bundle is unknown -- ` +
         'run `npm ci` and build first (resources/web is build output)');
  }
} catch (e) { fail(e.message); }

/* ------------------------------------------------------------ fonts */
try {
  const fonts = section(sections, 'Bundled font');
  const shipped = new Set();
  /* The font is the kit's (ui-kit/src/fonts), and every editor that imports
   * the kit's tokens.css carries it in its stylesheet -- so each of those must
   * ship OFL.txt in its fonts/ directory, and the kit must keep it beside the
   * font files. */
  const kitFonts = join(ROOT, 'ui-kit', 'src', 'fonts');
  const kitHasFont = existsSync(kitFonts) && readdirSync(kitFonts).some((f) => /\.(ttf|otf|woff2?)$/.test(f));
  if (kitHasFont && !existsSync(join(kitFonts, 'OFL.txt'))) fail('ui-kit/src/fonts has a font and no OFL.txt');
  for (const p of PLUGINS) {
    const ui = join(ROOT, 'plugins', p.dir, 'ui', 'src');
    const usesKit = existsSync(ui) && readdirSync(ui).some((f) => /\.jsx?$/.test(f)
      && readFileSync(join(ui, f), 'utf8').includes('@ultraviolet/ui/tokens.css'));
    if (!kitHasFont || !usesKit) continue;
    if (!existsSync(join(ROOT, 'plugins', p.dir, 'ui', 'public', 'fonts', 'OFL.txt')))
      fail(`plugins/${p.dir}/ui draws the kit's font and has no public/fonts/OFL.txt`);
    shipped.add(`${p.bundle}.{vst3,clap,component}`);
  }
  sameSet('font bundles', new Set(fonts.keys()), shipped);
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
 * `cargo metadata`'s resolve is a little wider than that graph, in two ways
 * that cargo-about and cargo-deny correct (both walk it with the krates
 * crate), so this corrects them the same way -- or the two sides would
 * disagree about crates that nothing builds:
 *
 *   - It keeps an edge to an optional dependency that only a weak feature
 *     names. lexical-core's `format` asks for `lexical-write-float?/format`,
 *     which configures the writer IF something else turns it on; nothing
 *     does, and the build and `cargo tree` agree. enabled() asks the node's
 *     own features instead.
 *   - It keeps an edge whose cfg() holds on no platform at all. serde_core
 *     pins serde_derive's version through `cfg(any())`, which would otherwise
 *     bring in serde_derive, syn, quote, proc-macro2 and unicode-ident.
 *     noPlatform() drops exactly that shape and keeps every cfg() that names
 *     a platform: every platform counts. */
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
  const seen = new Set();
  const todo = [...meta.workspace_members];
  while (todo.length) {
    const id = todo.pop();
    if (seen.has(id)) continue;
    seen.add(id);
    const pkg = packages.get(id);
    const node = nodes.get(id);
    for (const d of node?.deps ?? []) {
      const name = packages.get(d.pkg).name;
      if (d.dep_kinds.some((k) => k.kind === null && !noPlatform(k.target)
                               && enabled(pkg, node, name, k.target)))
        todo.push(d.pkg);
    }
  }
  return new Set([...seen].map((id) => packages.get(id))
    .filter((p) => p.source).map((p) => `${p.name} ${p.version}`));
}

/* Whether `pkg`, resolved as `node`, depends on the package `name` through its
 * normal dependency for `target`. A declaration that is not optional always
 * does. An optional one does only when an enabled feature switches it on
 * strongly: `dep:x`, or `x/feature` -- never `x?/feature`. `node.features` is
 * every feature cargo enabled, the implicit `x = ["dep:x"]` of an optional
 * dependency included, so one switched on by its own name is found the same
 * way. */
function enabled(pkg, node, name, target) {
  const bare = (t) => (t ?? '').replace(/\s+/g, '');
  const decls = pkg.dependencies.filter((dep) =>
    dep.kind === null && dep.name === name && bare(dep.target) === bare(target));
  if (!decls.length)
    throw new Error(`cargo metadata resolves ${pkg.name} -> ${name} (${target ?? 'every platform'}), ` +
                    `which ${pkg.name}'s manifest does not declare`);
  return decls.some((dep) => {
    if (!dep.optional) return true;
    const local = dep.rename ?? dep.name;
    return node.features.some((f) => (pkg.features[f] ?? []).some((v) =>
      v === `dep:${local}` || v.startsWith(`${local}/`)));
  });
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
  for (const p of PLUGINS) {
    for (const ext of ['vst3', 'component', 'clap']) {
      const res = join(out, `${p.bundle}.${ext}`, 'Contents', 'Resources');
      for (const f of ['LICENSE', 'THIRD_PARTY_LICENSES.md', 'web/assets/ui.js.LICENSE.txt', 'web/fonts/OFL.txt'])
        if (!existsSync(join(res, f))) fail(`${p.bundle}.${ext} ships without Contents/Resources/${f}`);
    }
  }
}

if (problems.length) {
  console.error(`THIRD_PARTY_LICENSES.md and what ships disagree:\n  - ${problems.join('\n  - ')}`);
  process.exit(1);
}
console.log('every shipped dependency has a notice, and every notice is for something that ships');

#!/usr/bin/env node
/*
 * Everything that ships has a notice, and every notice is for something that
 * ships.
 * Copyright (c) 2026 Torben Gräber. MIT.
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
 *   engines    exactly the crates in Cargo.lock -- and Cargo.lock holds no
 *              crate from a registry, or that crate needs its own row
 *   test-only  doctest, while it is vendored
 *
 * With --bundles it also opens the built bundles and checks that each one
 * carries LICENSE, THIRD_PARTY_LICENSES.md and its editor's notice file.
 */
import { readFileSync, readdirSync, existsSync } from 'node:fs';
import { join } from 'node:path';
import { ROOT, listedBySection, section } from './licenses-lib.mjs';

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
if (!/^MIT License\s+Copyright \(c\) 2026 Torben Gräber$/m.test(read('LICENSE')))
  fail('LICENSE is not the MIT licence with "Copyright (c) 2026 Torben Gräber"');

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
  for (const p of PLUGINS) {
    const dir = join(ROOT, 'plugins', p.dir, 'ui', 'public', 'fonts');
    if (!existsSync(dir) || !readdirSync(dir).some((f) => /\.(ttf|otf|woff2?)$/.test(f))) continue;
    if (!existsSync(join(dir, 'OFL.txt'))) fail(`plugins/${p.dir}/ui/public/fonts has a font and no OFL.txt`);
    shipped.add(`${p.bundle}.{vst3,clap,component}`);
  }
  sameSet('font bundles', new Set(fonts.keys()), shipped);
} catch (e) { fail(e.message); }

/* ------------------------------------------------------------ engines */
try {
  const engines = section(sections, 'The engines');
  const lock = read('Cargo.lock');
  const shipped = new Set();
  for (const pkg of lock.split('[[package]]').slice(1)) {
    const name = /^name = "([^"]+)"/m.exec(pkg)?.[1];
    if (/^source = /m.test(pkg)) {
      /* A registry or git crate. It would be linked into a product, so it
       * needs a row somewhere -- and today there are none. */
      if (![...sections.values()].some((s) => s.has(name)))
        fail(`Cargo.lock: \`${name}\` comes from a registry and THIRD_PARTY_LICENSES.md does not list it`);
    } else {
      shipped.add(name);
    }
  }
  sameSet('engine crates', new Set(engines.keys()), shipped);
} catch (e) { fail(e.message); }

/* ------------------------------------------------------------ test-only */
try {
  const testOnly = section(sections, 'Test-only');
  sameSet('test-only', new Set(testOnly.keys()),
    new Set(existsSync(join(ROOT, 'external', 'doctest', 'doctest.h')) ? ['doctest'] : []));
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

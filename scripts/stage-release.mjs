#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What goes into a product's release zip, checked against the built bundle.
 *
 *   node scripts/stage-release.mjs <product> <bundle-dir> <stage-dir> [--os macos|linux|windows]
 *   node scripts/stage-release.mjs <product> <bundle-dir> --dry-run [--os ...]
 *
 * For each format the product builds -- a VST3, the only one any product
 * builds (cmake/NiJucePlugin.cmake) -- the bundle in <bundle-dir> must be
 * there, must hold its binary where a host on that OS looks for it (a
 * universal arm64 + x86_64 one on macOS), and must carry the notices its
 * build puts in Contents/Resources: LICENSE, THIRD_PARTY_LICENSES.md, the
 * AGPLv3 and Apache 2.0 texts, OFL.txt for an editor's font, Bravura-OFL.txt
 * for the music font, and the moduleinfo.json JUCE writes
 * (scripts/licenses-lib.mjs, bundleResources). Then the bundle is copied into
 * <stage-dir> as it is -- on macOS by ditto, which keeps what a signed bundle
 * holds -- with LICENSE and THIRD_PARTY_LICENSES.md beside it at the top, for
 * whoever reads the download before installing it.
 *
 * NOTHING IS ADDED TO A BUNDLE HERE. A signature covers every file in it, so
 * everything a bundle carries is put there by the build before it signs; the
 * release workflow signs the staged copy for distribution afterwards, and
 * adds nothing either.
 *
 * --dry-run checks and prints the plan without copying anything:
 * tests/release.test.mjs runs it against build/out, so a staging rule that a
 * built bundle cannot meet fails in the full tier rather than on a tag.
 * --os defaults to the platform it runs on. Exits non-zero, naming every
 * problem, when anything is missing.
 */
import { cpSync, existsSync, mkdirSync, readdirSync, statSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join, posix } from 'node:path';
import { fileURLToPath } from 'node:url';
import { ROOT, juceProducts, bundleResources } from './licenses-lib.mjs';

const HOST_OS = { darwin: 'macos', linux: 'linux', win32: 'windows' }[process.platform];

/* Where a VST3 bundle keeps its binary on each OS (the VST3 SDK's module
 * layout), relative to Contents/. */
export const binaryOf = (bundle, os) => ({
  macos: posix.join('MacOS', bundle),
  linux: posix.join('x86_64-linux', `${bundle}.so`),
  windows: posix.join('x86_64-win', `${bundle}.vst3`),
})[os];

/*
 * The plan for one product: what is staged, from where, to where -- and every
 * problem the built bundle has. A macOS release is universal, and `archs`
 * reads a binary's slices (lipo by default; a test passes its own);
 * `universal: false` checks a one-slice build's bundle for everything else.
 */
export function plan(product, bundles, { os = HOST_OS, root = ROOT, archs = lipoArchs, universal = os === 'macos' } = {}) {
  const p = juceProducts(root).find((x) => x.dir === product);
  if (!p) return { problems: [`${product} builds no plugin (no ni_add_juce_plugin in plugins/${product})`] };
  if (!os) return { problems: [`no release is staged on ${process.platform}`] };

  const problems = [];
  const src = join(bundles, `${p.bundle}.vst3`);
  const contents = join(src, 'Contents');
  if (!existsSync(src) || !statSync(src).isDirectory()) {
    problems.push(`${src} is missing`);
  } else {
    const binary = join(contents, binaryOf(p.bundle, os));
    if (!existsSync(binary)) {
      problems.push(`${p.bundle}.vst3 has no ${os} binary at Contents/${binaryOf(p.bundle, os)}`);
    } else if (os === 'macos' && universal) {
      const slices = archs(binary);
      for (const a of ['arm64', 'x86_64'])
        if (!slices.includes(a)) problems.push(`${p.bundle}.vst3 is not universal: no ${a} slice (${slices.join(' ') || 'none'})`);
    }
    for (const f of bundleResources(p))
      if (!existsSync(join(contents, 'Resources', f)))
        problems.push(`${p.bundle}.vst3 has no Contents/Resources/${f}`);
  }
  for (const f of ['LICENSE', 'THIRD_PARTY_LICENSES.md'])
    if (!existsSync(join(root, f))) problems.push(`${f} is missing from the checkout`);

  return {
    product, bundle: p.bundle, os, problems,
    copies: [
      { from: src, to: `${p.bundle}.vst3`, bundle: true },
      { from: join(root, 'LICENSE'), to: 'LICENSE' },
      { from: join(root, 'THIRD_PARTY_LICENSES.md'), to: 'THIRD_PARTY_LICENSES.md' },
    ],
  };
}

function lipoArchs(binary) {
  try {
    return execFileSync('lipo', ['-archs', binary], { encoding: 'utf8' }).trim().split(/\s+/);
  } catch {
    return [];
  }
}

export function stage(p, out) {
  mkdirSync(out, { recursive: true });
  for (const c of p.copies) {
    const to = join(out, c.to);
    if (c.bundle && p.os === 'macos' && process.platform === 'darwin')
      execFileSync('ditto', [c.from, to]);
    else
      cpSync(c.from, to, { recursive: true, verbatimSymlinks: true });
  }
  return readdirSync(out).sort();
}

function main(argv) {
  const args = [...argv];
  const take = (flag) => {
    const i = args.indexOf(flag);
    if (i < 0) return undefined;
    const [, v] = args.splice(i, 2);
    return v;
  };
  const os = take('--os') ?? HOST_OS;
  const dry = args.includes('--dry-run');
  const rest = args.filter((a) => a !== '--dry-run');
  const [product, bundles, out] = rest;
  if (!product || !bundles || (!dry && !out) || rest.length > 3) {
    console.error('usage: stage-release.mjs <product> <bundle-dir> (<stage-dir> | --dry-run) [--os macos|linux|windows]');
    process.exit(2);
  }
  const p = plan(product, bundles, { os });
  if (p.problems.length) {
    console.error(`cannot stage ${product} for ${os}:\n  - ${p.problems.join('\n  - ')}`);
    process.exit(1);
  }
  for (const c of p.copies) console.log(`${dry ? 'would stage' : 'staging'} ${c.to}  <-  ${c.from}`);
  if (!dry) console.log(`staged in ${out}: ${stage(p, out).join(', ')}`);
}

if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) main(process.argv.slice(2));

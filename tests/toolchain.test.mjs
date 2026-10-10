// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * ONE RUST TOOLCHAIN, WHEREVER IT IS BUILT.
 *
 * rust-toolchain.toml pins the compiler the Mac and a GitHub runner use; the
 * two images that build outside the Mac (the Move module's and the Linux
 * cross-build's) install their own and must install the same one, or the
 * plugin and the module of one product are built by two compilers and the
 * render goldens -- hashes of floating-point output -- can disagree for no
 * reason anyone changed. Every base image is pinned by digest for the same
 * reason: a tag moves when its publisher rebuilds it.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
const read = (p) => readFileSync(join(ROOT, p), 'utf8');

const channel = () => {
  const m = /^\s*channel\s*=\s*"([^"]+)"/m.exec(read('rust-toolchain.toml'));
  assert.ok(m, 'rust-toolchain.toml names a channel');
  return m[1];
};

const IMAGES = ['modules/_shared/Dockerfile', 'tools/docker/linux/Dockerfile'];

test('the toolchain is pinned to an exact version, not a moving channel', () => {
  assert.match(channel(), /^\d+\.\d+\.\d+$/, 'a version such as 1.99.0, not "stable"');
});

for (const image of IMAGES) {
  test(`${image} installs the pinned toolchain and uses it`, () => {
    const src = read(image);
    const arg = /^ARG RUST_TOOLCHAIN=(\S+)$/m.exec(src);
    assert.ok(arg, 'declares ARG RUST_TOOLCHAIN');
    assert.equal(arg[1], channel(), `RUST_TOOLCHAIN is rust-toolchain.toml's channel`);
    assert.match(src, /^ENV RUSTUP_TOOLCHAIN=\$RUST_TOOLCHAIN$/m,
      'RUSTUP_TOOLCHAIN overrides the checkout\'s file inside the image');
  });

  test(`${image} starts from a base pinned by digest`, () => {
    const froms = read(image).split('\n').filter((l) => /^FROM\s/.test(l));
    assert.ok(froms.length > 0);
    for (const from of froms) {
      const base = from.split(/\s+/)[1];
      /* A stage built on an earlier stage, or on an ARG that is itself
       * checked to carry a digest. */
      if (/^\$\{?\w+\}?$/.test(base)) {
        const name = base.replace(/[${}]/g, '');
        const decl = new RegExp(`^ARG ${name}=(\\S+)$`, 'm').exec(read(image));
        assert.ok(decl, `${name} is declared with a default`);
        assert.match(decl[1], /@sha256:[0-9a-f]{64}$/, `${name} is pinned by digest`);
      } else {
        assert.match(base, /@sha256:[0-9a-f]{64}$/, `${base} is pinned by digest`);
      }
    }
  });
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The engine and the module's declaration, as the tests need them: the real
 * tg_web.wasm from the build tree, and CHAIN_PARAMS as the kit's build reads
 * it from tg-move's params.rs -- the very text the device serves.
 *
 * The wasm is a build product, not a fixture: missing, the tests that need it
 * fail and say how to make it, rather than passing on nothing.
 */
import { readFileSync, existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import { capiOf } from '@ultraviolet/ui/capi';
import { readChainParams } from '@ultraviolet/ui/vite';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');
export const WASM = join(ROOT, 'target', 'wasm32-unknown-unknown', 'release', 'tg_web.wasm');

/** A fresh instance of the engine's C ABI. */
export async function loadEngine() {
  if (!existsSync(WASM)) {
    throw new Error(`${WASM} is missing: run scripts/build-web.sh trance-gate`);
  }
  const { instance } = await WebAssembly.instantiate(readFileSync(WASM), {});
  return capiOf(instance, 'tg');
}

/** The module's chain_params, parsed. */
export const chainParams = () => readChainParams('tg-move');

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The real engine for a node test: sc_web.wasm, as scripts/build-web.sh
 * builds it, behind the harness's own adapter (src/harness/engine.js).
 *
 * A TEST THAT NEEDS IT AND CANNOT HAVE IT SAYS SO. The wasm is a build output,
 * so a fresh checkout has none; the tests that need it are skipped with the
 * command that makes it, rather than passing over the engine in silence.
 */
import { existsSync, readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import { capiOf } from '../../../kit/src/lib/capi.js';
import { createEngine } from '../../src/harness/engine.js';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');
export const WASM = join(ROOT, 'target', 'wasm32-unknown-unknown', 'release', 'sc_web.wasm');

export const NO_WASM = existsSync(WASM)
  ? false
  : 'sc_web.wasm is not built: run scripts/build-web.sh side-chain';

/** A fresh engine on a fresh instance, so no test sees another's state. */
export async function realEngine(options) {
  const { instance } = await WebAssembly.instantiate(readFileSync(WASM), {});
  return createEngine(capiOf(instance, 'sc'), options);
}

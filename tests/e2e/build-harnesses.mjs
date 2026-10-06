// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Build every editor's review harness before the suite runs.
 *
 * Each harness's build.sh runs vite and copies the bundle in beside the page,
 * so the suite drives the SAME bundle the plugin ships -- not a dev server.
 * Serial, because the four vite builds are CPU-bound and share node_modules.
 */
import { execFileSync } from 'node:child_process';
import { existsSync, readdirSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = fileURLToPath(new URL('../../', import.meta.url));

export const HARNESSES = readdirSync(join(ROOT, 'plugins'))
  .map((p) => join(ROOT, 'plugins', p, 'ui', 'test', 'harness', 'build.sh'))
  .filter((f) => existsSync(f));

/*
 * Under NI_E2E_COVERAGE each is built with a source map into build/e2e/web
 * instead, so scripts/e2e-coverage.mjs can map Chrome's coverage of the bundle
 * back to the sources -- and resources/web, which ships, never carries one.
 */
export default function buildHarnesses() {
  for (const script of HARNESSES) {
    const plugin = script.split('/plugins/')[1].split('/')[0];
    const env = { ...process.env };
    if (process.env.NI_E2E_COVERAGE) env.NI_HARNESS_SOURCEMAP = join(ROOT, 'build', 'e2e', 'web', plugin);
    execFileSync('sh', [script], { stdio: ['ignore', 'inherit', 'inherit'], env });
  }
}

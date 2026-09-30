/*
 * Build every editor's review harness before the suite runs.
 * Copyright (c) 2026 Torben Gräber. MIT.
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

export default function buildHarnesses() {
  for (const script of HARNESSES) {
    execFileSync('sh', [script], { stdio: ['ignore', 'inherit', 'inherit'] });
  }
}

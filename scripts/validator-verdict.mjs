#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * clap-validator's verdict, held to the known-failures manifest.
 *
 *   clap-validator validate --json <bundle>.clap \
 *     | node scripts/validator-verdict.mjs <bundle> [manifest]
 *
 * WHY A MANIFEST AND NOT A SKIP LIST. Some of clap-validator's failures are
 * iPlug2's, fixed by patches in docs/iplug2-patches that are proposed but not
 * applied, and one is clap-validator's own bug. A validator stage that is red on
 * those forever teaches everybody to stop reading it; one that skips them stops
 * seeing them come back. So the manifest (tests/validators.known.json) names
 * each expected failure by bundle and test id, with the patch that fixes it, and
 * this is STRICT BOTH WAYS:
 *
 *   - a failure the manifest does not list fails the stage -- a regression;
 *   - a listed failure that no longer fails fails it too -- the fix landed, and
 *     the entry must go, so the list can only shrink.
 *
 * Warnings are printed and never fail anything. A crash counts as a failure:
 * clap-validator reports its own panics that way.
 *
 * Exits 0 when the run matches the manifest exactly, 1 when it does not, 2 on
 * bad input. The logic is exported for tests/validator_verdict.test.mjs.
 */
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

export const VALIDATOR = 'clap-validator';
const FAILING = new Set(['failed', 'crashed']);

/** clap-validator's --json output -> [{ test, status, details }]. */
export function parseResults(text) {
  const json = JSON.parse(text);
  if (!Array.isArray(json?.results)) throw new Error('not clap-validator --json output: no "results" array');
  return json.results.map((r) => {
    const kind = Object.keys(r.test ?? {})[0];
    const test = r.test?.[kind]?.test;
    if (!test || typeof r.status?.code !== 'string') throw new Error('a result without a test id or a status');
    return { test, status: r.status.code, details: r.status.details ?? '' };
  });
}

/** The manifest's entries for one bundle and this validator: test id -> entry. */
export function knownFor(manifest, bundle) {
  const out = new Map();
  for (const e of manifest.known ?? []) {
    if (e.bundle === bundle && e.validator === VALIDATOR) out.set(e.test, e);
  }
  return out;
}

/**
 * The comparison. `unexpected`: failing and not listed. `fixed`: listed and not
 * failing (passed, skipped, or no longer run). `expected`: listed and failing.
 * `warnings`: informational only.
 */
export function verdict(results, known) {
  const failing = new Map(results.filter((r) => FAILING.has(r.status)).map((r) => [r.test, r]));
  const unexpected = [...failing.values()].filter((r) => !known.has(r.test));
  const expected = [...failing.values()].filter((r) => known.has(r.test));
  const fixed = [...known.values()].filter((e) => !failing.has(e.test)).map((e) => ({
    ...e,
    now: results.find((r) => r.test === e.test)?.status ?? 'not run',
  }));
  const warnings = results.filter((r) => r.status === 'warning');
  return { ok: unexpected.length === 0 && fixed.length === 0, unexpected, expected, fixed, warnings };
}

export function report(bundle, v) {
  const lines = [];
  const first = (s) => String(s).split('\n').find((l) => l.trim()) ?? '';
  for (const r of v.expected) lines.push(`  known   ${bundle} ${r.test} (${r.status})`);
  for (const r of v.warnings) lines.push(`  warning ${bundle} ${r.test}: ${first(r.details)}`);
  for (const r of v.unexpected) lines.push(`  NEW     ${bundle} ${r.test} (${r.status}): ${first(r.details)}`);
  for (const e of v.fixed) {
    lines.push(`  FIXED   ${bundle} ${e.test} is listed as known and is now ${e.now} -- ` +
               'remove it from tests/validators.known.json');
  }
  lines.push(v.ok
    ? `${bundle}: clap-validator matches the known-failures manifest (${v.expected.length} known)`
    : `${bundle}: clap-validator does NOT match the known-failures manifest`);
  return lines.join('\n');
}

const main = () => {
  const [bundle, manifestPath = fileURLToPath(new URL('../tests/validators.known.json', import.meta.url))] =
    process.argv.slice(2);
  if (!bundle) {
    console.error('usage: clap-validator validate --json <b>.clap | validator-verdict.mjs <bundle> [manifest]');
    process.exit(2);
  }
  let results, manifest;
  try {
    results = parseResults(readFileSync(0, 'utf8'));
    manifest = JSON.parse(readFileSync(manifestPath, 'utf8'));
  } catch (e) {
    console.error(`validator-verdict: ${e.message}`);
    process.exit(2);
  }
  const v = verdict(results, knownFor(manifest, bundle));
  console.log(report(bundle, v));
  process.exit(v.ok ? 0 : 1);
};

if (process.argv[1] === fileURLToPath(import.meta.url)) main();

#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * WHAT A TEST RUN ACTUALLY COVERED, written down, and the gate on it.
 *
 *   node scripts/test-summary.mjs --junit <ctest.xml> --capabilities <file>
 *        --title <text> [--waive <name>]... [--gate] [--out <file.md>]
 *
 * A green ctest says that nothing which RAN failed. It says nothing about what
 * did not run: a test Docker was needed for, and Docker was missing, is simply
 * not registered (ni_missing_capability, cmake/NiTest.cmake); a test whose
 * input was missing exits with its skip code and ctest counts it as passed.
 * This reads both -- ctest's JUnit report and <build>/ni-capabilities.txt --
 * and writes one Markdown summary: the counts, every test that did not run and
 * why, every capability the build lacked, and the toolchain. A release
 * attaches it, so what a published build was tested with is on record.
 *
 * --gate fails the run (exit 1) when anything did not run, unless the run
 * waived it by name: a capability (`docker`, `rosetta`, `node`) or a test
 * (`schwung_pin`). A waiver is written into the summary too, so a gap that was
 * accepted is still a gap on the record. Without --gate it only reports.
 */
import { readFileSync, writeFileSync, existsSync } from 'node:fs';
import { execFileSync } from 'node:child_process';

/** The test cases in a ctest JUnit report: name, status, why it did not run,
 * and its labels. */
export function parseJunit(xml) {
  const cases = [];
  const re = /<testcase\s+name="([^"]*)"[^>]*\sstatus="(\w+)"\s*>([\s\S]*?)<\/testcase>/g;
  for (const m of xml.matchAll(re)) {
    const body = m[3];
    const skipped = /<skipped\s+message="([^"]*)"/.exec(body);
    const labels = /<property\s+name="cmake_labels"\s+value="([^"]*)"/.exec(body);
    cases.push({
      name: unescape(m[1]),
      status: m[2],
      why: skipped ? unescape(skipped[1]) : '',
      labels: labels ? labels[1].split(';').filter(Boolean) : [],
    });
  }
  return cases;
}

function unescape(s) {
  return s.replace(/&quot;/g, '"').replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&apos;/g, "'").replace(/&amp;/g, '&');
}

/** The capabilities a build lacked: `<name> <what is not tested>` per line. */
export function parseCapabilities(text) {
  return text
    .split('\n')
    .map((l) => l.trim())
    .filter(Boolean)
    .map((l) => {
      const at = l.indexOf(' ');
      return at < 0 ? { name: l, what: '' } : { name: l.slice(0, at), what: l.slice(at + 1) };
    });
}

/** Everything that did not run, each marked waived or not. */
export function gaps(cases, capabilities, waived) {
  const w = new Set(waived);
  const notRun = cases
    .filter((c) => c.status !== 'run' && c.status !== 'fail')
    .map((c) => ({ kind: 'test', name: c.name, why: c.why || c.status, waived: w.has(c.name) }));
  const missing = capabilities.map((c) => ({ kind: 'capability', name: c.name, why: c.what, waived: w.has(c.name) }));
  return [...missing, ...notRun];
}

/** The summary as Markdown. */
export function render({ title, cases, found, toolchain }) {
  const run = cases.filter((c) => c.status === 'run').length;
  const failed = cases.filter((c) => c.status === 'fail').length;
  const open = found.filter((g) => !g.waived);
  const lines = [
    `# ${title}`,
    '',
    `- tests: ${cases.length} registered, ${run} passed, ${failed} failed, ${cases.length - run - failed} did not run`,
    `- gaps: ${found.length} (${open.length} not waived)`,
  ];
  for (const [k, v] of Object.entries(toolchain)) lines.push(`- ${k}: ${v}`);
  if (found.length) {
    lines.push('', '## Not tested in this run', '', '| what | name | why | waived |', '|---|---|---|---|');
    for (const g of found) lines.push(`| ${g.kind} | \`${g.name}\` | ${g.why.replace(/\|/g, '\\|')} | ${g.waived ? 'yes' : 'no'} |`);
  }
  const byProduct = new Map();
  for (const c of cases) {
    const product = (c.labels.find((l) => l.startsWith('product:')) || 'product:?').slice(8);
    const row = byProduct.get(product) || { run: 0, failed: 0, other: 0 };
    if (c.status === 'run') row.run++;
    else if (c.status === 'fail') row.failed++;
    else row.other++;
    byProduct.set(product, row);
  }
  lines.push('', '## By product', '', '| product | passed | failed | did not run |', '|---|---|---|---|');
  for (const [p, r] of [...byProduct].sort()) lines.push(`| ${p} | ${r.run} | ${r.failed} | ${r.other} |`);
  return lines.join('\n') + '\n';
}

function tool(cmd, args) {
  try {
    return execFileSync(cmd, args, { encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'] }).split('\n')[0].trim();
  } catch {
    return 'not found';
  }
}

function main(argv) {
  const opt = { waive: [], gate: false };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === '--gate') opt.gate = true;
    else if (a === '--waive') opt.waive.push(...argv[++i].split(',').filter(Boolean));
    else if (a.startsWith('--')) opt[a.slice(2)] = argv[++i];
  }
  if (!opt.junit || !opt.title) {
    process.stderr.write('usage: test-summary.mjs --junit <xml> --title <text> [--capabilities <file>] [--waive <name>]... [--gate] [--out <md>]\n');
    return 2;
  }
  const cases = parseJunit(readFileSync(opt.junit, 'utf8'));
  const capabilities = opt.capabilities && existsSync(opt.capabilities)
    ? parseCapabilities(readFileSync(opt.capabilities, 'utf8')) : [];
  const found = gaps(cases, capabilities, opt.waive);
  const toolchain = {
    commit: tool('git', ['rev-parse', '--short', 'HEAD']),
    rustc: tool('rustc', ['--version']),
    cmake: tool('cmake', ['--version']),
  };
  const md = render({ title: opt.title, cases, found, toolchain });
  if (opt.out) writeFileSync(opt.out, md);
  const open = found.filter((g) => !g.waived);
  for (const g of found) {
    process.stdout.write(`  ${g.waived ? 'waived ' : 'MISSING'} ${g.kind} ${g.name}: ${g.why}\n`);
  }
  if (opt.gate && open.length) {
    process.stderr.write(`\n${open.length} thing(s) were not tested. Provide them, or waive each by name `
      + `(scripts/test.sh full --waive <name>) so the gap is on the record.\n`);
    return 1;
  }
  return 0;
}

if (import.meta.url === `file://${process.argv[1]}`) {
  process.exit(main(process.argv.slice(2)));
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * scripts/test-summary.mjs: what a run did not test is counted, named and,
 * unless waived, fails the gate -- a test that skipped and a capability the
 * build lacked alike.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, writeFileSync, readFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { parseJunit, parseCapabilities, gaps, render } from '../scripts/test-summary.mjs';

const SCRIPT = join(dirname(fileURLToPath(import.meta.url)), '..', 'scripts', 'test-summary.mjs');

const JUNIT = `<?xml version="1.0" encoding="UTF-8"?>
<testsuite name="(empty)" tests="4" failures="1" disabled="0" skipped="1">
  <testcase name="bc_core" classname="bc_core" time="0.1" status="run">
    <properties><property name="cmake_labels" value="full;product:bit-crusher;quick"/></properties>
    <system-out>ok</system-out>
  </testcase>
  <testcase name="timing" classname="timing" time="0.1" status="run">
    <properties><property name="cmake_labels" value="full;product:shared;quick"/></properties>
  </testcase>
  <testcase name="site_links" classname="site_links" time="0" status="notrun">
    <skipped message="SKIP_RETURN_CODE=2"/>
    <properties><property name="cmake_labels" value="full;product:shared;site"/></properties>
  </testcase>
  <testcase name="bc_host" classname="bc_host" time="1" status="fail">
    <properties><property name="cmake_labels" value="full;host;product:bit-crusher"/></properties>
  </testcase>
</testsuite>`;

test('every test case is read with its status, its reason and its product', () => {
  const cases = parseJunit(JUNIT);
  assert.deepEqual(cases.map((c) => [c.name, c.status]), [
    ['bc_core', 'run'], ['timing', 'run'], ['site_links', 'notrun'], ['bc_host', 'fail'],
  ]);
  assert.equal(cases[2].why, 'SKIP_RETURN_CODE=2');
  assert.ok(cases[0].labels.includes('product:bit-crusher'));
});

test('a skipped test and a missing capability are gaps; a failure is not a gap but a failure', () => {
  const found = gaps(parseJunit(JUNIT), parseCapabilities('docker the Move tests are not registered\n\n'), ['docker']);
  assert.deepEqual(found.map((g) => [g.kind, g.name, g.waived]), [
    ['capability', 'docker', true], ['test', 'site_links', false],
  ]);
});

test('the summary counts, lists the gaps and splits by product', () => {
  const cases = parseJunit(JUNIT);
  const md = render({ title: 'T', cases, found: gaps(cases, [], []), toolchain: { rustc: 'rustc 1.99.0' } });
  assert.match(md, /4 registered, 2 passed, 1 failed, 1 did not run/);
  assert.match(md, /\| test \| `site_links` \| SKIP_RETURN_CODE=2 \| no \|/);
  assert.match(md, /\| bit-crusher \| 1 \| 1 \| 0 \|/);
  assert.match(md, /- rustc: rustc 1.99.0/);
});

test('the gate fails on an unwaived gap, passes once each is waived, and writes the summary', () => {
  const dir = mkdtempSync(join(tmpdir(), 'ni-summary-'));
  writeFileSync(join(dir, 'j.xml'), JUNIT);
  writeFileSync(join(dir, 'caps.txt'), 'docker the Move tests are not registered\n');
  const run = (...extra) => spawnSync(process.execPath, [SCRIPT, '--junit', join(dir, 'j.xml'),
    '--capabilities', join(dir, 'caps.txt'), '--title', 'full', '--gate', '--out', join(dir, 's.md'), ...extra],
  { encoding: 'utf8' });
  assert.equal(run().status, 1);
  assert.equal(run('--waive', 'docker').status, 1);
  assert.equal(run('--waive', 'docker,site_links').status, 0);
  assert.match(readFileSync(join(dir, 's.md'), 'utf8'), /\| capability \| `docker` \| .* \| yes \|/);
  /* Without the gate it only reports. */
  const report = spawnSync(process.execPath, [SCRIPT, '--junit', join(dir, 'j.xml'), '--title', 'x'], { encoding: 'utf8' });
  assert.equal(report.status, 0);
  assert.equal(spawnSync(process.execPath, [SCRIPT], { encoding: 'utf8' }).status, 2);
});

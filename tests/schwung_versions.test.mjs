// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Schwung versions a module names, held to each other.
 *
 *   modules/_shared/schwung.version   the Schwung the modules are TESTED
 *                                     against (modules/_shared/schwung.sh)
 *   module.json "min_host_version"    the oldest Schwung a module RUNS on
 *   the module's help and its docs    what a user is told about both
 *
 * A module's minimum can be no newer than the host it was tested on, and
 * what the user reads must be what module.json and the pin say -- three
 * spellings of two numbers, which drift one edit at a time unless something
 * holds them together.
 *
 * The pin's format is read by release tooling too, so it is held to the
 * trivial one it documents: KEY=VALUE lines and comments.
 *
 *   node --test tests/schwung_versions.test.mjs
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, readdirSync, existsSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
const read = (...p) => readFileSync(join(ROOT, ...p), 'utf8');

const SEMVER = /^(\d+)\.(\d+)\.(\d+)$/;
const parts = (v) => v.match(SEMVER).slice(1).map(Number);
const newer = (a, b) => {
  const [x, y] = [parts(a), parts(b)];
  for (let i = 0; i < 3; i++) if (x[i] !== y[i]) return x[i] > y[i];
  return false;
};

function readPin() {
  const pin = {};
  for (const line of read('modules', '_shared', 'schwung.version').split('\n')) {
    if (line === '' || line.startsWith('#')) continue;
    const m = line.match(/^([A-Z_]+)=(\S+)$/);
    assert.ok(m, `schwung.version: "${line}" is not KEY=VALUE`);
    assert.ok(!(m[1] in pin), `schwung.version sets ${m[1]} twice`);
    pin[m[1]] = m[2];
  }
  return pin;
}

const MODULES = readdirSync(join(ROOT, 'modules'))
  .filter((d) => existsSync(join(ROOT, 'modules', d, 'module.json')));

test('the pin is the three keys it documents, well formed', () => {
  const pin = readPin();
  assert.deepEqual(Object.keys(pin).sort(), ['SCHWUNG_COMMIT', 'SCHWUNG_REPO', 'SCHWUNG_TAG']);
  assert.match(pin.SCHWUNG_REPO, /^[\w.-]+\/[\w.-]+$/, 'SCHWUNG_REPO is owner/repo');
  assert.match(pin.SCHWUNG_TAG, /^v\d+\.\d+\.\d+$/, 'SCHWUNG_TAG is a release tag');
  assert.match(pin.SCHWUNG_COMMIT, /^[0-9a-f]{40}$/, 'SCHWUNG_COMMIT is a full commit SHA');
});

for (const m of MODULES) {
  test(`modules/${m}: the host it asks for is one it was tested on`, () => {
    const json = JSON.parse(read('modules', m, 'module.json'));
    if (!('min_host_version' in json)) return;
    const min = json.min_host_version;
    assert.match(min, SEMVER, 'min_host_version is x.y.z, no "v": Schwung compares it part by part');
    const tested = readPin().SCHWUNG_TAG.slice(1);
    assert.ok(!newer(min, tested),
      `min_host_version ${min} is newer than the Schwung the module is tested against (${tested})`);
  });
}

test('the Trance Gate tells its user the host it needs and the host it was tested on', () => {
  const min = JSON.parse(read('modules', 'trance-gate', 'module.json')).min_host_version;
  const tested = readPin().SCHWUNG_TAG.slice(1);
  const docs = read('plugins', 'trance-gate', 'docs', 'schwung.md');
  assert.ok(docs.includes(`**${min} or newer**`), `docs/schwung.md does not say "${min} or newer"`);
  assert.ok(docs.includes(`tested against Schwung ${tested}`),
    `docs/schwung.md does not say it is tested against Schwung ${tested}`);
  const help = JSON.parse(read('modules', 'trance-gate', 'help.json'));
  const lines = [];
  const walk = (n) => { lines.push(...(n.lines || [])); (n.children || []).forEach(walk); };
  walk(help);
  assert.ok(lines.join(' ').includes(`${min} or newer`), `help.json does not say "${min} or newer"`);
});

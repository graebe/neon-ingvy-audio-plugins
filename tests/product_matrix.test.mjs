// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Which shells each product has, said once and held to the tree.
 *
 * A product's README front matter says where it runs (`hosts`), and when
 * that leaves out the Move, why (`notOnMove`): the site renders both. Those
 * are the product matrix, and the owner's decisions live there -- a product
 * is Live-only on purpose, never by omission. This holds the matrix to what
 * the tree builds:
 *
 *   - `move` is among the hosts exactly when modules/<product>/ exists
 *   - `notOnMove` is given exactly when the Move is not a host
 *   - every module in release.json is one that modules/ builds
 *
 *   node --test tests/product_matrix.test.mjs
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { existsSync, readdirSync, readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');

/* The front matter's two fields this test reads, by line: `hosts: [a, b]`
 * and whether a `notOnMove:` key is present. */
function matrix(product) {
  const text = readFileSync(join(ROOT, 'plugins', product, 'README.md'), 'utf8');
  const front = text.match(/^---\n([\s\S]*?)\n---/)?.[1] ?? '';
  const hosts = front.match(/^hosts:\s*\[([^\]]*)\]/m)?.[1].split(',').map((h) => h.trim()) ?? [];
  return { hosts, notOnMove: /^notOnMove:/m.test(front) };
}

const products = readdirSync(join(ROOT, 'plugins'), { withFileTypes: true })
  .filter((d) => d.isDirectory() && !d.name.startsWith('_') && existsSync(join(ROOT, 'plugins', d.name, 'README.md')))
  .map((d) => d.name);
const modules = readdirSync(join(ROOT, 'modules'), { withFileTypes: true })
  .filter((d) => d.isDirectory() && !d.name.startsWith('_'))
  .map((d) => d.name);

test('every product says where it runs', () => {
  assert.ok(products.length >= 8, `found ${products.length} products`);
  for (const p of products)
    assert.ok(matrix(p).hosts.includes('live'), `${p}: every product is a Live plugin`);
});

test('the Move is a host exactly when the product has a module', () => {
  for (const p of products) {
    const onMove = matrix(p).hosts.includes('move');
    assert.equal(onMove, modules.includes(p),
      onMove ? `${p} says it runs on the Move, and modules/${p} does not exist`
             : `modules/${p} exists, and ${p}'s hosts leave out the Move`);
  }
  for (const m of modules)
    assert.ok(products.includes(m), `modules/${m} has no plugins/${m}/README.md`);
});

test('a Live-only product says why', () => {
  for (const p of products) {
    const { hosts, notOnMove } = matrix(p);
    assert.equal(notOnMove, !hosts.includes('move'),
      notOnMove ? `${p} runs on the Move and still gives notOnMove`
                : `${p} is Live-only and gives no notOnMove`);
  }
});

test('every module the Move manager offers is one modules/ builds', () => {
  const ids = modules.map((m) => JSON.parse(readFileSync(join(ROOT, 'modules', m, 'module.json'), 'utf8')).id);
  const offered = Object.keys(JSON.parse(readFileSync(join(ROOT, 'release.json'), 'utf8')).modules ?? {});
  for (const id of offered)
    assert.ok(ids.includes(id), `release.json offers ${id}, which no modules/*/module.json declares`);
});

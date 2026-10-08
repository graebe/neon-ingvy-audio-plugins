// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What the licence check counts as shipping, on a graph small enough to read.
 *
 * check-licenses.mjs holds THIRD_PARTY_LICENSES.md to the crates a build of
 * the workspace links, and decides which those are with resolveFeatures:
 * cargo's own feature resolution, through normal edges only. The graph below
 * is `cargo metadata`'s shape, cut down to the fields it reads, with one case
 * per rule -- the build dependency that asks for a feature the shipping graph
 * does not, the dev dependency likewise, a weak feature, an implicit feature,
 * `default-features = false`, and a cfg() that holds nowhere.
 *
 *   node --test tests/licenses_resolve.test.mjs
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { noPlatform, resolveFeatures } from '../scripts/licenses-lib.mjs';

/* A package: its features table and its declared dependencies. */
const pkg = (name, features = {}, dependencies = []) => ({
  id: name, name, features,
  dependencies: dependencies.map((d) => ({
    kind: null, target: null, optional: false, uses_default_features: true,
    features: [], rename: null, ...d,
  })),
});
/* A resolved edge, of the kinds `cargo metadata` lists. */
const edge = (to, ...kinds) => ({
  pkg: to, dep_kinds: (kinds.length ? kinds : [null]).map((k) =>
    typeof k === 'object' && k !== null ? k : { kind: k, target: null }),
});

function graph() {
  const packages = [
    pkg('app', {}, [
      { name: 'serde' },
      { name: 'codegen', kind: 'build' },
      { name: 'bench', kind: 'dev' },
      { name: 'lexical', uses_default_features: false, features: ['parse'] },
      { name: 'sys', target: 'cfg(any())' },
      { name: 'unix', target: 'cfg(unix)' },
    ]),
    pkg('serde', { default: ['std'], std: [], derive: ['dep:serde_derive'] },
      [{ name: 'serde_derive', optional: true }]),
    pkg('serde_derive', {}, [{ name: 'syn' }]),
    pkg('syn'),
    pkg('codegen', {}, [{ name: 'serde', features: ['derive'] }]),
    pkg('bench', {}, [{ name: 'serde', features: ['derive'] }]),
    pkg('lexical', {
      default: ['std'], std: [], parse: ['lexical-parse'],
      format: ['lexical-write?/format'],
    }, [
      { name: 'lexical-parse', optional: true },
      { name: 'lexical-write', optional: true },
    ]),
    pkg('lexical-parse'),
    pkg('lexical-write', { format: [] }),
    pkg('sys'),
    pkg('unix'),
  ];
  const nodes = [
    { id: 'app', deps: [
      edge('serde'), edge('codegen', 'build'), edge('bench', 'dev'), edge('lexical'),
      edge('sys', { kind: null, target: 'cfg(any())' }),
      edge('unix', { kind: null, target: 'cfg(unix)' }),
    ] },
    { id: 'serde', deps: [edge('serde_derive')] },
    { id: 'serde_derive', deps: [edge('syn')] },
    { id: 'syn', deps: [] },
    { id: 'codegen', deps: [edge('serde')] },
    { id: 'bench', deps: [edge('serde')] },
    /* cargo metadata keeps the edge a weak feature names; the resolver must not follow it. */
    { id: 'lexical', deps: [edge('lexical-parse'), edge('lexical-write')] },
    { id: 'lexical-parse', deps: [] },
    { id: 'lexical-write', deps: [] },
    { id: 'sys', deps: [] },
    { id: 'unix', deps: [] },
  ];
  return [
    new Map(packages.map((p) => [p.id, p])),
    new Map(nodes.map((n) => [n.id, n])),
  ];
}

test('a feature only a build or dev dependency asks for does not ship', () => {
  const [packages, nodes] = graph();
  const reached = resolveFeatures(['app'], packages, nodes);
  assert.ok(reached.has('serde'));
  assert.ok(!reached.get('serde').has('derive'), 'asked only by a build and a dev dependency');
  for (const never of ['serde_derive', 'syn', 'codegen', 'bench'])
    assert.ok(!reached.has(never), `${never} is not built for the target`);
});

test('a dependency the shipping graph itself asks to derive does ship', () => {
  const [packages, nodes] = graph();
  packages.get('app').dependencies[0].features = ['derive'];
  const reached = resolveFeatures(['app'], packages, nodes);
  assert.ok(reached.has('serde_derive'));
  assert.ok(reached.has('syn'));
});

test('an optional dependency ships when a feature names it strongly, never weakly', () => {
  const [packages, nodes] = graph();
  const reached = resolveFeatures(['app'], packages, nodes);
  assert.deepEqual([...reached.get('lexical')].sort(), ['parse'], 'default-features = false');
  assert.ok(reached.has('lexical-parse'), 'an implicit feature, named by `parse`');
  assert.ok(!reached.has('lexical-write'), '`lexical-write?/format` turns nothing on');

  packages.get('app').dependencies[3].features = ['parse', 'format', 'lexical-write'];
  const both = resolveFeatures(['app'], packages, nodes);
  assert.ok(both.has('lexical-write'), 'switched on by its own name');
  assert.ok(both.get('lexical-write').has('format'), 'and then the weak feature applies');
});

test('a cfg() that holds on no platform is no edge; one that names a platform is', () => {
  const [packages, nodes] = graph();
  const reached = resolveFeatures(['app'], packages, nodes);
  assert.ok(!reached.has('sys'));
  assert.ok(reached.has('unix'));
  assert.equal(noPlatform('cfg(any())'), true);
  assert.equal(noPlatform('cfg(not(all()))'), true);
  assert.equal(noPlatform('cfg(all())'), false);
  assert.equal(noPlatform('cfg(windows)'), false);
  assert.equal(noPlatform('x86_64-pc-windows-msvc'), false);
  assert.equal(noPlatform(null), false);
});

test('an edge the manifest does not declare is a broken metadata read, and says so', () => {
  const [packages, nodes] = graph();
  nodes.get('syn').deps.push(edge('unix'));
  packages.get('app').dependencies[0].features = ['derive'];
  assert.throws(() => resolveFeatures(['app'], packages, nodes), /syn -> unix/);
});

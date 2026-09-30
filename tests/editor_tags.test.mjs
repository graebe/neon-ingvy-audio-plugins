/*
 * The editor protocol's numbers, on both sides of the WebView.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * Every message tag and parameter index exists twice: an enum in C++ and a
 * table in JavaScript. Neither is generated from the other -- a generator hides
 * a disagreement by overwriting it; this names it. It found one: the
 * Spectrogram's view, compare and clash tags had drifted apart.
 *
 *   ni::editor::Tag (plugins/_shared/ni/Editor.h)   <->  SHELL_MSG (ui-kit)
 *   each plugin's EMsgTags                          <->  its msg.js MSG
 *   each plugin's EParams                           <->  its msg.js P, NUM_PARAMS
 *
 * A C++ name kMsgFooBar / kShell... / kFooBar is the JS key fooBar.
 *
 *   node --test tests/editor_tags.test.mjs
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { dirname, join } from 'node:path';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');

/** Strip comments, so a commented-out enumerator cannot satisfy a check. */
const code = (src) => src.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/.*$/gm, '');

/** `enum <name> [: type] { ... }` as { enumerator: value }, implicit values counted. */
function parseEnum(file, name) {
  const src = code(readFileSync(join(ROOT, file), 'utf8'));
  const m = new RegExp(`enum\\s+${name}\\s*(?::\\s*\\w+\\s*)?\\{([^}]*)\\}`).exec(src);
  assert.ok(m, `enum ${name} not found in ${file}`);
  const out = {};
  let next = 0;
  for (const raw of m[1].split(',')) {
    const item = raw.trim();
    if (!item) continue;
    const [, ident, expr] = /^(\w+)\s*(?:=\s*(.+))?$/.exec(item) ?? [];
    assert.ok(ident, `cannot read "${item}" in ${file}`);
    if (expr !== undefined) {
      const v = Number(expr.trim());
      assert.ok(Number.isInteger(v), `${ident} = ${expr} is not an integer literal in ${file}`);
      next = v;
    }
    out[ident] = next++;
  }
  return out;
}

const jsKey = (ident) => {
  const bare = ident.replace(/^k(Msg|Shell)?/, '');
  return bare.charAt(0).toLowerCase() + bare.slice(1);
};

const load = (file) => import(pathToFileURL(join(ROOT, file)).href);

const SHELL = parseEnum('plugins/_shared/ni/Editor.h', 'Tag');
const { SHELL_MSG } = await load('ui-kit/src/lib/shell.js');

test('the shell tags agree, name for name and number for number', () => {
  const cpp = Object.fromEntries(Object.entries(SHELL).map(([k, v]) => [jsKey(k), v]));
  assert.deepEqual(cpp, SHELL_MSG);
});

const PLUGINS = [
  { name: 'trance-gate', header: 'plugins/trance-gate/TranceGate.h', params: 'plugins/trance-gate/Params.h' },
  { name: 'side-chain', header: 'plugins/side-chain/SideChain.h', params: 'plugins/side-chain/Params.h' },
  { name: 'spectrogram', header: 'plugins/spectrogram/Spectrogram.h', params: 'plugins/spectrogram/Spectrogram.h' },
  { name: 'listen-in', header: 'plugins/listen-in/ListenIn.h', params: 'plugins/listen-in/State.h' },
];

for (const p of PLUGINS) {
  const msg = await load(`plugins/${p.name}/ui/src/lib/msg.js`);
  const tags = parseEnum(p.header, 'EMsgTags');
  const params = parseEnum(p.params, 'EParams');
  const nParams = params.kNumParams;

  test(`${p.name}: every product tag is in msg.js with the same number, and nothing else is`, () => {
    const cpp = Object.fromEntries(Object.entries(tags).map(([k, v]) => [jsKey(k), v]));
    const js = Object.fromEntries(Object.entries(msg.MSG).filter(([k]) => !(k in SHELL_MSG)));
    assert.deepEqual(js, cpp);
  });

  test(`${p.name}: msg.js carries the shell's tags unchanged`, () => {
    for (const [k, v] of Object.entries(SHELL_MSG)) assert.equal(msg.MSG[k], v, `MSG.${k}`);
  });

  test(`${p.name}: product tags sit between the display strings and the shell's`, () => {
    const shell = new Set(Object.values(SHELL));
    for (const [k, v] of Object.entries(tags)) {
      assert.ok(v >= 64 && v < 112, `${k} = ${v} is outside 64..111`);
      assert.ok(!shell.has(v), `${k} = ${v} is a shell tag`);
    }
    assert.equal(new Set(Object.values(tags)).size, Object.keys(tags).length, 'two tags share a number');
    assert.ok(nParams < 64, 'the display strings would run into the product tags');
  });

  test(`${p.name}: the parameter indices agree`, () => {
    if (!('P' in msg)) {
      assert.ok(!('NUM_PARAMS' in msg), 'NUM_PARAMS without P');
      return;
    }
    const cpp = Object.fromEntries(
      Object.entries(params).filter(([k]) => k !== 'kNumParams').map(([k, v]) => [jsKey(k), v]));
    assert.deepEqual(msg.P, cpp);
    assert.equal(msg.NUM_PARAMS, nParams);
  });
}

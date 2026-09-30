/*
 * The ground's wiring, in the shared shell every plugin is built on.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * A SOURCE CHECK, AND ONLY BECAUSE NOTHING ELSE CAN SEE THIS. ni::WebPlugin
 * derives from a format wrapper and cannot be linked into a test, and every
 * property below is the ORDER of a few calls, which no type checks. The
 * Spectrogram once sent its ground after six early returns and its background
 * almost never moved while every test stayed green.
 *
 * What is asserted:
 *
 *   - OnIdle's first statement is SendGround(), before anything can return
 *   - the detector is fed in ProcessBlock before the product's audio runs
 *   - OnReset re-rates and resets it; the constructor makes it, the
 *     destructor frees it
 *   - OnUIOpen switches it on; CloseWindow switches it off and chains to the
 *     base -- CloseWindow, because WebViewEditorDelegate never calls OnUIClose
 *   - those hooks are `final`, every plugin derives from ni::WebPlugin, and no
 *     plugin touches the detector itself
 *
 *   node --test tests/ground_shells.test.mjs
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');

/** Strip comments, so a commented-out call cannot satisfy a check. */
const code = (src) => src.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/.*$/gm, '');
const read = (f) => code(readFileSync(join(ROOT, f), 'utf8'));

/** The body of `Cls::name(...)`, braces balanced, or null. */
function body(src, cls, name) {
  const m = new RegExp(`\\b${cls}::${name}\\s*\\([^)]*\\)[^{;]*\\{`).exec(src);
  if (!m) return null;
  let depth = 1;
  let i = m.index + m[0].length;
  const start = i;
  for (; i < src.length && depth > 0; i++) {
    if (src[i] === '{') depth++;
    else if (src[i] === '}') depth--;
  }
  return src.slice(start, i - 1);
}

/** Statements in order, preprocessor lines dropped. */
const statements = (b) =>
  b.split('\n').filter((l) => !/^\s*#/.test(l)).join('\n')
    .split(/;|\{|\}/).map((s) => s.trim()).filter(Boolean);

const HDR = read('plugins/_shared/ni/WebPlugin.h');
const SRC = read('plugins/_shared/ni/WebPlugin.cpp');
const at = (name) => {
  const b = body(SRC, 'WebPlugin', name);
  assert.ok(b, `WebPlugin::${name} not found`);
  return b;
};

test('SendGround is the first thing OnIdle does', () => {
  assert.equal(statements(at('OnIdle'))[0], 'SendGround()');
});

test('the detector hears the input before the product writes an output', () => {
  const b = at('ProcessBlock');
  const push = b.indexOf('gnd_push(mGround');
  const audio = b.indexOf('ProcessAudio(');
  assert.ok(push >= 0, 'ProcessBlock does not feed the detector');
  assert.ok(audio > push, 'ProcessAudio runs before the detector is fed');
});

test('a reset re-rates the detector and clears its bed', () => {
  const b = at('OnReset');
  assert.match(b, /gnd_set_sample_rate\(\s*mGround/);
  assert.match(b, /gnd_reset\(\s*mGround\s*\)/);
});

test('the detector is made once and freed once', () => {
  assert.match(at('WebPlugin'), /mGround\s*=\s*gnd_new\(/);
  assert.match(at('~WebPlugin'), /gnd_free\(\s*mGround\s*\)/);
});

test('opening the editor switches the detector on', () => {
  assert.match(at('OnUIOpen'), /gnd_set_active\(\s*mGround\s*,\s*1\s*\)/);
});

test('closing the editor switches the detector off and tears the WebView down', () => {
  const b = at('CloseWindow');
  assert.match(b, /gnd_set_active\(\s*mGround\s*,\s*0\s*\)/);
  assert.match(b, /iplug::Plugin::CloseWindow\(\)/, 'CloseWindow must chain to the base');
});

test('no product can reorder any of it', () => {
  for (const hook of ['ProcessBlock', 'OnReset', 'OnIdle', 'OnUIOpen', 'CloseWindow']) {
    assert.match(HDR, new RegExp(`\\b${hook}\\([^)]*\\)\\s*final\\s*;`), `${hook} is not final`);
  }
});

const PLUGINS = {
  TranceGate: 'plugins/trance-gate/TranceGate',
  SideChain: 'plugins/side-chain/SideChain',
  Spectrogram: 'plugins/spectrogram/Spectrogram',
  ListenIn: 'plugins/listen-in/ListenIn',
};

for (const [cls, base] of Object.entries(PLUGINS)) {
  test(`${cls} is built on the shared shell and leaves the ground to it`, () => {
    const h = read(`${base}.h`);
    const c = read(`${base}.cpp`);
    assert.match(h, new RegExp(`class\\s+${cls}\\s+final\\s*:\\s*public\\s+ni::WebPlugin`));
    assert.doesNotMatch(h + c, /\bgnd_\w+\(/, `${cls} calls the detector itself`);
  });
}

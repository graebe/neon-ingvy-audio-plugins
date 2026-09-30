/*
 * The ground's wiring in the four plugin shells.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * A SOURCE CHECK, AND ONLY BECAUSE NOTHING ELSE CAN SEE THIS. The shells cannot
 * be linked into a test -- iPlug2 refuses any target that is not a plugin
 * format (tests/cpp/CMakeLists.txt says more) -- and every property below is a
 * property of the ORDER of a few calls in them, which no type checks.
 *
 * It exists because one of them was wrong for a release: the Spectrogram sent
 * its ground AT THE END of OnIdle, after six early returns -- one of them
 * `!mClashOn`, which is off by default -- so its background almost never moved,
 * and every test stayed green. The fix is structural (SendGround is the first
 * statement of every OnIdle) and this keeps it that way.
 *
 * What is asserted, per shell:
 *
 *   - OnIdle's first statement is SendGround(), before anything can return
 *   - OnUIOpen switches the detector on
 *   - CloseWindow switches it off, and chains to the base -- it is CloseWindow
 *     and not OnUIClose because WebViewEditorDelegate never calls OnUIClose
 *
 *   node --test tests/ground_shells.test.mjs
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');

const SHELLS = {
  TranceGate: 'plugins/trance-gate/TranceGate.cpp',
  SideChain: 'plugins/side-chain/SideChain.cpp',
  Spectrogram: 'plugins/spectrogram/Spectrogram.cpp',
  ListenIn: 'plugins/listen-in/ListenIn.cpp',
};

/** Strip comments, so a commented-out call cannot satisfy a check. */
const code = (src) => src.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/.*$/gm, '');

/** The body of `Cls::name()`, braces balanced, or null. */
function body(src, cls, name) {
  const m = new RegExp(`\\b${cls}::${name}\\s*\\([^)]*\\)\\s*\\{`).exec(src);
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

for (const [cls, file] of Object.entries(SHELLS)) {
  const src = code(readFileSync(join(ROOT, file), 'utf8'));

  test(`${cls}: SendGround is the first thing OnIdle does`, () => {
    const b = body(src, cls, 'OnIdle');
    assert.ok(b, `${cls}::OnIdle not found in ${file}`);
    assert.equal(statements(b)[0], 'SendGround()',
      `${cls}::OnIdle must call SendGround() before anything that can return early`);
  });

  test(`${cls}: opening the editor switches the detector on`, () => {
    const b = body(src, cls, 'OnUIOpen');
    assert.ok(b, `${cls}::OnUIOpen not found in ${file}`);
    assert.match(b, /gnd_set_active\(\s*mGround\s*,\s*1\s*\)/);
  });

  test(`${cls}: closing the editor switches the detector off`, () => {
    const b = body(src, cls, 'CloseWindow');
    assert.ok(b, `${cls}::CloseWindow not found in ${file} -- OnUIClose is never called by the WebView delegate`);
    assert.match(b, /gnd_set_active\(\s*mGround\s*,\s*0\s*\)/);
    assert.match(b, /iplug::Plugin::CloseWindow\(\)/, 'CloseWindow must chain to the base, or the WebView is never torn down');
  });
}

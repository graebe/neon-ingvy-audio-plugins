// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The ground's wiring, in every product's processor.
 *
 * A SOURCE CHECK, AND ONLY BECAUSE NOTHING ELSE CAN SEE THIS: every property
 * below is the ORDER of a few calls, which no type checks. The Spectrogram
 * once sent its ground after six early returns and its background almost
 * never moved while every test stayed green.
 *
 * What is asserted, for ni::GroundClock (plugins/_shared/juce) and each
 * product that owns one:
 *
 *   - the clock ticks from the host's transport (position, tempo, meter,
 *     playing) and is handed no audio: the ground keeps time, it does not
 *     listen
 *   - a product ticks it before its audio runs and before anything can
 *     return, and in a bypassed block too
 *   - it is re-rated on prepare, on only while a window is open, and a
 *     window opening forgets the rings counted while it was closed
 *   - no product calls the ground's C ABI itself
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

/* Each product names the statement its audio starts with: the engine taken
 * for the block, or the bus's pusher. */
const JUCE_PLUGINS = {
  /* Its block, heard or bypassed, is one run() around tg_shell_begin. */
  'NI Trance Gate': ['plugins/trance-gate/TranceGate.h', 'plugins/trance-gate/TranceGate.cpp', /^run\s*\(/],
  'NI Listen-In': ['plugins/listen-in/ListenIn.h', 'plugins/listen-in/ListenIn.cpp', /shell_handoff_acquire/],
  'NI Spectrogram': ['plugins/spectrogram/SpectrogramProcessor.h', 'plugins/spectrogram/SpectrogramProcessor.cpp',
    /shell_handoff_acquire/],
  /* Its block, heard or bypassed, is one run() around sc_shell_begin. */
  'NI Side-Chain': ['plugins/side-chain/SideChain.h', 'plugins/side-chain/SideChain.cpp', /^run\s*\(/],
};

test('the ground is fed no audio, by the shell or by any product', () => {
  const all = read('plugins/_shared/juce/GroundClock.h')
    + Object.values(JUCE_PLUGINS).map(([h, cpp]) => read(h) + read(cpp)).join('');
  assert.doesNotMatch(all, /\bgnd_push\b/, 'gnd_push is gone: the ground keeps time');
});

test('ni::GroundClock ticks from the host clock it is given, and forgets old rings when a window opens', () => {
  const g = read('plugins/_shared/juce/GroundClock.h');
  assert.match(g, /gnd_tick\s*\(\s*clock\s*,\s*c\.ppq\s*,\s*c\.bpm\s*,\s*c\.numerator\s*,\s*c\.denominator\s*,/);
  assert.match(g, /seen\s*=\s*gnd_fires\s*\(\s*clock\s*\)\s*;\s*gnd_set_active\s*\(\s*clock\s*,\s*on/,
    'a window opening skips the rings counted while it was closed');
  assert.match(g, /gnd_set_sample_rate\s*\(\s*clock[\s\S]*gnd_reset\s*\(\s*clock\s*\)/);
});

for (const [name, [h, cpp, audioStarts]] of Object.entries(JUCE_PLUGINS)) {
  test(`${name} ticks its ground before its audio, bypassed too, and only with a window open`, () => {
    const src = read(cpp);
    assert.match(read(h), /ni::GroundClock\s+\w+\s*;/);
    assert.doesNotMatch(read(h) + src, /\bgnd_\w+\(/, `${name} calls the ground itself`);
    const process = statements(body(src, 'Processor', 'process'));
    const tick = process.findIndex((st) => /\.tick\s*\(\s*clock\s*,\s*frames\s*\)/.test(st));
    const audio = process.findIndex((st) => audioStarts.test(st));
    assert.ok(tick >= 0 && audio > tick, 'the ground is ticked before the engine runs, and before an early return');
    assert.ok(process.slice(0, tick).every((st) => !/return/.test(st)), 'nothing returns before the tick');
    assert.match(body(src, 'Processor', 'processBypassed'), /\.tick\s*\(\s*(readClock\s*\(|clock\s*,)/);
    assert.match(body(src, 'Processor', 'prepareToPlay'), /\.prepare\s*\(/);
    assert.match(body(src, 'Processor', 'editorOpened'), /\.setActive\s*\(\s*true\s*\)/);
    assert.match(body(src, 'Processor', 'editorClosed'), /\.setActive\s*\(\s*false\s*\)/);
  });
}

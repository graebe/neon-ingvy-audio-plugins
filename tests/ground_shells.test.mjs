// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The ground's wiring, in the shared shell every plugin is built on.
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
 *   - ProcessBlock ticks the clock exactly once, from the host's transport
 *     (position, tempo, meter, playing), before the product's audio runs --
 *     and hands it no audio: the ground keeps time, it does not listen
 *   - SendGround sends a frame tick every idle tick while, and only while,
 *     the editor says its ground is moving, and a closed editor gets none
 *   - OnReset re-rates and resets it; the constructor makes it, the
 *     destructor frees it
 *   - OnUIOpen switches it on; CloseWindow switches it off and chains to the
 *     base -- CloseWindow, because WebViewEditorDelegate never calls OnUIClose
 *   - those hooks are `final`, every plugin on iPlug2 derives from
 *     ni::WebPlugin, and no plugin touches the ground itself
 *   - on the JUCE shell, ni::GroundClock keeps the same order for a product
 *     that owns one: ticked before the audio, bypassed too, on only while a
 *     window is open
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

test('ProcessBlock ticks the clock once, from the host transport, before the audio', () => {
  const b = at('ProcessBlock');
  const ticks = b.match(/\bgnd_tick\(/g) ?? [];
  assert.equal(ticks.length, 1, 'ProcessBlock must tick the ground exactly once a block');
  const tick = b.indexOf('gnd_tick(mGround');
  const audio = b.indexOf('ProcessAudio(');
  assert.ok(tick >= 0, 'ProcessBlock does not tick the ground');
  assert.ok(audio > tick, 'ProcessAudio runs before the ground is ticked');

  /* The arguments, in the header's order: position, tempo, meter, playing,
   * frames. Swapping two doubles compiles and rings on the wrong beats. */
  const call = b.slice(tick, b.indexOf(';', tick));
  assert.match(call,
    /gnd_tick\(\s*mGround\s*,\s*GetPPQPos\(\)\s*,\s*GetTempo\(\)\s*,\s*num\s*,\s*den\s*,\s*GetTransportIsRunning\(\)[^,]*,\s*nFrames\s*\)/);
  assert.match(b, /GetTimeSig\(\s*num\s*,\s*den\s*\)/, 'the meter comes from the host');
});

test('the ground is fed no audio, in the shell or anywhere', () => {
  assert.doesNotMatch(SRC + HDR, /\bgnd_push\b/, 'gnd_push is gone: the ground keeps time');
  assert.doesNotMatch(at('ProcessBlock').slice(0, at('ProcessBlock').indexOf('ProcessAudio(')),
    /\binputs\b/, 'nothing before ProcessAudio reads the input');
});

test('while the editor reports its ground moving, every idle tick sends it a frame tick', () => {
  const b = at('SendGround');
  assert.match(b, /if\s*\(\s*mGroundRunning\s*\)\s*SendText\(\s*editor::kGroundTick/,
    'the frame tick is gated on the editor\'s report and nothing else');
  assert.match(HDR, /PortGroundRunning\(bool running\) override\s*\{\s*mGroundRunning = running;/);
  assert.match(at('CloseWindow'), /mGroundRunning\s*=\s*false/, 'a closed editor gets no ticks');
  assert.match(at('OnUIOpen'), /mGroundRunning\s*=\s*false/, 'a new page starts at rest');
});

test('a reset re-rates the clock and makes the next block a fresh start', () => {
  const b = at('OnReset');
  assert.match(b, /gnd_set_sample_rate\(\s*mGround/);
  assert.match(b, /gnd_reset\(\s*mGround\s*\)/);
});

test('the ground is made once and freed once', () => {
  assert.match(at('WebPlugin'), /mGround\s*=\s*gnd_new\(/);
  assert.match(at('~WebPlugin'), /gnd_free\(\s*mGround\s*\)/);
});

test('opening the editor switches the ground on', () => {
  assert.match(at('OnUIOpen'), /gnd_set_active\(\s*mGround\s*,\s*1\s*\)/);
});

test('closing the editor switches the ground off and tears the WebView down', () => {
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
  ListenIn: 'plugins/listen-in/ListenIn',
};

for (const [cls, base] of Object.entries(PLUGINS)) {
  test(`${cls} is built on the shared shell and leaves the ground to it`, () => {
    const h = read(`${base}.h`);
    const c = read(`${base}.cpp`);
    assert.match(h, new RegExp(`class\\s+${cls}\\s+final\\s*:\\s*public\\s+ni::WebPlugin`));
    assert.doesNotMatch(h + c, /\bgnd_\w+\(/, `${cls} calls the ground itself`);
  });
}

/*
 * THE JUCE SHELL'S GROUND: ni::GroundClock (plugins/_shared/juce), which a
 * product on that shell owns. The same order holds there -- the clock ticked
 * from the host's transport before the product's audio, the bypassed block
 * included, on only while a window shows it -- and the same rule: no product
 * calls the ground's C ABI itself. Each product names the statement its
 * audio starts with: the engine taken for the block, or the bus's pusher.
 */
const JUCE_PLUGINS = {
  'NI Trance Gate': ['plugins/trance-gate/TranceGate.h', 'plugins/trance-gate/TranceGate.cpp', /tg_shell_begin/],
  'NI Spectrogram': ['plugins/spectrogram/SpectrogramProcessor.h', 'plugins/spectrogram/SpectrogramProcessor.cpp',
    /shell_handoff_acquire/],
  /* Its block, heard or bypassed, is one run() around sc_shell_begin. */
  'NI Side-Chain': ['plugins/side-chain/SideChain.h', 'plugins/side-chain/SideChain.cpp', /^run\s*\(/],
};

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

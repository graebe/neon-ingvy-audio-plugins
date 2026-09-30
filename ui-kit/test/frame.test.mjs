/*
 * The frame's pieces: the fit, the height it reports, the clock and the
 * handshake. Copyright (c) 2026 Torben Gräber. MIT.
 *
 * Run with --conditions=browser (npm test does), so Solid's effects run.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createRoot } from 'solid-js';
import { createFit, fitScale, reportHeight, scaledHeight } from '../src/lib/fit.js';
import { createClock, positionAt } from '../src/lib/clock.js';
import { useEditorBridge, parseGround } from '../src/lib/bridge.js';
import { SHELL_MSG } from '../src/lib/shell.js';

const flush = () => new Promise((r) => setTimeout(r, 0));

/* A window that can be resized and says who is listening. */
function fakeWindow(width) {
  const listeners = new Set();
  return {
    innerWidth: width,
    addEventListener: (type, fn) => type === 'resize' && listeners.add(fn),
    removeEventListener: (type, fn) => type === 'resize' && listeners.delete(fn),
    resize(w) { this.innerWidth = w; for (const fn of [...listeners]) fn(); },
    listeners,
  };
}

test('the scale fits the design width into the viewport', () => {
  assert.equal(fitScale(412, 824), 0.5);
  assert.equal(fitScale(0, 824), 1);          /* no viewport yet */
  assert.equal(fitScale(10, 824), 0.1);       /* floored */
  assert.equal(scaledHeight(604, 0.5), 302);
  assert.equal(scaledHeight(101, 1.5), 152);
});

test('createFit follows a resize and removes its listener with its owner', () => {
  const win = fakeWindow(824);
  let fit;
  const dispose = createRoot((d) => { fit = createFit(824, { win }); return d; });
  assert.equal(fit.scale(), 1);
  win.resize(412);
  assert.equal(fit.scale(), 0.5);
  assert.equal(win.listeners.size, 1);
  dispose();
  assert.equal(win.listeners.size, 0, 'the resize listener outlived the editor');
});

/*
 * THE BUG: the height was computed inside an effect that read a plain fit()
 * call, so a host resize moved the transform and never re-sent the height.
 */
test('the height is re-sent when the host resizes the window', async () => {
  const win = fakeWindow(760);
  const sent = [];
  const dispose = createRoot((d) => {
    const { scale } = createFit(760, { win });
    reportHeight(() => 604, scale, (h) => sent.push(h));
    return d;
  });
  await flush();
  win.resize(380);
  await flush();
  win.resize(380);                             /* no change, no message */
  await flush();
  dispose();
  assert.deepEqual(sent, ['604', '302']);
});

test('the clock interpolates between anchors and holds when stopped', () => {
  assert.equal(positionAt({ pos: 2, perMs: 0.01, moving: true, at: 100 }, 150), 2.5);
  assert.equal(positionAt({ pos: 2, perMs: 0.01, moving: false, at: 100 }, 150), 2);
  assert.equal(positionAt({ pos: 2, perMs: 0, moving: true, at: 100 }, 150), 2);
});

test('the clock runs a frame loop only while the position moves', () => {
  let t = 0;
  const frames = [];
  const raf = (f) => { frames.push(f); return frames.length; };
  const caf = () => {};
  createRoot((dispose) => {
    const c = createClock({ raf, caf, now: () => t });
    assert.equal(c.running(), false, 'a stopped clock must not schedule frames');
    c.set(4, 1 / 125, true);
    assert.equal(c.running(), true);
    t = 250;
    assert.equal(c.position(), 6);
    frames.shift()();                          /* one frame ticks and re-arms */
    assert.equal(c.running(), true);
    c.set(6, 1 / 125, false);                  /* transport stops */
    assert.equal(c.running(), false);
    const before = frames.length;
    frames.shift()?.();                        /* a stale frame does not re-arm */
    assert.equal(frames.length, before - 1);
    assert.equal(c.position(), 6);
    dispose();
  });
});

test('the handshake sends ready once, after mount, and hands kicks to the ground', async () => {
  const sent = [];
  globalThis.IPlugSendMsg = (m) => sent.push(m);
  const kicks = [];
  const other = [];
  const b64 = (s) => btoa(s);
  const dispose = createRoot((d) => {
    const bridge = useEditorBridge({ onMessage: (tag, text) => other.push([tag, text]) });
    bridge.setGround({ trigger: (s) => kicks.push(s) });
    return d;
  });
  await flush();
  globalThis.SAMFD(SHELL_MSG.ground, 0, b64('0.750'));
  globalThis.SAMFD(SHELL_MSG.ground, 0, b64('nope'));
  globalThis.SAMFD(64, 0, b64('x'));
  dispose();
  globalThis.SAMFD(64, 0, b64('after'));
  assert.deepEqual(sent.map((m) => m.msgTag), [SHELL_MSG.ready]);
  assert.deepEqual(kicks, [0.75]);
  assert.deepEqual(other, [[64, 'x']]);
  assert.equal(parseGround(''), null);
});

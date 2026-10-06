/*
 * A PLAYING TRANSPORT, for reviewing the animated ground without a host.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * In a plugin the ground rings on the host's beat: the beat clock in Rust
 * (engines/ground) watches the transport on the audio thread and the plugin
 * sends one message per ring. A review harness has no host, so this sends
 * those same messages on a fake transport's beat -- the same thing the editor
 * sees in Live:
 *
 *   one ring a quarter note, "1.000" on each bar's downbeat and "0.400" on
 *   every other beat, and nothing while stopped.
 *
 * It is the one copy, loaded by every editor's harness page after its editor:
 *
 *   <script type="module" src="../../../../../ui-kit/harness/beat.js"></script>
 *
 * THE QUERY, shared with the Spectrogram's mock so its bar view and its ground
 * agree about the tempo:
 *
 *   ?bpm=N       the tempo (default 120)
 *   ?sig=7/8     the time signature (default 4/4)
 *   ?stopped     start with the transport stopped
 *
 * and the keyboard: T starts and stops it. A stopped transport restarts from
 * bar 1, as a host's play button does from the start marker.
 *
 * AND THE GROUND'S FRAME CLOCK, as the plugin's idle tick sends it: while the
 * editor reports its ground moving (SHELL_MSG.groundRun "1"), an empty
 * groundTick every 20 ms, until it reports "0". Those two messages are the
 * plugin's business, so they are answered here and never reach the mock's
 * record of what the editor sent.
 *
 * For a test, `window.__transport` is the same thing as an object: `play()`,
 * `stop()`, and `playing`, `bpm`, `rings` (each sent ring's strength, oldest
 * first) and `groundRunning` to read.
 *
 * WHY THE RULE IS RESTATED HERE, when it lives in ground-core: this file's
 * whole job is to stand in for the plugin, and the plugin's copy is on the
 * other side of a C ABI. What it must agree with is small -- a ring per
 * quarter, the downbeat every num*4/den quarters -- and the editor cannot tell
 * the two apart, which is the point.
 */
import { SHELL_MSG } from '../src/lib/shell.js';

const Q = new URLSearchParams(location.search);
const BPM = Number(Q.get('bpm')) > 0 ? Number(Q.get('bpm')) : 120;
const [NUM, DEN] = /^(\d+)\/(\d+)$/.exec(Q.get('sig') ?? '')?.slice(1).map(Number) ?? [4, 4];
const BAR = NUM > 0 && DEN > 0 ? (NUM * 4) / DEN : 4;
/* How often the "plugin" looks at its count: an idle tick. */
const TICK_MS = 20;
/* More than this many quarters since the last tick is a jump, not a lag. */
const MAX_LAG = 0.5;
const EPS = 1e-9;

const b64 = (s) => btoa(String.fromCharCode(...new TextEncoder().encode(s)));
const ring = (strength) => {
  const text = strength.toFixed(3);
  transport.rings.push(Number(text));
  globalThis.SAMFD?.(SHELL_MSG.ground, text.length, b64(text));
};

let t0 = 0;
/* The position, in quarters, up to which every ring has been sent. */
let done = 0;
let timer = 0;

/* Every ring in [from, to), in quarters: each quarter, and each bar start; a
 * bar start on a quarter is one ring, the strong one. */
function ringBetween(from, to) {
  let q = Math.ceil(from);
  let b = Math.ceil(from / BAR) * BAR;
  if (b < from) b += BAR;
  while (Math.min(q, b) < to) {
    if (Math.abs(b - q) < EPS) { q += 1; b += BAR; ring(1); }
    else if (b < q) { b += BAR; ring(1); }
    else { q += 1; ring(0.4); }
  }
}

function tick() {
  const now = ((performance.now() - t0) / 1000) * (BPM / 60);
  /* A tab that slept wakes as a host does after a seek: the beats it missed
   * are not rung in a burst. */
  ringBetween(now - done > MAX_LAG ? now : done, now);
  done = now;
}

const transport = {
  bpm: BPM,
  playing: false,
  rings: [],
  play() {
    if (transport.playing) return;
    transport.playing = true;
    t0 = performance.now();
    done = 0;
    timer = setInterval(tick, TICK_MS);
  },
  stop() {
    transport.playing = false;
    clearInterval(timer);
    timer = 0;
  },
};
window.__transport = transport;

/* The plugin's half of the ground's frame clock. */
transport.groundRunning = false;
const toMock = window.IPlugSendMsg;
window.IPlugSendMsg = (m) => {
  if (m?.msg === 'SAMFUI' && m.msgTag === SHELL_MSG.groundRun) {
    transport.groundRunning = atob(m.data ?? '') === '1';
    return;
  }
  toMock?.(m);
};
setInterval(() => {
  if (transport.groundRunning) globalThis.SAMFD?.(SHELL_MSG.groundTick, 0, '');
}, TICK_MS);

addEventListener('keydown', (e) => {
  if (e.repeat || e.key.toLowerCase() !== 't') return;
  const el = e.target;
  if (el?.isContentEditable || /^(input|textarea|select)$/i.test(el?.tagName ?? '')) return;
  if (transport.playing) transport.stop(); else transport.play();
});

if (!Q.has('stopped')) transport.play();
console.info(`harness: transport ${transport.playing ? 'playing' : 'stopped'} at ${BPM} BPM in ` +
  `${NUM}/${DEN} -- T starts and stops it (?bpm=, ?sig=, ?stopped)`);

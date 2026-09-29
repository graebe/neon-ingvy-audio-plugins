/*
 * The plugin, faked, for reviewing the editor without a host.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * IT PUSHES BEFORE THE EDITOR LOADS, ON PURPOSE.
 *
 * This is a classic <script> and the editor is a <script type="module">, so this
 * file runs FIRST and the editor's own code has not evaluated yet -- exactly the
 * order the real plugin produces, where OnUIOpen fires from didFinishNavigation
 * before the deferred module does. Every SPVFD() below therefore lands on an
 * undefined global and is dropped, which is the bug kMsgReady exists to fix.
 *
 * So the harness is only honest if it keeps doing that. The Trance Gate's mock
 * once waited 120 ms before pushing, which papered over the very fault it should
 * have caught: the editor looked correct in the harness and came up with twelve
 * zeroes in Live. IF YOU MAKE THIS DEFERRED OR DELAYED, YOU HAVE DISABLED THE
 * TEST.
 *
 * The reply to kMsgReady is where the values actually arrive.
 */
const b64 = (s) => btoa(String.fromCharCode(...new TextEncoder().encode(s)));

const MSG = {
  uiState: 64, params: 65, scope: 66, stageMs: 67, buses: 68,
  setText: 96, height: 97, ready: 98,
};

const Q = new URLSearchParams(location.search);
const num = (k, d) => (Q.has(k) ? Number(Q.get(k)) : d);

/* The engine's own values, in pump_param_t order. ?curve= and ?shape=d,a,h,r
 * let a review look at one curve at a time. */
const SHAPE = (Q.get('shape') ?? '4,8,14,45').split(',').map(Number);
const SOURCE = num('source', 0);
const CURVE = num('curve', 1);
const DEPTH = num('depth', 0.85);
const ENGINE = [
  SOURCE,        /* source     */
  num('rate', 4),/* rate       */
  0,             /* time_mode  */
  SHAPE[0], SHAPE[1], SHAPE[2], SHAPE[3],
  DEPTH,         /* depth 0..1 */
  CURVE,
  1, 36, 0, 0, -24, 20,
];

/* Normalised, as a host reports them -- the ranges are Pump.cpp's. */
const NORM = [
  SOURCE / 2, num('rate', 4) / 11, 0,
  SHAPE[0] / 100, SHAPE[1] / 200, SHAPE[2] / 200, SHAPE[3] / 200,
  DEPTH, CURVE / 3,
  1 / 16, 36 / 127, 0, 0, (-24 + 60) / 60, 20 / 200,
];
const DISPLAY = [
  ['Cycle', 'MIDI', 'Sidechain'][SOURCE],
  '1/4', 'ms',
  `${SHAPE[0].toFixed(1)} %`, `${SHAPE[1].toFixed(1)} %`,
  `${SHAPE[2].toFixed(1)} %`, `${SHAPE[3].toFixed(1)} %`,
  `${(DEPTH * 100).toFixed(1)} %`,
  ['Linear', 'Exponential', 'S-Curve', 'Pump'][CURVE],
  '1', 'C1', 'Trigger', '0.0 %', '-24.0 dB', '20 ms',
];

const MS_CYCLE = 500;

/*
 * A SYNTHETIC CAPTURE, and it is built by running the REAL shape maths so the
 * harness cannot show an agreement the plugin does not have.
 *
 * 256 columns of a phase-locked cycle: a steady input, ducked by the shape, with
 * a kick-shaped transient at the top of the cycle so the dry band has something
 * in it that the wet band visibly loses.
 */
const COLS = 256;
function buildScope() {
  const hex = [];
  const seen = [];
  const K = 3.0, DENOM = 0.95021293163213605;
  const cexp = (t) => (1 - Math.exp(-K * t)) / DENOM;
  const shape = (t, dir) => {
    if (!(t > 0)) return 0;
    if (t >= 1) return 1;
    if (CURVE === 1) return cexp(t);
    if (CURVE === 2) {
      return t < 0.5 ? 0.5 * (1 - cexp(1 - 2 * t)) : 0.5 + 0.5 * cexp(2 * t - 1);
    }
    if (CURVE === 3) {
      if (dir === 0) return t;
      const inv = 1 - t;
      return 1 - inv * inv * inv;
    }
    return t;
  };
  const [d, a, h, r] = SHAPE;
  const duckAt = (pct) => {
    if (pct < d) return 0;
    if (pct < d + a) return shape((pct - d) / a, 0);
    if (pct < d + a + h) return 1;
    if (pct < d + a + h + r) return 1 - shape((pct - d - a - h) / r, 1);
    return 0;
  };
  const byte = (v) => {
    const c = Math.max(-1, Math.min(1, v));
    return Math.round((c + 1) * 127.5).toString(16).padStart(2, '0').toUpperCase();
  };
  const ubyte = (v) => Math.round(Math.max(0, Math.min(1, v)) * 255)
    .toString(16).padStart(2, '0').toUpperCase();

  /* ?filling= leaves the tail unseen, so a review can check that a picture
   * still filling reads as unfinished rather than as a signal that stopped. */
  const filled = num('filling', COLS);

  for (let i = 0; i < COLS; i++) {
    seen.push(i < filled ? '1' : '0');
    const pct = (i / COLS) * 100;
    /* A kick at the top of the cycle plus a steady pad under it. */
    const env = Math.exp(-pct / 6);
    const amp = 0.28 + 0.62 * env;
    const gain = 1 - DEPTH * duckAt(pct);
    hex.push(byte(-amp), byte(amp), byte(-amp * gain), byte(amp * gain), ubyte(gain));
  }
  return `${COLS}:${seen.join('')}:${hex.join('')}`;
}

/* source:rate:ms_cycle:sweep:advancing:fires:duck:key:connected:stage:phase */
let FIRES = 12;
const SWEEP = num('sweep', 0.34);
const uiState = () => [
  SOURCE, num('rate', 4), MS_CYCLE.toFixed(3), SWEEP.toFixed(6),
  SOURCE === 0 ? 1 : 0, FIRES, '0.0000', '0.00000',
  SOURCE === 2 ? num('key', 1) : 0,
  num('stage', 4), SWEEP.toFixed(6),
].join(':');

const stageMsText = () => SHAPE.map((p) => ((p / 100) * MS_CYCLE).toFixed(3)).join(':');
const busesText = () => `${SOURCE === 2 ? num('key', 1) : 0}:${num('keyismain', 0)}`;

function pushAll() {
  for (let i = 0; i < NORM.length; i++) {
    globalThis.SPVFD?.(i, NORM[i]);
    globalThis.SAMFD?.(i, DISPLAY[i].length, b64(DISPLAY[i]));
  }
  const send = (tag, text) => globalThis.SAMFD?.(tag, text.length, b64(text));
  send(MSG.params, ENGINE.join(':'));
  send(MSG.uiState, uiState());
  send(MSG.stageMs, stageMsText());
  send(MSG.buses, busesText());
  send(MSG.scope, buildScope());
}

/* The premature push -- dropped, and that is the point. */
pushAll();

globalThis.IPlugSendMsg = (m) => {
  if (m.msg === 'SAMFUI' && (m.msgTag | 0) === MSG.ready) {
    /* The reply that actually arrives. */
    pushAll();
    return;
  }
  if (m.msg === 'SAMFUI' && (m.msgTag | 0) === MSG.height) return;
  /* Echo a parameter edit back the way a host that echoes would, so dragging a
   * handle in the harness moves the drawing. */
  if (m.msg === 'SPVFUI') {
    NORM[m.paramIdx] = m.value;
    /* And keep the engine values in step, since the drawing reads those. */
    const R = [null, null, null, [0, 100], [0, 200], [0, 200], [0, 200],
      [0, 1], null, null, null, null, [0, 1], [-60, 0], [0, 200]][m.paramIdx];
    if (R) ENGINE[m.paramIdx] = R[0] + (R[1] - R[0]) * m.value;
    globalThis.SAMFD?.(MSG.params, 0, b64(ENGINE.join(':')));
  }
};

/*
 * The plugin, faked, for reviewing the editor without a host.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * IT PUSHES BEFORE THE EDITOR LOADS, ON PURPOSE.
 *
 * This is a classic <script> and the editor is a <script type="module">, so
 * this file runs FIRST and the editor's own code has not evaluated yet --
 * exactly the order the real plugin produces, where OnUIOpen fires from
 * didFinishNavigation before the deferred module does. Every SPVFD() below
 * therefore lands on an undefined global and is dropped, which is the bug that
 * kMsgReady exists to fix.
 *
 * So the harness is only honest if it keeps doing that. An earlier version of
 * this mock waited 120 ms before pushing, which quietly papered over the very
 * fault it should have caught: the editor looked correct here and came up with
 * twelve zeroes in Live. If you make this deferred or delayed, you have
 * disabled the test.
 *
 * The reply to kMsgReady is where the values actually arrive.
 */
const b64 = (s) => btoa(String.fromCharCode(...new TextEncoder().encode(s)));

/* 12 normalised values in EParams order, and their display strings. */
const VALUES = [0 / 7, (16 - 1) / 127, 7 / 12, 0, 0, 0, 0.9, 0.75,
                1.6 / 200, 16 / 200, 1.0, 16 / 200];
const DISPLAY = ['1', '16', '1/16', 'Off', 'ms', 'Linear', '90.00 %', '75.00 %',
                 '1.60 %', '16.00 %', '100.00 %', '16.00 %'];

/* steps:ties:length:phase:ms_step:advancing:cursor:depths
 * Every other step on, two of them tied, step 4 at half amount. */
const DEPTHS = Array.from({ length: 16 },
                          (_, i) => (i % 2 ? 'FF' : (i === 4 ? '80' : 'FF'))).join('');
const UI_STATE = `5555:0044:16:5.400:125.00:1:5:${DEPTHS}`;
/*
 * slot:legato:time_mode:curve:rate:length:amount:hold:attack:decay:sustain:
 * release:width_ms
 *
 * ?curve=0|1|2 and ?adsr=a,d,s,r let a review look at one curve at a time with
 * stages long enough to SEE -- the default attack of 1.6% is nearly a step and
 * tells you nothing about the shape it took to get there.
 */
const Q = new URLSearchParams(location.search);
const CURVE = Q.get('curve') ?? '0';
const [A, D, S, R] = (Q.get('adsr') ?? '1.6,16,1,16').split(',');
/* ?width= is the gate's open time as a fraction of the step (`hold`), and
 * width_ms is derived from it so the plot's axis is not a lie. */
const HOLD = Number(Q.get('width') ?? 0.75);
const PARAMS = `0:0:0:${CURVE}:1/16:15:0.9:${HOLD}:${A}:${D}:${S}:${R}:${(HOLD * 125).toFixed(2)}`;

/*
 * The scope as the plugin sends it: "<cols>:<windowMs>:<4 hex bytes a column>",
 * already rotated so column 0 is the oldest sample in the window.
 *
 * `roll` shifts the phase so successive pushes are genuinely different data --
 * which is what makes "the window is live" testable rather than a claim.
 */
const COLS = 256, WINDOW_MS = 1000;
const scope = (roll = 0) => {
  const H = (x) => Math.max(0, Math.min(255,
    Math.round((Math.max(-1, Math.min(1, x)) + 1) * 127.5)))
    .toString(16).toUpperCase().padStart(2, '0');
  let hex = '';
  for (let i = 0; i < COLS; i++) {
    const j = i + roll;
    const a = 0.85 * Math.sin(j * 0.31) * (0.6 + 0.4 * Math.sin(j * 0.05));
    const open = Math.floor(j / 16) % 2 === 0 ? 1 : 0.12;   /* gated */
    hex += H(-Math.abs(a)) + H(Math.abs(a)) + H(-Math.abs(a * open)) + H(Math.abs(a * open));
  }
  return `${COLS}:${WINDOW_MS}:${hex}`;
};

let roll = 0;
const pushAll = () => {
  VALUES.forEach((x, i) => globalThis.SPVFD?.(i, x));
  DISPLAY.forEach((d, i) => globalThis.SAMFD?.(i, d.length, b64(d)));
  globalThis.SAMFD?.(64, 0, b64(UI_STATE));
  globalThis.SAMFD?.(65, 0, b64(PARAMS));
  globalThis.SAMFD?.(66, 0, b64(scope(roll)));
};

/* The plugin pushes the window every idle tick; so does this, or the scope
 * would look live only because nothing had asked it to change. */
setInterval(() => { roll += 7; globalThis.SAMFD?.(66, 0, b64(scope(roll))); }, 50);

/* THE RACE, REPRODUCED: this runs now, before the editor exists, and every one
 * of these calls goes nowhere. It is here to be dropped. */
window.__mockEarlyPush = 0;
pushAll();

const MSG_READY = 102, MSG_REQUEST_PATCH = 99;

/* Everything the editor sends, for the interaction tests to assert on. */
window.__sent = [];

window.IPlugSendMsg = (m) => {
  window.__sent.push(m);
  /* kMsgReady -- the editor has mounted and is listening. This is the reply
   * that actually delivers, and the whole point of the handshake. */
  if (m?.msg === 'SAMFUI' && m.msgTag === MSG_READY) {
    window.__mockEarlyPush++;
    pushAll();
    return;
  }
  if (m?.msg === 'SAMFUI' && m.msgTag === MSG_REQUEST_PATCH)
    globalThis.SAMFD?.(67, 0, b64('tg1:slot=0:len=16:steps=5555'));
  /*
   * NO ECHO, DELIBERATELY, AND THIS IS LOAD-BEARING.
   *
   * iPlug2's SPVFUI handler does SetNormalized() plus OnParamChangeUI() and
   * sends NOTHING back to the page. An earlier version of this mock echoed
   * writes -- which made every control look fine here while Join Neighbors
   * did nothing in Live and the Curve label stuck on its first entry, because
   * both were waiting on a value the plugin never returns.
   *
   * So this mock behaves like the plugin: it stays silent. Anything that
   * depends on hearing its own write back is broken, and will look broken
   * here, which is the whole point of a harness.
   */
};

/* Click through to the SIGNAL tab so the scope can be reviewed headlessly. */
if (location.search.includes('signal'))
  setTimeout(() => document.querySelectorAll('.tab')[1]?.click(), 260);

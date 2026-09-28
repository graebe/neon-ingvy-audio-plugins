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
const PARAMS = `0:0:0:${CURVE}:1/16:15:0.9:0.75:${A}:${D}:${S}:${R}:93.75`;

/* The scope as the plugin sends it: a byte per bound, hex, four per column. */
const scope = () => {
  const H = (x) => Math.max(0, Math.min(255,
    Math.round((Math.max(-1, Math.min(1, x)) + 1) * 127.5)))
    .toString(16).toUpperCase().padStart(2, '0');
  const FILLED = Number(new URLSearchParams(location.search).get('filled') ?? 256);
  let hex = '';
  for (let i = 0; i < FILLED; i++) {
    const a = 0.85 * Math.sin(i * 0.31) * (0.6 + 0.4 * Math.sin(i * 0.05));
    const open = Math.floor(i / 16) % 2 === 0 ? 1 : 0.12;   /* gated */
    hex += H(-Math.abs(a)) + H(Math.abs(a)) + H(-Math.abs(a * open)) + H(Math.abs(a * open));
  }
  return `${FILLED}:${hex}`;
};

const pushAll = () => {
  VALUES.forEach((x, i) => globalThis.SPVFD?.(i, x));
  DISPLAY.forEach((d, i) => globalThis.SAMFD?.(i, d.length, b64(d)));
  globalThis.SAMFD?.(64, 0, b64(UI_STATE));
  globalThis.SAMFD?.(65, 0, b64(PARAMS));
  globalThis.SAMFD?.(66, 0, b64(scope()));
};

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
  /* A real plugin echoes a parameter write back; without that the knobs do not
   * track their own drags here. */
  if (m?.msg === 'SPVFUI') globalThis.SPVFD?.(m.paramIdx, m.value);
};

/* Click through to the SIGNAL tab so the scope can be reviewed headlessly. */
if (location.search.includes('signal'))
  setTimeout(() => document.querySelectorAll('.tab')[1]?.click(), 260);

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
const Q0 = new URLSearchParams(location.search);
/* ?fade=0..1 and ?soft to review the fade-in at a setting. Default 1 -- the
 * neutral value, which is what the plugin ships with. */
const FADE = Number(Q0.get('fade') ?? 1);
const SOFT = Q0.has('soft') ? 1 : 0;
/* ?dir=out fades the HOLES in instead of the hits. */
const DIR = (Q0.get('dir') ?? 'in').toLowerCase() === 'out' ? 1 : 0;

const VALUES = [0 / 7, (16 - 1) / 127, 7 / 12, 0, 0, 0, 0.9, 0.75,
                1.6 / 200, 16 / 200, 1.0, 16 / 200,
                FADE, SOFT, DIR];
const DISPLAY = ['1', '16', '1/16', 'Off', 'ms', 'Linear', '90.00 %', '75.00 %',
                 /* The stages in ms, as the plugin formats them with Env Time
                  * at ms -- the editor no longer converts. */
                 '1.5 ms', '15.0 ms', '100.00 %', '15.0 ms',
                 `${(FADE * 100).toFixed(2)} %`, SOFT ? 'Soft' : 'Hard',
                 DIR ? 'Out' : 'In'];

/* steps:ties:length:phase:ms_step:advancing:cursor:depths:orders
 * Every other step on, two of them tied, step 4 at half amount. */
const DEPTHS = Array.from({ length: 16 },
                          (_, i) => (i % 2 ? 'FF' : (i === 4 ? '80' : 'FF'))).join('');
/*
 * THE ARRIVAL ORDER, 1..N over the ON steps and 00 for a gap -- the mask is
 * 5555, so steps 0,2,4,...,14 sound. Deliberately NOT position order: a mock
 * that only ever showed 1,2,3,4 would let a reviewer miss the whole point of
 * the numbers. This one arrives 4th, 1st, 7th, 2nd, ...
 */
const ORDER_SEQ = [4, 1, 7, 2, 8, 3, 6, 5];
/*
 * The HOLES carry a rank too, among themselves -- that is the order Fade Out
 * introduces them in, and the reason one array can hold both. Position order
 * here, so ?dir=out reads left to right against the scrambled hit order.
 */
const HOLE_SEQ = [1, 2, 3, 4, 5, 6, 7, 8];
const ORDERS = Array.from({ length: 16 }, (_, i) =>
  (i % 2 === 0 ? ORDER_SEQ[i / 2] : HOLE_SEQ[(i - 1) / 2])
    .toString(16).padStart(2, '0').toUpperCase()
).join('');
const UI_STATE = `5555:0044:16:5.400:125.00:1:5:${DEPTHS}:${ORDERS}`;
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
/* ?legato to review Join Neighbors -- three adjacent ON steps must draw ONE
 * attack, not three. That bug was invisible here because legato was hard-coded
 * off AND the editor never read the field. */
const LEGATO = Q0.has('legato') ? '1' : '0';
const PARAMS = `0:${LEGATO}:0:${CURVE}:1/16:15:0.9:${HOLD}:${A}:${D}:${S}:${R}:` +
               `${(HOLD * 125).toFixed(2)}:${FADE}:${SOFT}:${DIR}`;

/*
 * The scope as the plugin sends it:
 * "<cols>:<cycleMs>:<head>:<4 hex bytes a column>", IN PATTERN ORDER -- column k
 * is phase k/cols, not rotated, because the axis is the pattern and stands still.
 *
 * `head` is where the sweep is writing. `roll` advances it so successive pushes
 * are genuinely different data, which is what makes "the sweep is live" testable
 * rather than a claim -- and here it advances the HEAD rather than shifting the
 * whole picture, because a picture that scrolls is the thing this stopped doing.
 *
 * The gate is keyed to the same 16 steps the mask says are on, so the envelope
 * overlay has something to line up with.
 */
const COLS = 256, CYCLE_MS = 2000;
const scope = (roll = 0) => {
  const H = (x) => Math.max(0, Math.min(255,
    Math.round((Math.max(-1, Math.min(1, x)) + 1) * 127.5)))
    .toString(16).toUpperCase().padStart(2, '0');
  const head = roll % COLS;
  let hex = '';
  for (let i = 0; i < COLS; i++) {
    /* The input, as a function of PHASE -- so a column holds the same signal
     * every cycle, which is what a static axis shows. */
    const a = 0.85 * Math.sin(i * 0.31) * (0.6 + 0.4 * Math.sin(i * 0.05));
    /* Gated by the step the column falls in: 16 steps over 256 columns. */
    const step = Math.floor(i / (COLS / 16));
    const open = step % 2 === 0 ? 1 : 0.0;
    /* Columns the sweep has not reached on this pass are quieter, so the
     * "filling left to right" reading is visible in the harness at all. */
    const fresh = i <= head ? 1 : 0.35;
    hex += H(-Math.abs(a * fresh)) + H(Math.abs(a * fresh))
         + H(-Math.abs(a * open * fresh)) + H(Math.abs(a * open * fresh));
  }
  return `${COLS}:${CYCLE_MS}:${head}:${hex}`;
};

/*
 * THE GATE CURVE, as the plugin renders it: "<length>:<perStep>:<hex>", a byte
 * per sample of one cycle.
 *
 * The real one comes out of a scratch engine; this is a stand-in shaped to
 * exercise the thing the editor used to get wrong -- the gate opens on the lit
 * steps and its RELEASE RUNS PAST THE STEP EDGE into the next one, which the
 * old model drew as an instant cut.
 */
const GATE_PER_STEP = 64;
const gateCurve = () => {
  const H = (x) => Math.max(0, Math.min(255, Math.round(x * 255)))
    .toString(16).toUpperCase().padStart(2, '0');
  let hex = '';
  let g = 0;
  for (let s = 0; s < 16; s++) {
    const on = s % 2 === 0;                     /* the 5555 mask */
    for (let k = 0; k < GATE_PER_STEP; k++) {
      const t = k / GATE_PER_STEP;
      if (on && t < HOLD) g = 1;                /* open for Width */
      else g = Math.max(0, g - 1 / (GATE_PER_STEP * 1.6));   /* a long release */
      hex += H(g);
    }
  }
  return `16:${GATE_PER_STEP}:${hex}`;
};

let roll = 0;
/* Every parameter's normalised default, as Params.cpp declares them -- what a
 * double-click resets to. */
const DEFAULTS = [0, 15 / 127, 7 / 12, 0, 0, 0, 1, 1, 1.6 / 200, 16 / 200, 1, 16 / 200, 1, 0, 0];
const pushAll = () => {
  globalThis.SAMFD?.(113, 0, b64(DEFAULTS.join(':')));
  VALUES.forEach((x, i) => globalThis.SPVFD?.(i, x));
  DISPLAY.forEach((d, i) => globalThis.SAMFD?.(i, d.length, b64(d)));
  globalThis.SAMFD?.(64, 0, b64(UI_STATE));
  globalThis.SAMFD?.(65, 0, b64(PARAMS));
  globalThis.SAMFD?.(66, 0, b64(scope(roll)));
  globalThis.SAMFD?.(105, 0, b64(gateCurve()));
};

/* The plugin pushes the window every idle tick; so does this, or the scope
 * would look live only because nothing had asked it to change. */
setInterval(() => { roll += 3; globalThis.SAMFD?.(66, 0, b64(scope(roll))); }, 50);

/* THE RACE, REPRODUCED: this runs now, before the editor exists, and every one
 * of these calls goes nowhere. It is here to be dropped. */
window.__mockEarlyPush = 0;
pushAll();

const MSG_READY = 120, MSG_REQUEST_PATCH = 99;

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

/* A kick for the animated ground is kick.js's job -- a module, because it takes
 * the message tag from the editor's own msg.js rather than retyping it. */

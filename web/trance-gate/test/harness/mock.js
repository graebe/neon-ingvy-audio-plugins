// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The plugin, faked, for reviewing the editor without a host.
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
/* The scope and the gate travel as an ASCII header then RAW bytes; the mock
 * builds them as "<header>:<hex>" for readability and packs them here. */
const binary = (text) => {
  const at = text.lastIndexOf(':') + 1;
  let bin = text.slice(0, at);
  for (let i = at; i < text.length; i += 2) bin += String.fromCharCode(parseInt(text.substr(i, 2), 16));
  return btoa(bin);
};

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
const RATES = ['1/1T', '1/2', '1/2T', '1/4', '1/4T', '1/8', '1/8T', '1/16', '1/16T',
               '1/32', '1/32T', '1/64', '1/128'];
/*
 * THE LENGTH DETENTS, per rate, in 4/4 -- exactly the 4/4 column of tg-core's
 * `the_detents_for_every_rate_and_meter` (rates.rs). The editor is told them
 * and never works them out, so a stand-in table is all the mock needs.
 */
const DETENTS_44 = ['3,6', '1,2,4,8', '3,6,12', '2,4,8,16', '3,6,12,24', '4,8,16,32',
                    '6,12,24,48', '8,16,32,64', '12,24,48,96', '16,32,64,128',
                    '24,48,96', '32,64,128', '64,128'];
/* The readout for the rate at normalised `rate`, as the engine formats it. */
const paramsLine = (rate) => {
  const r = Math.round(rate * 12);
  return `0:${LEGATO}:0:${CURVE}:${RATES[r]}:15:0.9:${HOLD}:${A}:${D}:${S}:${R}:` +
         `${(HOLD * 125).toFixed(2)}:${FADE}:${SOFT}:${DIR}:${DETENTS_44[r]}`;
};

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

/*
 * THE ENVELOPE PLOT'S CURVES, as tg_core_render_envelope returns them:
 * "<steps>:<perStep>:" then the gated curve and the envelope as dialled, a byte
 * a sample. A linear stand-in from ?adsr and ?width -- the real ones come out
 * of a scratch engine.
 */
const envelopeCurves = () => {
  const STEPS = 4, PER = 64;
  const w = HOLD, a = +A / 100 * w, d = +D / 100 * w, sus = +S, r = +R / 100 * w;
  const dialled = (x) => (x < a ? x / a : x < a + d ? 1 - (1 - sus) * (x - a) / d : sus);
  let bin = `${STEPS}:${PER}:`;
  const gated = [], ghost = [];
  for (let i = 0; i < STEPS * PER; i++) {
    const x = i / PER;
    const at = dialled(Math.min(x, w));
    gated.push(x < w ? dialled(x) : r > 0 && x < w + r ? at * (1 - (x - w) / r) : 0);
    ghost.push(dialled(x));
  }
  for (const v of [...gated, ...ghost]) bin += String.fromCharCode(Math.round(Math.max(0, Math.min(1, v)) * 255));
  return btoa(bin);
};

let roll = 0;
/* The current slot's values -- VALUES until the slots below exist, which is
 * after the early push this file makes on purpose. */
let current = () => VALUES;
/* Every parameter's normalised default, as Params.cpp declares them -- what a
 * double-click resets to. */
const DEFAULTS = [0, 15 / 127, 7 / 12, 0, 0, 0, 1, 1, 1.6 / 200, 16 / 200, 1, 16 / 200, 1, 0, 0];
const pushAll = () => {
  globalThis.SAMFD?.(113, 0, b64(DEFAULTS.join(':')));
  VALUES.forEach((x, i) => globalThis.SPVFD?.(i, x));
  DISPLAY.forEach((d, i) => globalThis.SAMFD?.(i, d.length, b64(d)));
  globalThis.SAMFD?.(64, 0, b64(UI_STATE));
  globalThis.SAMFD?.(65, 0, b64(paramsLine(current()[2])));
  globalThis.SAMFD?.(66, 0, binary(scope(roll)));
  globalThis.SAMFD?.(105, 0, binary(gateCurve()));
  globalThis.SAMFD?.(106, 0, envelopeCurves());
};

/* The plugin pushes the window every idle tick; so does this, or the scope
 * would look live only because nothing had asked it to change. */
setInterval(() => { roll += 3; globalThis.SAMFD?.(66, 0, binary(scope(roll))); }, 50);

/* THE RACE, REPRODUCED: this runs now, before the editor exists, and every one
 * of these calls goes nowhere. It is here to be dropped. */
window.__mockEarlyPush = 0;
pushAll();

const MSG_READY = 120, P_SLOT = 0, P_RATE = 2, P_TIME_MODE = 4;

/*
 * EVERY PARAMETER BUT SLOT BELONGS TO A SLOT, as in the plugin. Slot 1 holds
 * the session above; the other seven are fresh, at the defaults. A write lands
 * in the current slot, and a Slot write recalls the new slot's values the way
 * the plugin's switch does: a value and a display string for every parameter
 * (SetParamFromPlugin), and the Slot's own display -- never its value, which
 * the editor wrote.
 */
const slots = Array.from({ length: 8 }, (_, s) => (s === 0 ? VALUES : DEFAULTS).slice());
let slot = 0;
current = () => slots[slot];
/*
 * AND ITS OWN PATTERN: slot 1 is the session's (ties, an accent, a scrambled
 * arrival order), the others the engine's default -- every other step on, full
 * depth, position order. The `ui` readout is the current slot's, re-sent on a
 * switch as the plugin's next idle tick would.
 */
const positional = (steps, n) => {
  let on = 0, off = 0;
  return Array.from({ length: n }, (_, i) => {
    const lit = (parseInt(steps[steps.length - 1 - (i >> 2)] ?? '0', 16) >> (i & 3)) & 1;
    return (lit ? ++on : ++off).toString(16).padStart(2, '0').toUpperCase();
  }).join('');
};
const freshPattern = () => ({ steps: '5555', ties: '0', depths: '', orders: '' });
const patterns = Array.from({ length: 8 }, (_, s) =>
  (s === 0 ? { steps: '5555', ties: '0044', depths: DEPTHS, orders: ORDERS } : freshPattern()));
const lengthOf = (s) => Math.round(slots[s][1] * 127) + 1;
const uiState = (s) => {
  const p = patterns[s], n = lengthOf(s);
  const depths = (p.depths || '').padEnd(2 * n, 'F').slice(0, 2 * n);
  const orders = p.orders ? p.orders.padEnd(2 * n, '0').slice(0, 2 * n) : positional(p.steps, n);
  return `${p.steps}:${p.ties}:${n}:5.400:125.00:1:${Math.min(5, n - 1)}:${depths}:${orders}`;
};
/* A slot file's "pattern" field -- "steps:ties:length[:depths[:orders]]". */
const parsePattern = (field) => {
  const [steps, ties, , depths = '', orders = ''] = field.split(':');
  return { steps, ties, depths, orders };
};
const patternField = (s) => {
  const p = patterns[s];
  return `${p.steps}:${p.ties}:${lengthOf(s)}:${p.depths}${p.orders ? `:${p.orders}` : ''}`;
};
/* Each parameter's text from its normalised value, as Params.cpp formats it. */
const displayOf = (i, v, all) => {
  const pct = (x) => `${(x * 100).toFixed(2)} %`;
  switch (i) {
    case 0: return String(Math.round(v * 7) + 1);
    case 1: return String(Math.round(v * 127) + 1);
    case 2: return RATES[Math.round(v * 12)];
    case 3: return v >= 0.5 ? 'On' : 'Off';
    case 4: return v >= 0.5 ? '%' : 'ms';
    case 5: return ['Linear', 'Exponential', 'S-Curve'][Math.round(v * 2)];
    case 8: case 9: case 11: {
      const stage = v * 200;
      return all[4] >= 0.5 ? `${stage.toFixed(2)} %` : `${(stage / 100 * all[7] * 125).toFixed(1)} ms`;
    }
    case 13: return v >= 0.5 ? 'Soft' : 'Hard';
    case 14: return v >= 0.5 ? 'Out' : 'In';
    default: return pct(v);
  }
};
const say = (i, d) => globalThis.SAMFD?.(i, d.length, b64(d));

/*
 * SLOT FILES. The plugin shows the system's panels; the mock "picks" a fixture
 * file instead -- fixtures/slot.nitgslot, or the bank when the test has set
 * window.__importFixture = 'bank' -- and applies it as the engine would: a slot
 * file into the current slot, a bank into all eight, after which the host's
 * values follow the current slot (recall). An export answers with the status
 * the plugin sends once the file is written.
 *
 * THE CLIPBOARD IS THE PLUGIN'S (ni/Clipboard.h): window.__clipboard stands in
 * for NSPasteboard, which a test may read or fill. Copy writes the current slot
 * as a slot file's text; Paste reads it back as the engine would -- a slot into
 * the current slot, a bank into all eight -- and refuses anything else with the
 * engine's words. (The third kind, a whole patch, is the engine's to read and
 * is tested there: tg-core's paste.rs, tests/cpp/tg_state.cpp.)
 */
const MSG_STATUS = 68, MSG_EXPORT = 107, MSG_IMPORT = 108, MSG_COPY = 109, MSG_PASTE = 110;
window.__clipboard = '';
const soundToValues = (sound, pattern, into) => {
  const f = sound.split(':');
  const length = parseInt(pattern.split(':')[2], 10);
  const v = into.slice();
  v[1] = (length - 1) / 127;
  v[2] = RATES.indexOf(f[0]) / 12;
  [v[8], v[9], v[10], v[11], v[7], v[6], v[12]] =
    [+f[1] / 200, +f[2] / 200, +f[3], +f[4] / 200, +f[5], +f[6], +f[7]];
  [v[13], v[14], v[3], v[4], v[5]] = [+f[8], +f[9], +f[10], +f[11], +f[12] / 2];
  return v;
};
const valuesToSound = (v) => [
  RATES[Math.round(v[2] * 12)], (v[8] * 200).toFixed(2), (v[9] * 200).toFixed(2), v[10].toFixed(3),
  (v[11] * 200).toFixed(2), v[7].toFixed(3), v[6].toFixed(3), v[12].toFixed(4),
  Math.round(v[13]), Math.round(v[14]), Math.round(v[3]), Math.round(v[4]), Math.round(v[5] * 2),
].join(':');
const status = (words) => say(MSG_STATUS, words);
/* A slot or bank file's text into the slots, as tg-core's import applies it;
 * the kind it was, or null for text that is neither. */
const apply = (text) => {
  let file;
  try { file = JSON.parse(text); } catch { return null; }
  if (file?.format === 'ni-trance-gate-bank') {
    for (let s = 0; s < 8; s++) {
      slots[s] = soundToValues(file[`sound${s + 1}`], file[`pattern${s + 1}`], slots[s]);
      patterns[s] = parsePattern(file[`pattern${s + 1}`]);
    }
    return 'bank';
  }
  if (file?.format === 'ni-trance-gate-slot') {
    slots[slot] = soundToValues(file.sound, file.pattern, slots[slot]);
    patterns[slot] = parsePattern(file.pattern);
    return 'slot';
  }
  return null;
};
const importFixture = async () => {
  const name = window.__importFixture === 'bank' ? 'bank.nitgbank' : 'slot.nitgslot';
  const kind = apply(await (await fetch(`fixtures/${name}`)).text());
  status(kind === 'bank' ? `ok:Imported all 8 slots from ${name}.`
                         : `ok:Imported ${name} into slot ${slot + 1}.`);
  recall(slot);
};
const copySlot = () => {
  window.__clipboard = `{\n  "format": "ni-trance-gate-slot",\n  "version": 1,\n  "sound": "${
    valuesToSound(slots[slot])}",\n  "pattern": "${patternField(slot)}"\n}\n`;
  status(`ok:Copied slot ${slot + 1}.`);
};
const pasteSlot = () => {
  const text = window.__clipboard ?? '';
  if (!text.trim()) return status('error:Failed to paste: The clipboard is empty.');
  const kind = apply(text);
  if (!kind) return status("error:Failed to paste: The clipboard doesn't hold a Trance Gate slot.");
  status(kind === 'bank' ? 'ok:Pasted all 8 slots.' : `ok:Pasted into slot ${slot + 1}.`);
  recall(slot);
};
const recall = (to) => {
  slot = to;
  globalThis.SAMFD?.(64, 0, b64(uiState(to)));
  say(P_SLOT, displayOf(P_SLOT, to / 7, slots[to]));
  slots[to].forEach((v, i) => {
    if (i === P_SLOT) return;
    globalThis.SPVFD?.(i, v);
    say(i, displayOf(i, v, slots[to]));
  });
  globalThis.SAMFD?.(65, 0, b64(paramsLine(slots[to][2])));
};

/*
 * THE HOST MOVING A PARAMETER -- automation, its own UI -- as the plugin
 * relays it: the value, its display text and, for the Rate, the engine's next
 * `params` readout with that rate's detents.
 */
window.__hostMoves = (i, v) => {
  slots[slot][i] = v;
  globalThis.SPVFD?.(i, v);
  say(i, displayOf(i, v, slots[slot]));
  if (i === P_RATE) globalThis.SAMFD?.(65, 0, b64(paramsLine(v)));
};

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
  if (m?.msg === 'SAMFUI' && m.msgTag === MSG_EXPORT) {
    status(atob(m.data ?? '') === 'bank'
      ? 'ok:Exported all 8 slots to NI Trance Gate Bank.nitgbank.'
      : `ok:Exported slot ${slot + 1} to NI Trance Gate Slot ${slot + 1}.nitgslot.`);
    return;
  }
  if (m?.msg === 'SAMFUI' && m.msgTag === MSG_IMPORT) {
    importFixture();
    return;
  }
  if (m?.msg === 'SAMFUI' && m.msgTag === MSG_COPY) {
    copySlot();
    return;
  }
  if (m?.msg === 'SAMFUI' && m.msgTag === MSG_PASTE) {
    pasteSlot();
    return;
  }
  if (m?.msg === 'SPVFUI' && m.paramIdx === P_SLOT) {
    const to = Math.round(m.value * 7);
    if (to !== slot) recall(to);
    return;
  }
  if (m?.msg === 'SPVFUI' && m.paramIdx > P_SLOT) slots[slot][m.paramIdx] = m.value;
  /* A Rate moved: the engine's next `params` readout carries its detents. */
  if (m?.msg === 'SPVFUI' && m.paramIdx === P_RATE)
    globalThis.SAMFD?.(65, 0, b64(paramsLine(m.value)));
  /* Env Time switched: the stage readouts follow it, as OnEditorIdle re-sends
   * them -- in ms of the gate's width, or in percent. That is the plugin's
   * DISPLAY text, not a value echo, so the rule below still holds. */
  if (m?.msg === 'SPVFUI' && m.paramIdx === P_TIME_MODE) {
    const ms = m.value < 0.5;
    const widthMs = HOLD * 125;
    [[8, +A], [9, +D], [11, +R]].forEach(([i, pct]) => {
      const d = ms ? `${(pct / 100 * widthMs).toFixed(1)} ms` : `${pct.toFixed(2)} %`;
      globalThis.SAMFD?.(i, d.length, b64(d));
    });
  }
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

/* The animated ground's rings are the kit's harness/beat.js: a playing
 * transport, shared by every editor's harness. */

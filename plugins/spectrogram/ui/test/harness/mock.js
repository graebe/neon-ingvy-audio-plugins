/*
 * The plugin, faked, for reviewing the picture without a host.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * IT PUSHES BEFORE THE EDITOR LOADS, ON PURPOSE.
 *
 * This is a classic <script> and the editor is a <script type="module">, so this
 * file runs FIRST and the editor's code has not evaluated yet -- exactly the
 * order the real plugin produces, where OnUIOpen fires from didFinishNavigation
 * before the deferred module does. The axis pushed below therefore lands on an
 * undefined global and is dropped, which is the bug kMsgReady exists to fix. If
 * you make this deferred or delayed, you have disabled the test.
 *
 * WHAT IT DOES NOT DO IS ANALYSE ANYTHING. The columns below are drawn, not
 * measured: a sweeping partial, a pulse in the bass on a slow beat, and a haze
 * near the floor. The analyzer has its own tests (cargo test -p spectro-core,
 * and tests/spectro_columns.c through the C ABI); this exists to answer "does
 * the picture scroll, is the ramp readable, is the scale beside the right
 * bands" -- which no assertion answers as well as looking at it.
 */
const b64 = (s) => btoa(String.fromCharCode(...new TextEncoder().encode(s)));
/* A column batch as the plugin sends it: an ASCII header, then RAW bytes -- the
 * mock builds its columns as hex for readability and packs them here. */
const binary = (header, hex) => {
  let bin = header;
  for (let i = 0; i < hex.length; i += 2) bin += String.fromCharCode(parseInt(hex.substr(i, 2), 16));
  return btoa(bin);
};

const BANDS = 256;
const MSG_COLS = 64, MSG_AXIS = 65, MSG_SYNC = 66, MSG_SOURCES = 67,
      MSG_CLASHCOLS = 68, MSG_RANGE = 96, MSG_SELECT = 97, MSG_VIEW = 99,
      MSG_COMPARE = 100, MSG_READY = 120, MSG_STATE = 69;

/*
 * A FAKE TRANSPORT, so the bar view can be reviewed without a host.
 *
 * 120 BPM in 4/4, running, with the position derived from the same clock the
 * columns are -- which is the point: if the two disagreed, the sweep would
 * scatter and the harness would be testing its own bookkeeping instead of the
 * editor's. `?bpm=N` moves it, `?stopped` parks the transport to exercise the
 * free-wheeling branch.
 */
const BPM = Number(/bpm=([\d.]+)/.exec(location.search)?.[1]) || 120;
const RUNNING = !location.search.includes('stopped');

/* The range the fake analyzer is currently spread over -- moved by the editor's
 * dropdown, exactly as the plugin's kMsgRange moves the real one. */
let fMin = 10, fMax = 20000;

/* The same geometric axis the analyzer computes, and a second copy of it --
 * acceptable HERE and nowhere else, because this file's whole job is to stand in
 * for the thing that owns the first copy. */
const axis = () => {
  const lo = Math.log(fMin), hi = Math.log(fMax);
  const out = [];
  for (let b = 0; b < BANDS; b++) {
    const a = Math.exp(lo + ((hi - lo) * b) / BANDS);
    const c = Math.exp(lo + ((hi - lo) * (b + 1)) / BANDS);
    out.push(Math.sqrt(a * c).toFixed(1));
  }
  return out.join(',');
};

const H = (v) => Math.max(0, Math.min(255, Math.round(v))).toString(16)
  .toUpperCase().padStart(2, '0');

/* One drawn column. `t` is seconds, so the sweep and the pulse are functions of
 * time rather than of how often this happens to be called. */
/* Which band a frequency falls in, under whatever range is selected -- so the
 * fake sweep and the fake kick stay at the same FREQUENCIES when the dropdown
 * zooms, instead of at the same band indices. Zooming then shows what zooming
 * should: the same material, spread out. */
const bandOf = (hz) =>
  (Math.log(hz) - Math.log(fMin)) / (Math.log(fMax) - Math.log(fMin)) * BANDS;

const column = (t) => {
  /* A partial sweeping 60 Hz .. 6 kHz and back over 6 seconds. */
  const sweepHz = 60 * (100 ** (0.5 - 0.5 * Math.cos((2 * Math.PI * t) / 6)));
  const centre = bandOf(sweepHz);
  /* A kick every half second, decaying, at 45 Hz. */
  const beat = Math.exp(-8 * (t % 0.5));
  const lowBand = bandOf(45);
  /* Widths in bands scale with the zoom, so a zoomed partial is a thicker
   * stroke rather than the same stroke -- which is what more resolution over a
   * narrower range actually looks like. */
  const spread = (BANDS / Math.log2(fMax / fMin)) / 12;
  let hex = '';
  for (let b = 0; b < BANDS; b++) {
    const partial = 250 * Math.exp(-((b - centre) ** 2) / (2 * spread * spread));
    const low = 230 * beat * Math.exp(-((b - lowBand) ** 2) / (6 * spread * spread));
    /* A haze that falls away with frequency: near the floor, never at it, so the
     * bottom of the ramp is visible in the picture too. */
    const haze = 40 * Math.exp(-b / BANDS) * (0.6 + 0.4 * Math.sin(b * 1.7 + t * 3));
    hex += H(Math.max(partial, low, haze));
  }
  return hex;
};

/* THE RACE, REPRODUCED: this runs now, before the editor exists, and goes
 * nowhere. It is here to be dropped. */
globalThis.SAMFD?.(MSG_AXIS, 0, b64(axis()));

/* Everything the editor sends, for a review or a headless check to assert on. */
window.__sent = [];

/* The session first, as the plugin does -- the editor pushes nothing until it
 * has applied it. ?zoom=lo:hi reviews a reopened zoom. */
const sendState = () => {
  const range = /zoom=([\d.]+:[\d.]+)/.exec(location.search)?.[1] ?? '10.00:20000.00';
  globalThis.SAMFD?.(MSG_STATE, 0,
    b64(`${range}:${viewing.join(',')}:${cmpA}:${cmpB}:${clashWanted ? 1 : 0}:-60.00:12.00`));
};

window.IPlugSendMsg = (m) => {
  window.__sent.push(m);
  /* kMsgReady -- the editor has mounted and is listening. This is the reply that
   * actually delivers the axis, and the whole point of the handshake. */
  if (m?.msg === 'SAMFUI' && m.msgTag === MSG_READY) {
    /* ?holdstate withholds the session until window.__releaseState() -- the
     * window in which an editor that pushed early would overwrite it. */
    if (/[?&]holdstate\b/.test(location.search)) window.__releaseState = sendState;
    else sendState();
    globalThis.SAMFD?.(MSG_AXIS, 0, b64(axis()));
    sendSources();
  }
  if (m?.msg === 'SAMFUI' && m.msgTag === MSG_SELECT) {
    chosen = atob(m.data ?? '').split(',').filter(Boolean).map(Number);
    sendSources();
  }
  if (m?.msg === 'SAMFUI' && m.msgTag === MSG_VIEW) {
    viewing = atob(m.data ?? '').split(',').filter((x) => x !== '').map(Number);
    if (!viewing.length) viewing = [0];
  }
  if (m?.msg === 'SAMFUI' && m.msgTag === MSG_COMPARE) {
    const [a, b, on] = atob(m.data ?? '').split(':').map(Number);
    cmpA = a; cmpB = b; clashWanted = !!on;
  }
  /*
   * The range, the way the plugin handles it: re-band, then send the scale BACK
   * rather than letting the editor assume its request was honoured.
   *
   * NOTE WHAT IS NOT HERE: a pause message. Pause holds the view inside the
   * editor and the plugin is never told -- so this mock keeps pushing columns
   * through a freeze, which is exactly the behaviour that makes unpausing show
   * a current picture. A mock that stopped would hide the whole point.
   */
  if (m?.msg === 'SAMFUI' && m.msgTag === MSG_RANGE) {
    const [lo, hi] = atob(m.data ?? '').split(':').map(Number);
    if (Number.isFinite(lo) && Number.isFinite(hi) && hi > lo * 1.5) {
      fMin = lo;
      fMax = hi;
      globalThis.SAMFD?.(MSG_AXIS, 0, b64(axis()));
    }
  }
};

/*
 * THE COLUMN FEED, at the plugin's own cadence: the analyzer finishes ~47
 * columns a second and OnIdle runs at 60 Hz, so most ticks carry ONE and some
 * carry none. Sending one every frame would look smoother here and would hide
 * both the batching the wire format exists for and the real scroll speed.
 */
const t0 = performance.now();
let frozen = location.search.includes('freeze');
let due = 0;
setInterval(() => {
  if (frozen) return;
  const t = (performance.now() - t0) / 1000;
  /* 47 a second against a 60 Hz tick: one column most ticks, none on the rest. */
  due += 47 / 60;
  const count = Math.floor(due);
  if (count < 1) return;
  due -= count;
  let hex = '';
  for (let c = 0; c < count; c++) hex += column(t + c * 0.021);
  /* The clock FIRST and every tick, exactly as OnIdle sends it -- the editor
   * places the columns with the position it already has in hand. */
  sendSync(t);

  /*
   * ONE SUMMED STREAM, exactly as the plugin sends it: the editor never learns
   * it was several. Channel 0 is this track's sweep; every bus is the same
   * drawn source, so viewing two must read ~3 dB hotter than viewing one --
   * which is the claim a probe can check.
   */
  let viewHex = '';
  for (let c = 0; c < count; c++) {
    const own = hex.slice(c * BANDS * 2, (c + 1) * BANDS * 2);
    const parts = viewing.map((ch) => (ch === 0 ? own : busColumn()));
    viewHex += parts.length > 1 ? sumHex(parts) : parts[0] ?? own;
  }
  globalThis.SAMFD?.(MSG_COLS, 0, binary(`0:${count}:${BANDS}:`, viewHex));

  /* And the mask between the two channels the editor NAMED. */
  if (clashWanted && cmpA !== cmpB) {
    let chex = '';
    for (let c = 0; c < count; c++) {
      chex += clashColumn(hex.slice(c * BANDS * 2, (c + 1) * BANDS * 2));
    }
    globalThis.SAMFD?.(MSG_CLASHCOLS, 0, binary(`0:${count}:${BANDS}:`, chex));
  }
}, 16);

/*
 * TWO FAKE LISTEN-INs, and a second source drawn to overlap the first in a
 * KNOWN band range -- bands 80..120 -- so a review (and a headless probe) can
 * assert the orange lands there and nowhere else.
 */
const SOURCES = [
  { slot: 1, live: 1, rate: 48000, label: 'Bass' },
  { slot: 4, live: 1, rate: 48000, label: 'Pad' },
];
const CLASH_LO = 80, CLASH_HI = 120;
let chosen = [];
/* What the editor asked to SEE and what it asked to COMPARE -- the mock has to
 * honour the same split the plugin does, or the harness would be testing its
 * own bookkeeping instead of the editor's. */
let viewing = [0];
let cmpA = 0, cmpB = 1, clashWanted = false;

/* dB <-> byte, and POWER summation, the same arithmetic spectro-core does. A
 * byte is linear in dB, so the sum has to leave byte space: two equal sources
 * are +3 dB, not double the byte. */
const FLOOR = -96, CEIL = 0;
const byteToDb = (b) => (b <= 0 ? -Infinity : FLOOR + (b / 255) * (CEIL - FLOOR));
const dbToByte = (db) => Math.max(0, Math.min(255, Math.round(((db - FLOOR) / (CEIL - FLOOR)) * 255)));
const sumHex = (hexes) => {
  let out = '';
  for (let b = 0; b < BANDS; b++) {
    let power = 0;
    for (const h of hexes) {
      const db = byteToDb(parseInt(h.slice(b * 2, b * 2 + 2), 16));
      if (Number.isFinite(db)) power += 10 ** (db / 10);
    }
    out += H(power > 0 ? dbToByte(10 * Math.log10(power)) : 0);
  }
  return out;
};

const sendSources = () => globalThis.SAMFD?.(MSG_SOURCES, 0,
  b64(SOURCES.map((s) => `${s.slot}:${s.live}:${s.rate}:${s.label}`).join('\n')));

/* A bus's column: loud only inside the overlap window, so what clashes is
 * decided by this file rather than by whatever the sweep happens to do. */
const busColumn = () => {
  let hex = '';
  for (let b = 0; b < BANDS; b++) {
    const inside = b >= CLASH_LO && b <= CLASH_HI;
    hex += H(inside ? 210 : 4);
  }
  return hex;
};

/* The mask the plugin would have measured: both loud and level across the
 * window, nothing outside it. */
const clashColumn = (own) => {
  let hex = '';
  for (let b = 0; b < BANDS; b++) {
    const inside = b >= CLASH_LO && b <= CLASH_HI;
    const ownByte = parseInt(own.slice(b * 2, b * 2 + 2), 16);
    hex += H(inside && ownByte > 120 ? 230 : 0);
  }
  return hex;
};

/* One column is 1/47 s, which at this tempo is this many beats. */
const ppqPerCol = (1 / 47) * (BPM / 60);
const sendSync = (t) => {
  const ppq = t * (BPM / 60);
  globalThis.SAMFD?.(MSG_SYNC, 0,
    b64(`${ppq.toFixed(6)}:${BPM.toFixed(4)}:4:4:${RUNNING ? 1 : 0}:${ppqPerCol.toFixed(8)}:48000`));
};

/* ?freeze holds the picture after two seconds, so a screenshot catches a full
 * view rather than a quarter-filled one. */
if (location.search.includes('freeze')) {
  frozen = false;
  setTimeout(() => { frozen = true; }, 2000);
}

/*
 * ?pause clicks the button two seconds in and ?range=N picks a range, so both
 * can be reviewed headlessly -- the same trick the Trance Gate's harness uses to
 * reach its signal tab.
 *
 * ?pause is the interesting one: the feed above keeps running through it, so a
 * screenshot taken later shows a picture frozen at two seconds while the history
 * behind it has moved on. Unpausing then jumps to the current view, which is the
 * behaviour being checked.
 */
/*
 * MATCHED AS A FLAG, NOT AS A SUBSTRING. This was `includes('pause')`, and
 * "resume" does not contain "pause" -- so ?resume, the one invocation the
 * comment below documents, only ever pressed the button at eight seconds and
 * left the picture paused forever. The review it exists for was unreachable.
 */
const flag = (name) => new RegExp(`[?&]${name}(?:[=&]|$)`).test(location.search);

if (flag('pause') || flag('resume')) {
  setTimeout(() => document.querySelector('.btn')?.click(), 2000);
}
/* ?resume pauses at two seconds and lets go at eight, which is the claim worth
 * looking at: the picture that comes back must contain those six seconds --
 * scrolled past, present in the history -- not resume from a seam. */
if (flag('resume')) {
  setTimeout(() => document.querySelector('.btn')?.click(), 8000);
}
/*
 * ?hover=X,Y parks the pointer over the picture at those CANVAS coordinates, so
 * the crosshair and the three readouts can be reviewed in a screenshot. A real
 * pointer cannot be moved from a script; the component listens for pointermove
 * and that is what this sends, at the right client coordinates for the canvas's
 * own box -- which is what makes the page zoom part of the test rather than
 * something the harness works around.
 */
const hover = /hover=(-?[\d.]+),(-?[\d.]+)/.exec(location.search);
if (hover) {
  setTimeout(() => {
    const el = document.querySelector('.spectro');
    if (!el) return;
    const r = el.getBoundingClientRect();
    const w = Number(hover[1]), h = Number(hover[2]);
    el.dispatchEvent(new PointerEvent('pointermove', {
      bubbles: true,
      clientX: r.left + (w / 606) * r.width,
      clientY: r.top + (h / 256) * r.height,
    }));
  }, 3000);
}

/*
 * ?bars=N throws the bar switch and picks the window, so the sweep, the
 * playhead and the bar grid can be screenshotted. N is the VALUE (1/2/4/8/16),
 * not the dropdown index -- a review flag should say what it means.
 */
/*
 * ?listen ticks every channel in the VIEW picker (so the picture is a sum) and
 * turns the clash on. ?view=0,1 picks an explicit set instead.
 *
 * ASYNC, WITH A TICK BETWEEN OPENING AND CLICKING. The panel's rows are
 * RENDERED when it opens, not hidden by CSS, so querying them in the same turn
 * as the click finds nothing and the whole flag silently does nothing.
 */
if (location.search.includes('listen') || location.search.includes('view=')) {
  const pause = (ms) => new Promise((r) => setTimeout(r, ms));
  setTimeout(async () => {
    const want = /view=([\d,]+)/.exec(location.search);
    const face = document.querySelector('.checklist-face');
    face?.click();
    await pause(50);

    const rows = [...document.querySelectorAll('.checklist-row [role="switch"]')];
    for (let i = 0; i < rows.length; i++) {
      const on = rows[i].getAttribute('aria-checked') === 'true';
      const wanted = want ? want[1].split(',').map(Number).includes(i) : true;
      if (on !== wanted) { rows[i].click(); await pause(20); }
    }
    face?.click();
    await pause(50);

    if (!location.search.includes('noclash')) {
      document.querySelector('.control-group:last-child [role="switch"]')?.click();
    }
  }, 1400);
}

const wantedBars = /bars=(\d+)/.exec(location.search);
if (wantedBars) {
  setTimeout(() => {
    const i = [1, 2, 4, 8, 16].indexOf(Number(wantedBars[1]));
    const sel = document.querySelectorAll('.select select')[1];
    if (sel && i >= 0) {
      sel.value = String(i);
      sel.dispatchEvent(new Event('change', { bubbles: true }));
    }
    document.querySelector('[role="switch"]')?.click();
  }, 1200);
}

const wanted = /range=(\d)/.exec(location.search);
if (wanted) {
  setTimeout(() => {
    const el = document.querySelector('.select select');
    if (!el) return;
    el.value = wanted[1];
    el.dispatchEvent(new Event('change', { bubbles: true }));
  }, 1500);
}

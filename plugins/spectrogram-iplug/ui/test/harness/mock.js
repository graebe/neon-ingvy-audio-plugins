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

const BANDS = 256;
const MSG_COLS = 64, MSG_AXIS = 65, MSG_RANGE = 96, MSG_READY = 102;

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

window.IPlugSendMsg = (m) => {
  window.__sent.push(m);
  /* kMsgReady -- the editor has mounted and is listening. This is the reply that
   * actually delivers the axis, and the whole point of the handshake. */
  if (m?.msg === 'SAMFUI' && m.msgTag === MSG_READY) {
    globalThis.SAMFD?.(MSG_AXIS, 0, b64(axis()));
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
  globalThis.SAMFD?.(MSG_COLS, 0, b64(`${count}:${BANDS}:${hex}`));
}, 16);

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
if (location.search.includes('pause')) {
  setTimeout(() => document.querySelector('.btn')?.click(), 2000);
}
/* ?resume pauses at two seconds and lets go at eight, which is the claim worth
 * looking at: the picture that comes back must contain those six seconds --
 * scrolled past, present in the history -- not resume from a seam. */
if (location.search.includes('resume')) {
  setTimeout(() => document.querySelector('.btn')?.click(), 8000);
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

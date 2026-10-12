// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The animated ground: a damped wave field on the dot paper.
 *
 * A PORT, NOT A DESIGN. Every number and every step here comes from the
 * Ultraviolet design system's own reference implementation,
 * design/scheme/project/components/ground.js (UVGround.Field, 1.3.0), and the
 * model is spelled out in design/scheme/project/README.md under Motion.
 * Nothing in this file is a judgement call about how the background should
 * look -- if it reads wrong, the design system is where that gets decided.
 *
 * THE MODEL. The damped, viscous 2D wave equation
 *
 *   u_tt = c^2 lap(u) - gamma u_t + nu lap(u_t) + A s psi_f(t - t0) G(x - e)
 *
 * on a 6 px grid, every second node a dot, lap the isotropic 9-point stencil
 * so a ring stays round on the square grid. A ring is a DROP: psi a Ricker
 * wavelet peaking at f (one slow wave of wavelength c / f), G a Gaussian of
 * `spot` around one emitter e, s its strength -- a circle that spreads, thins
 * as it goes and meets the boxes.
 *
 * THE EMITTERS stand just off the boxes' outer corners, `inset` along both
 * edges, where no other box and no border is within `clear`; with no box at
 * all, the window centre is one. The one nearest the centre is the anchor: a
 * downbeat (a ring of full strength) falls there and starts the bar's walk,
 * and every other ring falls at the next emitter clockwise around the centre.
 * Each ring's frequency and strength vary a little (varyF, varyS) by a hash of
 * its count since the grid was built -- the same every time, so the ground
 * never repeats like a machine and every test is still deterministic.
 *
 * BOUNDARIES. Boxes reflect totally (Neumann: a neighbour inside a wall reads
 * as the node itself, so no phase flip), so rings echo between the boxes and
 * bend around their corners. The window border is an impedance edge -- Mur's
 * one-way update at the speed alpha c, alpha = (1 - R) / (1 + R) -- which
 * sends back borderR of a wave meeting it head on, so the window never fills
 * with its own standing waves. gamma = 2 / tau fades every wave alike; nu
 * fades it by its wavenumber squared, so ripple at the grid's scale dies at
 * once while the ring softens and lives on. The field comes to rest about ten
 * seconds after the last ring.
 *
 * DISPLAY. A dot shows tanh(u + slope (lambda / 2 pi) du/dl): the height plus
 * the slope towards a light at the top left, so a crest lights on the side
 * facing the light and shades on the other. Up: the dots grow and brighten
 * and the grain thickens; down: they shrink and dim and it thins. THE GRAIN
 * ITSELF NEVER MOVES, only its local density: moving grain reads as
 * television static.
 *
 * With no ring the field is exactly zero, the canvas shows the static design --
 * the same dots, the same colours, grain of the same density -- and the render
 * loop STOPS. That last part is not an optimisation, it is the design's rule:
 * "Controls never animate", and a background that idles is a background that is
 * animating. (It is not PIXEL-identical to the CSS fallback underneath: the
 * grain is uniform noise either way, but this tile is drawn fresh with
 * Math.random, as the design's reference does, and the CSS one is the design's
 * fixed PNG. Only the canvas is ever on screen while it works, so no seam shows.)
 *
 * ITS CLOCK IS THE PLUGIN, NOT requestAnimationFrame, AND IT DOES NOT PAUSE
 * WHILE THE DOCUMENT IS HIDDEN. In a real host WebKit treats a plugin editor
 * as a hidden page: about one animation frame in three seconds, and the
 * page's own timers throttled to a few hertz -- while the plugin's messages
 * arrive at once. A field clocked by frames, or one that paused while hidden,
 * as this one once did, never moved in Live. So the field reports when it
 * starts and stops moving (`onRunning`), the plugin sends a frame tick on
 * every idle tick in between, about fifty a second (ni/Editor.h, kGroundTick),
 * and each `tick()` steps the simulation by the time that really passed and
 * draws. A page timer is only the fallback for ticks that stop coming -- a
 * harness, a plugin too old to send them. (The reference has only the timer;
 * it keeps the same rule, never an animation frame, never paused while
 * hidden.) Work stops when the field rings out, and the plugin stops ringing
 * it when its window closes. prefers-reduced-motion is followed live rather
 * than read once.
 *
 * ONE STENCIL PASS, NOT TWO. The reference takes lap(u) and lap(u_prev) at
 * every node; the stencil and its mirroring at the walls are linear, so
 * k^2 lap(u) + nu (lap(u) - lap(u_prev)) is lap of one mix of the two, and a
 * step makes the mix once and takes one 9-point pass over it -- the same
 * equation at half the cost, and the same way the native kit's GroundField
 * does it. The two agree to float rounding (web/kit/test/field.test.mjs runs
 * the reference beside this and compares).
 *
 * WHAT IS NOT HERE, AND WHY. The reference ships a second half, a BeatClock
 * that a page ticks with the host's position to find the beats. A plugin's
 * page cannot see the host's transport, so the same rule runs in Rust on the
 * audio thread (engines/ground): a ring on every beat while the song plays,
 * the downbeat strongest (docs/tech/ground.md), arriving as a message;
 * `trigger` is where it lands, and a ring of full strength is the downbeat
 * that goes to the anchor. This is the ONLY part of the reference that is not
 * here.
 *
 * NO COLOUR IS SPELLED HERE. The reference carries three RGB triples in its
 * defaults; a canvas cannot use a CSS variable, so this reads them back out of
 * tokens.css through `readRgb` -- the same seam, and for the same reason, as
 * lib/ramp.js. See the note at the top of that file.
 */

import { readRgb, cssHex } from './ramp.js';
import { viewScale, backingRatio } from './ground-geometry.js';

/* The design system release this ports, as the reference exposes it
 * (UVGround.version). The parameter table can stay the same across a release
 * that changes the model -- 1.3.0 did both -- so the test holds this to the
 * reference as well as the numbers. */
const VERSION = '1.3.0';

/*
 * THE DESIGN'S PARAMETER TABLE. Retuning any of these is a design-system change
 * and not a code change: `gain`, `tau` and `borderR` in particular are what
 * keep the field reading as a background rather than as an effect, and the
 * README says so.
 */
const DEFAULTS = {
  pitch: 12,       // dot pitch, px (space-3)
  cell: 6,         // simulation grid, px (half a pitch)
  speed: 100,      // c, px/s
  freq: 1.5,       // f0 of the source wavelet, Hz -> wavelength c/f0 ~ 67 px; a ring on every beat (90-140 BPM) does not cancel the last
  tau: 10,         // amplitude e-folding time, s (gamma = 2/tau); with the border and the spreading, ~10 s from the last ring to rest
  visc: 22,        // nu, px^2/s: fades a wave by its wavenumber squared; grid-scale ripple dies within a second, the ring lives on
  gain: 220,       // source strength A: a downbeat lifts the dots ~300 px from its emitter, soft-clipped at the drop
  spot: 16,        // sigma of a drop's Gaussian footprint, px: a soft splash, not a clipped block
  inset: 9,        // an emitter sits this far off its box's corner, along both edges, px
  clear: 18,       // no other box within this of an emitter, px: a narrower gutter has none
  borderR: 0.5,    // share of a wave the window border reflects head on: 0 lets it leave, 1 reflects it all
  varyF: 0.08,     // +- relative frequency from ring to ring
  varyS: 0.12,     // +- relative strength from ring to ring
  slope: 0.7,      // display: weight of the slope facing the light (top left) against the height
  dt: 1 / 60,      // fixed simulation step, s
  maxSteps: 6,     // per frame, after a stall
  rest: 0.004,     // below this everywhere (and no ring active) the field is reset and the loop stops
  dotR: 1.0,       // dot radius at rest, px
  dotDR: 0.4,      // +- radius at |v| = 1
  dotDA: 0.3,      // +- relative dot opacity at |v| = 1
  grainBase: 0.5,  // grain layer opacity at rest (tile mean 10% -> 5% effective)
  grainK: 0.8,     // grain gain per unit v
  grainMin: 0.2,   // -> 2% effective in the deepest valley
  grainMax: 0.9,   // -> 9% effective on the highest peak
  fps: 30,         // the fallback timer's rate while the field moves; 0 at rest
};

/* The three tokens the canvas needs. The reference implementation carries these
 * as RGB literals in its defaults; here they are read back out of tokens.css,
 * for the reason lib/ramp.js gives at length -- a colour written into JavaScript
 * is a colour outside the stylesheet, and the token guard exists to stop that.
 *
 * THERE IS NO FALLBACK, deliberately, and ramp.js's `readStops` set the
 * precedent: a missing token is a bug in the stylesheet, not a case to paper
 * over. A plausible default would leave the ground looking nearly right with
 * colours nobody chose -- and, worse, would be a second copy of three values the
 * guard cannot see, which is exactly the drift it was written to prevent. */
const COLOUR_TOKENS = {
  bg: '--bg-000',
  dot: '--bg-dot',
  grain: '--uv-deep',
};

/* The sprite atlas quantises the wave into this many levels. MID is v = 0, the
 * ground at rest -- and a cell at MID is skipped entirely when drawing, which is
 * what makes a still field cost nothing. */
const LEVELS = 25;
const MID = (LEVELS - 1) / 2;

/* The grain tile: 96 px, a multiple of the 12 px pitch, so it tiles with the
 * dots. Alpha 0..20% gives a mean of 10%, which at grainBase 0.5 is the design's
 * 5% at rest. UNIFORM WHITE NOISE, not fractal -- the design is explicit, and a
 * fractal tile reads as cloth rather than as grain. */
const GRAIN_TILE = 96;
const GRAIN_MAX_ALPHA = 52;   // of 255, i.e. ~20%

/* The dot is drawn at this alpha over the background so that it reproduces
 * --bg-dot exactly; peaks then reach 100%. Any other value and a field at rest
 * would not match the static CSS ground. */
const DOT_ALPHA = 0.77;

/* With no frame tick from the plugin for this long, the field's own page
 * timer steps it instead -- slowly, in a host's hidden page, but never not at
 * all. */
const FALLBACK_AFTER_MS = 100;

const clamp = (v, a, b) => (v < a ? a : v > b ? b : v);
const now = () => (globalThis.performance || Date).now();

/** Uniform white noise in `rgb`, as a tile. */
function makeGrain(rgb) {
  const n = GRAIN_TILE;
  const c = document.createElement('canvas');
  c.width = n;
  c.height = n;
  const x = c.getContext('2d');
  const img = x.createImageData(n, n);
  const d = img.data;
  for (let i = 0; i < d.length; i += 4) {
    d[i] = rgb[0];
    d[i + 1] = rgb[1];
    d[i + 2] = rgb[2];
    d[i + 3] = Math.floor(Math.random() * GRAIN_MAX_ALPHA);
  }
  x.putImageData(img, 0, 0);
  return c;
}

/**
 * The dot fill that, at DOT_ALPHA over `bg`, comes out as `dot` exactly.
 *
 * Solving for the source colour rather than just drawing `dot` at full opacity
 * is what lets a peak brighten the dot ABOVE its resting colour without a second
 * token: the same fill at a higher alpha is the brighter dot.
 */
function dotBase(bg, dot) {
  const a = DOT_ALPHA;
  const s = [];
  for (let i = 0; i < 3; i++) s.push((dot[i] - bg[i] * (1 - a)) / a);
  return { rgb: cssHex(s), alpha: a };
}

/** A Ricker wavelet (the "Mexican hat"), peak at frequency `f`. */
function ricker(t, f) {
  let a = Math.PI * f * (t - 1 / f);
  a *= a;
  return (1 - 2 * a) * Math.exp(-a);
}

/**
 * A ring's count -> two numbers in -1..1, the same every time: a 32-bit
 * integer mix, so ring 0 of every fresh field is varied alike. [frequency,
 * strength].
 */
function vary(n) {
  let h = n >>> 0;
  h = Math.imul(h ^ (h >>> 16), 0x45d9f3b);
  h = Math.imul(h ^ (h >>> 16), 0x45d9f3b);
  h = (h ^ (h >>> 16)) >>> 0;
  return [(h & 0xffff) / 32767.5 - 1, (h >>> 16) / 32767.5 - 1];
}

/** How far (x, y) is from a box; 0 inside it. */
function toBox(x, y, b) {
  const dx = Math.max(b.x - x, 0, x - b.x - b.w);
  const dy = Math.max(b.y - y, 0, y - b.y - b.h);
  return Math.sqrt(dx * dx + dy * dy);
}

/**
 * h^2 lap(v) at node q, the isotropic 9-point stencil. A neighbour inside a
 * wall or off the grid reads as the node itself: a zero-gradient (Neumann)
 * edge, which reflects with no phase flip.
 */
function lap(v, wall, q, i, j, nx, ny) {
  const c = v[q];
  const L = i > 0;
  const R = i < nx - 1;
  const T = j > 0;
  const B = j < ny - 1;
  const l = L && !wall[q - 1] ? v[q - 1] : c;
  const r = R && !wall[q + 1] ? v[q + 1] : c;
  const t = T && !wall[q - nx] ? v[q - nx] : c;
  const d = B && !wall[q + nx] ? v[q + nx] : c;
  const tl = L && T && !wall[q - nx - 1] ? v[q - nx - 1] : c;
  const tr = R && T && !wall[q - nx + 1] ? v[q - nx + 1] : c;
  const bl = L && B && !wall[q + nx - 1] ? v[q + nx - 1] : c;
  const br = R && B && !wall[q + nx + 1] ? v[q + nx + 1] : c;
  return (4 * (l + r + t + d) + tl + tr + bl + br - 20 * c) / 6;
}

/**
 * Read the three colours out of the document's own custom properties.
 *
 * `el` is the element to resolve against, so a preview or a test can hand in a
 * subtree with different values.
 */
function readColours(el) {
  const out = {};
  for (const [key, token] of Object.entries(COLOUR_TOKENS)) {
    const rgb = readRgb(token, el);
    if (!rgb) throw new Error(`${token} is not a 6-digit hex colour in tokens.css`);
    out[key] = rgb;
  }
  return out;
}

export class Field {
  /**
   * @param canvas  the <canvas> to draw into; its CSS size is the field's size
   * @param opts    overrides for DEFAULTS, plus `colourFrom` (an element to
   *                resolve the tokens against, default documentElement)
   */
  constructor(canvas, opts = {}) {
    const o = (this.o = { ...DEFAULTS, ...opts });
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');
    this.rings = [];
    this.rects = [];
    this.emitters = [];
    this.enabled = true;
    this.running = false;
    this.fallback = 0;
    this.lastTick = 0;
    /* When the last tick from outside arrived; see `tick`. */
    this.lastOutside = -Infinity;
    this.acc = 0;
    this.t = 0;
    /* The level each dot was last drawn at; see `draw`. */
    this.levels = null;
    this.fresh = true;

    /* prefers-reduced-motion disables the field OUTRIGHT, which the design
     * states as a rule rather than a preference: the Motion switch is a separate
     * control and cannot turn this back on. FOLLOWED, not read once -- a user
     * who turns it on mid-session means mid-session. */
    this._mq = globalThis.matchMedia?.('(prefers-reduced-motion: reduce)') || null;
    this.reduced = !!this._mq?.matches;
    this._onReduced = (e) => {
      this.reduced = !!e?.matches;
      if (this.reduced) this._flatten();
    };
    this._mq?.addEventListener?.('change', this._onReduced);

    const colours = readColours(o.colourFrom || document.documentElement);
    this.bgCss = cssHex(colours.bg);
    this.grain = makeGrain(colours.grain);
    this.dot = dotBase(colours.bg, colours.dot);

    this._tick = this._tick.bind(this);
    /* Told true when the field starts moving and false when it stops: the
     * plugin sends frame ticks in between. */
    this.onRunning = typeof opts.onRunning === 'function' ? opts.onRunning : null;
    this._fallbackTick = () => {
      if (now() - this.lastOutside > FALLBACK_AFTER_MS) this._tick();
    };
    this.resize();
  }

  /* The size and pixel ratio the canvas needs right now. The ratio includes the
   * editor's CSS scale (see lib/ground-geometry.js): a backing store sized by
   * devicePixelRatio alone is resampled by that scale on screen. */
  _measure() {
    const c = this.canvas;
    const k = viewScale(c.getBoundingClientRect?.(), c.clientWidth);
    return {
      w: c.clientWidth,
      h: c.clientHeight,
      ratio: backingRatio(globalThis.devicePixelRatio || 1, k, this.o.pitch),
    };
  }

  /**
   * Resize if, and only if, the canvas's size or pixel ratio has changed.
   * Returns whether it did. A resize resets the field, so a layout notification
   * that changed nothing must not cost a ring in flight.
   */
  fit() {
    const m = this._measure();
    if (m.w === this.w && m.h === this.h && m.ratio === this.dpr) return false;
    this.resize();
    return true;
  }

  resize() {
    const c = this.canvas;
    const m = this._measure();
    this.w = m.w;
    this.h = m.h;
    /* `dpr` is backing px per layout px -- the device ratio times the editor's
     * scale, snapped so a dot pitch is a whole number of pixels. */
    const dpr = (this.dpr = m.ratio);
    c.width = Math.round(this.w * dpr);
    c.height = Math.round(this.h * dpr);
    this.cols = Math.floor(this.w / this.o.pitch) + 1;
    this.rows = Math.floor(this.h / this.o.pitch) + 1;
    this._grid();
    this._bake();
    this.draw();
  }

  /**
   * The boxes that reflect and carry the emitters at their corners, in CSS px
   * relative to the canvas.
   * @param rects [{x, y, w, h}]
   */
  setSources(rects) {
    this.rects = rects || [];
    this._grid();
    /* The walls moved, so which cells are drawn moved with them. */
    this.fresh = true;
    this.draw();
  }

  /* A drop's footprint around (x, y): the open nodes within 3 sigma, as
   * [node, Gaussian weight] pairs, flat. Made once per emitter when the grid
   * is built, so a ring costs no search. */
  _footprint(x, y) {
    const o = this.o;
    const h = o.cell;
    const s = o.spot;
    const r = Math.ceil((3 * s) / h);
    const ci = Math.round(x / h);
    const cj = Math.round(y / h);
    const fp = [];
    for (let j = cj - r; j <= cj + r; j++) {
      for (let i = ci - r; i <= ci + r; i++) {
        if (i < 0 || j < 0 || i >= this.nx || j >= this.ny) continue;
        const q = j * this.nx + i;
        const dx = i * h - x;
        const dy = j * h - y;
        const d2 = dx * dx + dy * dy;
        if (!this.wall[q] && d2 <= 9 * s * s) fp.push(q, Math.exp(-d2 / (2 * s * s)));
      }
    }
    return fp;
  }

  /* The simulation grid: walls (inside a box, edges included) and the
   * emitters, the anchor first, then clockwise. Rebuilding this resets the
   * field and the bar's walk -- a ring mid-flight through a box that just
   * moved has nowhere consistent to be. */
  _grid() {
    const o = this.o;
    const h = o.cell;
    const nx = (this.nx = Math.floor(this.w / h) + 1);
    const ny = (this.ny = Math.floor(this.h / h) + 1);
    const n = nx * ny;
    this.u = new Float32Array(n);
    this.up = new Float32Array(n);
    this.un = new Float32Array(n);
    /* The scratch one step mixes; see `_step`. */
    this.mix = new Float32Array(n);
    const rects = this.rects;
    const wall = (this.wall = new Uint8Array(n));
    for (let j = 0; j < ny; j++) {
      for (let i = 0; i < nx; i++) {
        const x = i * h;
        const y = j * h;
        for (let r = 0; r < rects.length; r++) {
          const b = rects[r];
          if (x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h) {
            wall[j * nx + i] = 1;
            break;
          }
        }
      }
    }

    /* A corner's emitter stands where open ground surrounds it: off every
     * other box by `clear`, inside the window by `clear`, and not on top of an
     * emitter already standing. In a narrow gutter, then, there is none: a
     * drop there would be a clipped splash between two walls. */
    let em = [];
    const e = o.inset;
    for (let r = 0; r < rects.length; r++) {
      const b = rects[r];
      const corners = [
        [b.x - e, b.y - e], [b.x + b.w + e, b.y - e],
        [b.x + b.w + e, b.y + b.h + e], [b.x - e, b.y + b.h + e],
      ];
      for (const [px, py] of corners) {
        let ok = px >= o.clear && py >= o.clear && px <= this.w - o.clear && py <= this.h - o.clear;
        for (let s = 0; ok && s < rects.length; s++) {
          if (s !== r && toBox(px, py, rects[s]) < o.clear) ok = false;
        }
        for (let s = 0; ok && s < em.length; s++) {
          if (Math.hypot(px - em[s].x, py - em[s].y) < o.clear) ok = false;
        }
        if (ok) em.push({ x: px, y: py });
      }
    }
    const cx = this.w / 2;
    const cy = this.h / 2;
    if (!em.length && !rects.some((b) => toBox(cx, cy, b) < o.clear)) em.push({ x: cx, y: cy });

    /* The anchor is the emitter nearest the centre, ties in reading order, so
     * every bar starts from the same place; the others follow it clockwise
     * around the centre -- clockwise ON SCREEN, where y grows downwards, which
     * is atan2's increasing angle. */
    if (em.length) {
      const near = (p) => Math.hypot(p.x - cx, p.y - cy);
      const first = (p, q) => {
        const d = near(p) - near(q);
        return Math.abs(d) > 1e-6 ? d : p.y - q.y || p.x - q.x;
      };
      const at = em.reduce((best, p) => (first(p, best) < 0 ? p : best), em[0]);
      const from = Math.atan2(at.y - cy, at.x - cx);
      const turn = (p) => {
        const a = Math.atan2(p.y - cy, p.x - cx) - from;
        return a < 0 ? a + 2 * Math.PI : a;
      };
      em = [at].concat(em.filter((p) => p !== at).sort((p, q) => turn(p) - turn(q)));
    }
    for (const p of em) p.fp = this._footprint(p.x, p.y);
    this.emitters = em;
    this.rings = [];
    this.t = 0;
    /* Rings since the grid was built, and since the last downbeat. */
    this.count = 0;
    this.walk = 0;
  }

  /**
   * One ring. `s` is 0..1: the plugin sends 1 on a downbeat, 0.4 on any other
   * beat. A ring of full strength is a downbeat: it falls at the anchor and
   * starts the bar's walk; any other falls at the next emitter clockwise.
   * `at` ({x, y}, CSS px) drops it there instead, as a click on the design's
   * preview does.
   */
  trigger(s, at) {
    const em = this.emitters;
    if (this.reduced || !this.enabled || !em.length) return;
    s = clamp(s == null ? 1 : s, 0, 1);
    this.walk = s >= 1 ? 0 : this.walk + 1;
    const [vf, vs] = vary(this.count++);
    this.rings.push({
      t0: this.t,
      s: s * (1 + this.o.varyS * vs),
      f: this.o.freq * (1 + this.o.varyF * vf),
      fp: at ? this._footprint(at.x, at.y) : em[this.walk % em.length].fp,
    });
    this._start();
  }

  setEnabled(on) {
    this.enabled = !!on;
    if (!on) this._flatten();
  }

  /** Back to exactly the static ground, now: no wave, no pending ring, no loop. */
  _flatten() {
    this.u.fill(0);
    this.up.fill(0);
    this.rings = [];
    this._stop();
    this.draw();
  }

  /**
   * One frame, from outside: the plugin's frame tick. Steps the simulation by
   * the time that really passed since the last frame and draws. Ignored at
   * rest.
   */
  tick() {
    this.lastOutside = now();
    if (this.running) this._tick();
  }

  _start() {
    if (this.running) return;
    this.running = true;
    this.lastTick = now();
    this.acc = 0;
    this.fallback = globalThis.setInterval(this._fallbackTick, 1000 / this.o.fps);
    this.onRunning?.(true);
  }

  _stop() {
    if (!this.running) return;
    this.running = false;
    globalThis.clearInterval(this.fallback);
    this.fallback = 0;
    this.onRunning?.(false);
  }

  /* One explicit step: the damped, viscous wave equation, Neumann at the
   * walls, the border partly absorbing. */
  _step() {
    const o = this.o;
    const nx = this.nx;
    const ny = this.ny;
    const u = this.u;
    const up = this.up;
    const un = this.un;
    const mix = this.mix;
    const wall = this.wall;
    const dt = o.dt;
    const k = (o.speed * dt) / o.cell;
    const k2 = k * k;
    const nu = (o.visc * dt) / (o.cell * o.cell);
    const g = ((2 / o.tau) * dt) / 2;
    const a = 1 / (1 + g);
    const b = 1 - g;

    /* k2 lap(u) + nu lap(u - up) is lap of one mix -- the stencil and its
     * mirroring at the walls are linear -- so one pass covers both terms. */
    for (let q = 0; q < u.length; q++) mix[q] = (k2 + nu) * u[q] - nu * up[q];
    for (let j = 0; j < ny; j++) {
      const row = j * nx;
      for (let i = 0; i < nx; i++) {
        const q = row + i;
        if (wall[q]) {
          un[q] = 0;
          continue;
        }
        un[q] = (2 * u[q] - b * up[q] + lap(mix, wall, q, i, j, nx, ny)) * a;
      }
    }

    /* The border: an impedance edge, Mur's one-way update at the speed alpha
     * c, alpha = (1 - R) / (1 + R), which sends back R of a wave meeting it
     * head on (alpha = 1 lets it all leave, alpha -> 0 reflects it all). */
    const R = o.borderR;
    if (R < 1 && nx > 1 && ny > 1) {
      const ka = (k * (1 - R)) / (1 + R);
      const m = (ka - 1) / (ka + 1);
      const edge = (q, n) => {
        if (!wall[q] && !wall[n]) un[q] = u[n] + m * (un[n] - u[q]);
      };
      for (let j = 0; j < ny; j++) {
        edge(j * nx, j * nx + 1);
        edge(j * nx + nx - 1, j * nx + nx - 2);
      }
      for (let i = 0; i < nx; i++) {
        edge(i, i + nx);
        edge((ny - 1) * nx + i, (ny - 2) * nx + i);
      }
    }

    /* The drops: every ring still inside its wavelet, at its own emitter.
     * Summing rather than replacing is what makes two rings interfere instead
     * of the second cancelling the first. */
    const f0 = dt * dt * o.gain * a;
    const live = [];
    for (const ring of this.rings) {
      const age = this.t - ring.t0;
      if (age >= 2.5 / ring.f) continue;
      live.push(ring);
      const f = f0 * ring.s * ricker(age, ring.f);
      const fp = ring.fp;
      for (let p = 0; p < fp.length; p += 2) un[fp[p]] += f * fp[p + 1];
    }
    this.rings = live;
    /* Rotate the three buffers rather than allocating: `un` becomes the present,
     * the present becomes the past, and the old past is the next scratch. */
    this.up = u;
    this.u = un;
    this.un = up;
    this.t += dt;
  }

  _tick() {
    const o = this.o;
    const t = now();
    this.acc += Math.min(0.1, (t - this.lastTick) / 1000);
    this.lastTick = t;
    let n = 0;
    while (this.acc >= o.dt && n < o.maxSteps) {
      this._step();
      this.acc -= o.dt;
      n++;
    }
    /* After a long stall, drop the backlog instead of racing to catch up --
     * which would show as the field suddenly running fast. */
    if (this.acc > o.dt) this.acc = 0;
    const peak = this.draw();
    if (!this.rings.length && peak <= o.rest) {
      /* Rung out. Back to EXACTLY zero -- not nearly zero -- so every dot is
       * back at its resting sprite, and the clock stops. */
      this.u.fill(0);
      this.up.fill(0);
      this.draw();
      this._stop();
    }
  }

  _peak() {
    let m = 0;
    const u = this.u;
    for (let q = 0; q < u.length; q++) {
      const v = u[q] < 0 ? -u[q] : u[q];
      if (v > m) m = v;
    }
    return m;
  }

  /* What the dot at node (gi, gj) shows, before tanh: the height plus the
   * slope facing the light at the top left. A surface rising towards the
   * bottom right faces the top left: (d/dx + d/dy) / sqrt 2, times
   * lambda / 2 pi so that a wave's slope and its height are on the same
   * scale. A neighbour inside a wall reads as the node, as in the stencil. */
  _lit(gi, gj) {
    const o = this.o;
    const nx = this.nx;
    const ny = this.ny;
    const u = this.u;
    const wall = this.wall;
    const q = gj * nx + gi;
    const c = u[q];
    const l = gi > 0 && !wall[q - 1] ? u[q - 1] : c;
    const r = gi < nx - 1 && !wall[q + 1] ? u[q + 1] : c;
    const t = gj > 0 && !wall[q - nx] ? u[q - nx] : c;
    const d = gj < ny - 1 && !wall[q + nx] ? u[q + nx] : c;
    const towards = ((r - l + d - t) / (2 * o.cell)) * Math.SQRT1_2;
    return c + ((o.slope * (o.speed / o.freq)) / (2 * Math.PI)) * towards;
  }

  /*
   * THE SPRITE ATLAS is why this can run at 30 fps in a WebView. Every state a
   * cell can be in -- 8x8 grain positions times LEVELS wave values -- is
   * rendered once, up front. A frame then blits the baked ground and one sprite
   * per cell a wave is actually crossing, instead of stroking a few thousand
   * arcs and compositing a noise tile every time.
   */
  _bake() {
    if (!this.w) return;
    const o = this.o;
    const p = o.pitch;
    const dpr = this.dpr;
    const cw = (this.cw = Math.round(p * dpr));
    const A = this.atlas || (this.atlas = document.createElement('canvas'));
    A.width = 64 * cw;
    A.height = LEVELS * cw;
    const x = A.getContext('2d');
    for (let l = 0; l < LEVELS; l++) {
      const v = (l - MID) / MID;
      for (let g = 0; g < 64; g++) {
        const gi = g % 8;
        const gj = (g / 8) | 0;
        // cell-local CSS px, dot at (p/2, p/2)
        x.setTransform(dpr, 0, 0, dpr, g * cw, l * cw);
        x.globalAlpha = 1;
        x.fillStyle = this.bgCss;
        x.fillRect(0, 0, p, p);
        x.globalAlpha = clamp(o.grainBase * (1 + o.grainK * v), o.grainMin, o.grainMax);
        x.drawImage(this.grain, gi * p, gj * p, p, p, 0, 0, p, p);
        x.globalAlpha = clamp(this.dot.alpha * (1 + o.dotDA * v), 0, 1);
        x.fillStyle = this.dot.rgb;
        x.beginPath();
        x.arc(p / 2, p / 2, o.dotR + o.dotDR * v, 0, 2 * Math.PI);
        x.fill();
      }
    }
    const B = this.baked || (this.baked = document.createElement('canvas'));
    B.width = this.canvas.width;
    B.height = this.canvas.height;
    const y = B.getContext('2d');
    y.fillStyle = this.bgCss;
    y.fillRect(0, 0, B.width, B.height);
    for (let j = 0; j < this.rows; j++) {
      for (let i = 0; i < this.cols; i++) this._blit(y, i, j, MID);
    }
    this.levels = new Uint8Array(this.rows * this.cols);
    this.fresh = true;
  }

  _blit(x, i, j, level) {
    const cw = this.cw;
    const g = (j % 8) * 8 + (i % 8);
    const half = cw / 2;
    x.drawImage(
      this.atlas,
      g * cw, level * cw, cw, cw,
      Math.round(i * cw - half), Math.round(j * cw - half), cw, cw,
    );
  }

  /** Dot (i, j)'s sprite level, 0..LEVELS-1: lit, tanh soft-clipped. */
  _level(i, j) {
    const step = Math.round(this.o.pitch / this.o.cell);
    return Math.round((Math.tanh(this._lit(i * step, j * step)) + 1) * MID);
  }

  /**
   * Draw the dots whose level changed since the last frame. Returns the peak |u|.
   *
   * ONLY THE CHANGED ONES, which is the whole cost of the ground while music
   * plays. Every sprite covers its own cell exactly -- one pitch square, on
   * whole pixels, opaque -- so redrawing a cell replaces it completely, and a
   * cell that has not changed level is already correct on the canvas. A frame
   * used to copy the whole baked ground and then blit every moving dot again;
   * now a frame where the field is still is no drawing at all.
   *
   * `fresh` forces the full path: after a resize, a rebake or new walls, the
   * canvas is copied from the baked ground once and every level is taken as
   * MID.
   */
  draw() {
    if (!this.baked || !this.u) return 0;
    const x = this.ctx;
    const wall = this.wall;
    const nx = this.nx;
    const cols = this.cols;
    const levels = this.levels;
    const step = Math.round(this.o.pitch / this.o.cell);
    x.setTransform(1, 0, 0, 1, 0, 0);
    x.globalAlpha = 1;
    if (this.fresh) {
      x.drawImage(this.baked, 0, 0);
      levels.fill(MID);
      this.fresh = false;
    }
    for (let j = 0; j < this.rows; j++) {
      const gj = j * step;
      if (gj >= this.ny) break;
      for (let i = 0; i < cols; i++) {
        const gi = i * step;
        if (gi >= nx) break;
        if (wall[gj * nx + gi]) continue;
        const level = this._level(i, j);
        const d = j * cols + i;
        if (level !== levels[d]) {
          this._blit(x, i, j, level);
          levels[d] = level;
        }
      }
    }
    return this._peak();
  }

  destroy() {
    this._stop();
    this.rings = [];
    this._mq?.removeEventListener?.('change', this._onReduced);
  }
}

/* Exported for the tests, which assert against the design's table rather than
 * against whatever this file happens to say. */
export { DEFAULTS, LEVELS, MID, VERSION };

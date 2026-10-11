/* Ultraviolet Ground 1.3.0 — the dot-paper ground as a wave field that keeps the host's musical time.
 * Reference implementation, no dependencies. Two parts:
 *   UVGround.Field      simulates and renders the ground (dots + grain) into a <canvas>
 *   UVGround.BeatClock  turns the host's transport into rings: one on every quarter note while it
 *                       plays, a stronger one on each bar's downbeat, none while it is stopped
 *
 * Model: the damped 2D wave equation  u_tt = c²∇²u − γ·u_t + ν·∇²u_t + A·s·ψ(t − t₀)·G(x − e)
 * on a 6 px grid (every second node is a dot). A ring is a drop: ψ a Ricker wavelet (peak f₀, so
 * one slow wave of wavelength c/f₀), G a small Gaussian around one emitter e, s its strength, so
 * every ring is a circle that spreads, thins with distance and meets the boxes.
 *
 * Emitters sit just off the outer corners of the boxes, where a box leaves open ground around
 * them (none in a gutter narrower than `clear`); with no box, the window centre is one. The one
 * nearest the window centre is the anchor: every downbeat falls there, so each bar starts from the
 * same place, and the bar's other beats walk the other emitters clockwise from it. Each ring varies
 * a little in frequency and strength, the same way every time (a hash of the ring's count).
 *
 * Boxes reflect totally (Neumann, no phase flip), so rings echo between the boxes and bend around
 * their corners; the window border reflects only `borderR` of what reaches it (an impedance edge),
 * so the window never fills with its own standing waves. γ = 2/τ fades
 * every wave alike; ν fades a wave by its wavenumber squared, so ripple at the grid's scale dies at
 * once while the long wave softens as it goes and rings out slowly. ∇² is the isotropic 9-point
 * stencil, so a ring stays round on the square grid.
 *
 * Display: each dot shows tanh(u + slope·(λ/2π)·∂u/∂ℓ), the height plus the slope facing a light
 * at the top left, so a crest lights on its side towards the light and shades on the other.
 * Up (towards the light): bigger, brighter dots and more grain; down: smaller, dimmer, less grain.
 * At rest u = 0 everywhere and the ground is exactly the static design.
 *
 * Clock: the field steps on a timer at its frame rate, never on requestAnimationFrame, and page
 * visibility does not pause it: plugin hosts report their editor pages as hidden while they are on
 * screen. The timer runs only while the field moves; destroy() stops it, which the host does when
 * the window closes.
 */
(function (root) {
  'use strict';

  var DEFAULTS = {
    pitch: 12,          // dot pitch, px (space-3)
    cell: 6,            // simulation grid, px (half a pitch)
    speed: 100,         // c, px/s
    freq: 1.5,          // f₀ of the source wavelet, Hz → wavelength c/f₀ ≈ 67 px; a ring on every beat (90–140 BPM) does not cancel the last
    tau: 10,            // amplitude e-folding time, s (γ = 2/τ); with the border and the spreading, ~10 s from the last ring to rest
    visc: 22,           // ν, px²/s: fades a wave by its wavenumber squared; grid-scale ripple dies within a second, the ring lives on
    gain: 220,          // source strength A: a downbeat lifts the dots ~300 px from its emitter, soft-clipped at the drop
    spot: 16,           // σ of a drop's Gaussian footprint, px: wide enough that the drop itself is a soft splash, not a clipped block
    inset: 9,           // an emitter sits this far off its box's corner, along both edges, px
    clear: 18,          // no other box within this of an emitter, px: a narrower gutter has none
    borderR: 0.5,       // share of a wave the window border reflects head on: 0 lets it leave, 1 reflects it all
    varyF: 0.08,        // ± relative frequency from ring to ring
    varyS: 0.12,        // ± relative strength from ring to ring
    slope: 0.7,         // display: weight of the slope facing the light (top left) against the height
    dt: 1 / 60,         // fixed simulation step, s
    maxSteps: 6,        // per frame, after a stall
    rest: 0.004,        // below this everywhere (and no ring active) the field is reset and the timer stops
    dotR: 1.0,          // dot radius at rest, px
    dotDR: 0.4,         // ± radius at |v| = 1
    dotDA: 0.3,         // ± relative dot opacity at |v| = 1
    grainBase: 0.5,     // grain layer opacity at rest (tile mean 10% → 5% effective)
    grainK: 0.8,        // grain gain per unit v
    grainMin: 0.2,      // → 2% effective in the deepest valley
    grainMax: 0.9,      // → 9% effective on the highest peak
    fps: 30,            // the timer's rate while the field moves; 0 at rest
    bg: [6, 4, 16],          // bg-000
    dot: [42, 30, 74],       // bg-dot
    grain: [162, 89, 255]    // uv-deep
  };

  function clamp(v, a, b) { return v < a ? a : v > b ? b : v; }
  function now() { return (root.performance || Date).now(); }

  // uniform white noise tile, 96×96 (a multiple of the pitch), alpha 0–20% → mean 10%
  function makeGrain(rgb) {
    var n = 96, c = document.createElement('canvas'); c.width = c.height = n;
    var x = c.getContext('2d'), img = x.createImageData(n, n), d = img.data;
    for (var i = 0; i < d.length; i += 4) {
      d[i] = rgb[0]; d[i + 1] = rgb[1]; d[i + 2] = rgb[2];
      d[i + 3] = Math.floor(Math.random() * 52);
    }
    x.putImageData(img, 0, 0);
    return c;
  }

  // dot fill that, at 77% opacity over bg, reproduces bg-dot exactly; peaks reach 100%
  function dotBase(o) {
    var a = 0.77, s = [];
    for (var i = 0; i < 3; i++) s.push(Math.round((o.dot[i] - o.bg[i] * (1 - a)) / a));
    return { rgb: 'rgb(' + s.join(',') + ')', alpha: a };
  }

  function ricker(t, f) {
    var a = Math.PI * f * (t - 1 / f); a *= a;
    return (1 - 2 * a) * Math.exp(-a);
  }

  // a ring's count → two numbers in −1..1, the same every time (a 32-bit integer mix)
  function vary(n) {
    var h = n >>> 0;
    h = Math.imul(h ^ (h >>> 16), 0x45d9f3b);
    h = Math.imul(h ^ (h >>> 16), 0x45d9f3b);
    h = (h ^ (h >>> 16)) >>> 0;
    return [(h & 0xffff) / 32767.5 - 1, (h >>> 16) / 32767.5 - 1];
  }

  // distance from (x, y) to a box, 0 inside it
  function toBox(x, y, b) {
    var dx = Math.max(b.x - x, 0, x - b.x - b.w), dy = Math.max(b.y - y, 0, y - b.y - b.h);
    return Math.sqrt(dx * dx + dy * dy);
  }

  function Field(canvas, opts) {
    var o = this.o = {};
    for (var k in DEFAULTS) o[k] = DEFAULTS[k];
    for (k in (opts || {})) o[k] = opts[k];
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');
    this.rings = [];
    this.rects = [];
    this.enabled = true;
    this.timer = 0; this.lastTick = 0; this.acc = 0; this.t = 0;
    var mq = root.matchMedia && root.matchMedia('(prefers-reduced-motion: reduce)');
    this.reduced = !!(mq && mq.matches);
    this.grain = makeGrain(o.grain);
    this.dot = dotBase(o);
    this._tick = this._tick.bind(this);
    this.resize();
  }

  Field.prototype.resize = function () {
    var c = this.canvas, dpr = root.devicePixelRatio || 1;
    this.w = c.clientWidth; this.h = c.clientHeight; this.dpr = dpr;
    c.width = Math.round(this.w * dpr); c.height = Math.round(this.h * dpr);
    this.cols = Math.floor(this.w / this.o.pitch) + 1;
    this.rows = Math.floor(this.h / this.o.pitch) + 1;
    this._grid();
    this._bake();
    this.draw();
  };

  // rects: [{x, y, w, h}] in CSS px relative to the canvas — the boxes that reflect and carry the emitters
  Field.prototype.setSources = function (rects) {
    this.rects = rects || [];
    this._grid();
    this.draw();
  };

  // a drop's footprint around (x, y): the open nodes within 3σ and their Gaussian weights
  Field.prototype._footprint = function (x, y) {
    var o = this.o, h = o.cell, s = o.spot, r = Math.ceil(3 * s / h), fp = [];
    var ci = Math.round(x / h), cj = Math.round(y / h);
    for (var j = cj - r; j <= cj + r; j++) for (var i = ci - r; i <= ci + r; i++) {
      if (i < 0 || j < 0 || i >= this.nx || j >= this.ny) continue;
      var q = j * this.nx + i, dx = i * h - x, dy = j * h - y, d2 = dx * dx + dy * dy;
      if (!this.wall[q] && d2 <= 9 * s * s) fp.push(q, Math.exp(-d2 / (2 * s * s)));
    }
    return fp;
  };

  // simulation grid: walls (inside a box), the emitters (anchor first, then clockwise)
  Field.prototype._grid = function () {
    var o = this.o, h = o.cell, nx = this.nx = Math.floor(this.w / h) + 1, ny = this.ny = Math.floor(this.h / h) + 1, n = nx * ny;
    this.u = new Float32Array(n); this.up = new Float32Array(n); this.un = new Float32Array(n);
    var wall = this.wall = new Uint8Array(n), rects = this.rects, i, j, r;
    for (j = 0; j < ny; j++) for (i = 0; i < nx; i++) {
      var x = i * h, y = j * h;
      for (r = 0; r < rects.length; r++) {
        var b = rects[r];
        if (x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h) { wall[j * nx + i] = 1; break; }
      }
    }

    // a corner's emitter stands where open ground surrounds it: off every other box by `clear`,
    // inside the window by `clear`, and not on top of an emitter already standing
    var em = [], e = o.inset;
    for (r = 0; r < rects.length; r++) {
      b = rects[r];
      var corners = [[b.x - e, b.y - e], [b.x + b.w + e, b.y - e], [b.x + b.w + e, b.y + b.h + e], [b.x - e, b.y + b.h + e]];
      for (var c = 0; c < 4; c++) {
        var px = corners[c][0], py = corners[c][1], ok = px >= o.clear && py >= o.clear && px <= this.w - o.clear && py <= this.h - o.clear;
        for (var s = 0; ok && s < rects.length; s++) if (s !== r && toBox(px, py, rects[s]) < o.clear) ok = false;
        for (s = 0; ok && s < em.length; s++) if (Math.hypot(px - em[s].x, py - em[s].y) < o.clear) ok = false;
        if (ok) em.push({ x: px, y: py });
      }
    }
    var cx = this.w / 2, cy = this.h / 2;
    if (!em.length && !rects.some(function (b) { return toBox(cx, cy, b) < o.clear; })) em.push({ x: cx, y: cy });

    // the anchor is the emitter nearest the centre; the others follow it clockwise around the centre
    if (em.length) {
      var near = function (p) { return Math.hypot(p.x - cx, p.y - cy); };
      var first = function (p, q) { var d = near(p) - near(q); return Math.abs(d) > 1e-6 ? d : p.y - q.y || p.x - q.x; };   // ties: reading order
      var at = em.reduce(function (best, p) { return first(p, best) < 0 ? p : best; }, em[0]);
      var turn = function (p) { var a = Math.atan2(p.y - cy, p.x - cx) - Math.atan2(at.y - cy, at.x - cx); return a < 0 ? a + 2 * Math.PI : a; };
      em = [at].concat(em.filter(function (p) { return p !== at; }).sort(function (p, q) { return turn(p) - turn(q); }));
    }
    for (r = 0; r < em.length; r++) em[r].fp = this._footprint(em[r].x, em[r].y);
    this.emitters = em;
    this.rings = []; this.t = 0; this.count = 0; this.walk = 0;
  };

  // One ring; s in 0..1 (1 on a downbeat, about half on any other beat). A downbeat falls at the
  // anchor and starts the bar's walk; every other beat falls at the next emitter. `at` ({x, y})
  // drops it there instead, as a click on the preview does.
  Field.prototype.trigger = function (s, at) {
    var em = this.emitters;
    if (this.reduced || !this.enabled || !em.length) return;
    s = clamp(s == null ? 1 : s, 0, 1);
    this.walk = s >= 1 ? 0 : this.walk + 1;
    var v = vary(this.count++);
    this.rings.push({
      t0: this.t,
      s: s * (1 + this.o.varyS * v[1]),
      f: this.o.freq * (1 + this.o.varyF * v[0]),
      fp: at ? this._footprint(at.x, at.y) : em[this.walk % em.length].fp
    });
    this._start();
  };

  Field.prototype.setEnabled = function (on) {
    this.enabled = !!on;
    if (!on) { this._stop(); this.u.fill(0); this.up.fill(0); this.rings = []; this.draw(); }
  };

  // a timer at the frame rate: animation frames and page visibility have no say in it
  Field.prototype._start = function () {
    if (!this.timer) { this.lastTick = now(); this.acc = 0; this.timer = root.setInterval(this._tick, 1000 / this.o.fps); }
  };

  Field.prototype._stop = function () {
    if (this.timer) root.clearInterval(this.timer);
    this.timer = 0;
  };

  // h²∇² at node q of field v, the isotropic 9-point stencil; a neighbour inside a wall or off the
  // grid reads as the node itself (Neumann)
  function lap(v, wall, q, i, j, nx, ny) {
    var c = v[q], L = i > 0, R = i < nx - 1, T = j > 0, B = j < ny - 1;
    var l = L && !wall[q - 1] ? v[q - 1] : c, r = R && !wall[q + 1] ? v[q + 1] : c;
    var t = T && !wall[q - nx] ? v[q - nx] : c, d = B && !wall[q + nx] ? v[q + nx] : c;
    var tl = L && T && !wall[q - nx - 1] ? v[q - nx - 1] : c, tr = R && T && !wall[q - nx + 1] ? v[q - nx + 1] : c;
    var bl = L && B && !wall[q + nx - 1] ? v[q + nx - 1] : c, br = R && B && !wall[q + nx + 1] ? v[q + nx + 1] : c;
    return (4 * (l + r + t + d) + tl + tr + bl + br - 20 * c) / 6;
  }

  // one explicit step: the damped, viscous wave equation, Neumann at the walls, the border partly absorbing
  Field.prototype._step = function () {
    var o = this.o, nx = this.nx, ny = this.ny, u = this.u, up = this.up, un = this.un, wall = this.wall;
    var dt = o.dt, k = o.speed * dt / o.cell, k2 = k * k, nu = o.visc * dt / (o.cell * o.cell);
    var g = (2 / o.tau) * dt / 2, a = 1 / (1 + g), b = 1 - g, i, j, q;
    for (j = 0; j < ny; j++) {
      var row = j * nx;
      for (i = 0; i < nx; i++) {
        q = row + i;
        if (wall[q]) { un[q] = 0; continue; }
        var lu = lap(u, wall, q, i, j, nx, ny), lp = lap(up, wall, q, i, j, nx, ny);
        un[q] = (2 * u[q] - b * up[q] + k2 * lu + nu * (lu - lp)) * a;
      }
    }

    // the border: an impedance edge, Mur's one-way update at the speed αc, α = (1 − R)/(1 + R), which
    // sends back R of a wave meeting it head on (α = 1 lets it all leave, α → 0 reflects it all)
    var R = o.borderR, ka = k * (1 - R) / (1 + R), m = (ka - 1) / (ka + 1);
    if (R < 1) {
      var edge = function (q, n) { if (!wall[q] && !wall[n]) un[q] = u[n] + m * (un[n] - u[q]); };
      for (j = 0; j < ny; j++) { edge(j * nx, j * nx + 1); edge(j * nx + nx - 1, j * nx + nx - 2); }
      for (i = 0; i < nx; i++) { edge(i, i + nx); edge((ny - 1) * nx + i, (ny - 2) * nx + i); }
    }

    // the drops: every ring still inside its wavelet, at its own emitter
    var live = [], f0 = dt * dt * o.gain * a;
    for (var r = 0; r < this.rings.length; r++) {
      var ring = this.rings[r], age = this.t - ring.t0;
      if (age >= 2.5 / ring.f) continue;
      live.push(ring);
      var f = f0 * ring.s * ricker(age, ring.f), fp = ring.fp;
      for (var p = 0; p < fp.length; p += 2) un[fp[p]] += f * fp[p + 1];
    }
    this.rings = live;
    this.up = u; this.u = un; this.un = up;
    this.t += dt;
  };

  // one frame: catch the simulation up to the clock, draw, and stop once the field has rung out
  Field.prototype._tick = function () {
    var o = this.o, t = now();
    this.acc += Math.min(0.1, (t - this.lastTick) / 1000); this.lastTick = t;
    var n = 0;
    while (this.acc >= o.dt && n < o.maxSteps) { this._step(); this.acc -= o.dt; n++; }
    if (this.acc > o.dt) this.acc = 0;   // after a long stall, drop the backlog instead of racing
    var peak = this.draw();
    if (!this.rings.length && peak <= o.rest) {   // rung out: back to the exact static ground, timer stops
      this._stop(); this.u.fill(0); this.up.fill(0); this.draw();
    }
  };

  Field.prototype._peak = function () {
    var m = 0, u = this.u;
    for (var q = 0; q < u.length; q++) { var v = u[q] < 0 ? -u[q] : u[q]; if (v > m) m = v; }
    return m;
  };

  // what a dot shows: the height plus the slope facing the light at the top left, before tanh
  Field.prototype._lit = function (gi, gj) {
    var o = this.o, nx = this.nx, ny = this.ny, u = this.u, wall = this.wall, q = gj * nx + gi, c = u[q];
    var l = gi > 0 && !wall[q - 1] ? u[q - 1] : c, r = gi < nx - 1 && !wall[q + 1] ? u[q + 1] : c;
    var t = gj > 0 && !wall[q - nx] ? u[q - nx] : c, d = gj < ny - 1 && !wall[q + nx] ? u[q + nx] : c;
    // a surface rising towards the bottom right faces the top left: (∂x + ∂y)/√2, times λ/2π so
    // a wave's slope and its height have the same scale
    var dl = (r - l + d - t) / (2 * o.cell) * Math.SQRT1_2;
    return c + o.slope * (o.speed / o.freq) / (2 * Math.PI) * dl;
  };

  // Sprite atlas: every cell state rendered once — 8×8 grain positions × LEVELS wave values.
  // A frame blits the baked ground, then one atlas sprite per cell a wave is crossing.
  var LEVELS = 25, MID = (LEVELS - 1) / 2;   // level MID is v = 0, the ground at rest

  Field.prototype._bake = function () {
    if (!this.w) return;
    var o = this.o, p = o.pitch, dpr = this.dpr, cw = this.cw = Math.round(p * dpr);
    var A = this.atlas || (this.atlas = document.createElement('canvas'));
    A.width = 64 * cw; A.height = LEVELS * cw;
    var x = A.getContext('2d'), bg = 'rgb(' + o.bg.join(',') + ')';
    for (var l = 0; l < LEVELS; l++) {
      var v = (l - MID) / MID;
      for (var g = 0; g < 64; g++) {
        var gi = g % 8, gj = (g / 8) | 0;
        x.setTransform(dpr, 0, 0, dpr, g * cw, l * cw);          // cell-local CSS px, dot at (p/2, p/2)
        x.globalAlpha = 1; x.fillStyle = bg; x.fillRect(0, 0, p, p);
        x.globalAlpha = clamp(o.grainBase * (1 + o.grainK * v), o.grainMin, o.grainMax);
        x.drawImage(this.grain, gi * p, gj * p, p, p, 0, 0, p, p);
        x.globalAlpha = clamp(this.dot.alpha * (1 + o.dotDA * v), 0, 1);
        x.fillStyle = this.dot.rgb;
        x.beginPath(); x.arc(p / 2, p / 2, o.dotR + o.dotDR * v, 0, 2 * Math.PI); x.fill();
      }
    }
    var B = this.baked || (this.baked = document.createElement('canvas'));
    B.width = this.canvas.width; B.height = this.canvas.height;
    var y = B.getContext('2d');
    y.fillStyle = bg; y.fillRect(0, 0, B.width, B.height);
    for (var j = 0; j < this.rows; j++) for (var i = 0; i < this.cols; i++) this._blit(y, i, j, MID);
  };

  Field.prototype._blit = function (x, i, j, level) {
    var cw = this.cw, g = (j % 8) * 8 + (i % 8), half = cw / 2;
    x.drawImage(this.atlas, g * cw, level * cw, cw, cw,
      Math.round(i * cw - half), Math.round(j * cw - half), cw, cw);
  };

  // blit the baked ground, then one atlas sprite per dot the field has moved; returns the peak |u|
  Field.prototype.draw = function () {
    if (!this.baked || !this.u) return 0;
    var x = this.ctx, wall = this.wall, nx = this.nx, step = Math.round(this.o.pitch / this.o.cell);
    x.setTransform(1, 0, 0, 1, 0, 0);
    x.globalAlpha = 1;
    x.drawImage(this.baked, 0, 0);
    for (var j = 0; j < this.rows; j++) {
      var gj = j * step; if (gj >= this.ny) break;
      for (var i = 0; i < this.cols; i++) {
        var gi = i * step; if (gi >= nx) break;
        if (wall[gj * nx + gi]) continue;
        var level = Math.round((Math.tanh(this._lit(gi, gj)) + 1) * MID);
        if (level !== MID) this._blit(x, i, j, level);
      }
    }
    return this._peak();
  };

  Field.prototype.destroy = function () {
    this._stop();
    this.rings = [];
  };

  /* ---------- beat clock ---------- */
  var BEAT = {
    downbeat: 1,          // a bar's first beat: the field's full source strength
    beat: 0.55,           // every other quarter note: about half (a plugin may tune it)
    fps: 30,              // ticks per second: one tick's travel is how far past a beat a start may land and still ring it
    slack: 1 / 16         // quarter notes: a position this far behind the last tick is jitter, not a jump
  };

  // quarter notes in a bar: numerator × 4 ÷ denominator; 4/4 when the host gives no time signature
  function barQuarters(num, den) { return num > 0 && den > 0 ? num * 4 / den : 4; }

  // trigger(strength) is called once per ring — pass field.trigger.bind(field)
  function BeatClock(trigger, opts) {
    var o = this.o = {};
    for (var k in BEAT) o[k] = BEAT[k];
    for (k in (opts || {})) o[k] = opts[k];
    this.trigger = trigger;
    this.last = null;     // the position at the last playing tick, in quarter notes; null while stopped
  }

  // forget the last tick: the next playing tick is a start
  BeatClock.prototype.reset = function () { this.last = null; };

  // One tick at the frame rate: the host's position in quarter notes, its tempo (quarter notes per
  // minute), its time signature and whether the transport plays. Rings every quarter note and every
  // bar start crossed since the last tick; returns how many.
  BeatClock.prototype.tick = function (pos, bpm, num, den, playing) {
    var o = this.o;
    if (!playing || !isFinite(pos)) { this.last = null; return 0; }
    var step = (isFinite(bpm) && bpm > 0 ? bpm : 120) / 60 / o.fps;   // quarter notes one tick covers
    var last = this.last;
    var jump = last === null || pos < last - o.slack || pos > last + 4 * step + o.slack;
    this.last = pos;
    // a start, a loop or a seek rings only what its own tick holds: the beat it lands on, never the ones it skipped
    var lo = jump ? pos - step : last;
    if (pos <= lo) return 0;
    var bar = barQuarters(num, den), q = Math.floor(lo) + 1, d = Math.ceil(lo / bar) * bar, rings = [];
    if (d <= lo) d += bar;
    while (Math.min(q, d) <= pos && rings.length < 16) {
      if (Math.abs(q - d) < 1e-9) { rings.push(o.downbeat); q += 1; d += bar; }   // a downbeat on a quarter is one ring
      else if (d < q) { rings.push(o.downbeat); d += bar; }                      // 7/8: a downbeat between two quarters
      else { rings.push(o.beat); q += 1; }
    }
    if (jump) rings = rings.slice(-1);
    for (var i = 0; i < rings.length; i++) this.trigger(rings[i]);
    return rings.length;
  };

  root.UVGround = { version: '1.3.0', Field: Field, BeatClock: BeatClock, defaults: DEFAULTS, beatDefaults: BEAT };
})(window);

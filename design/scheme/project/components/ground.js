/* Ultraviolet Ground 1.1.0 — the dot-paper ground as a wave field that keeps the host's musical time.
 * Reference implementation, no dependencies. Two parts:
 *   UVGround.Field      simulates and renders the ground (dots + grain) into a <canvas>
 *   UVGround.BeatClock  turns the host's transport into rings: one on every quarter note while it
 *                       plays, a stronger one on each bar's downbeat, none while it is stopped
 *
 * Model: the damped 2D wave equation  u_tt = c²∇²u − γ·u_t + A·s·ψ(t − t₀)·S(x)
 * on a 6 px grid (every second node is a dot). S marks the sources: every open node next to a
 * box edge and every node on the window border. ψ is a Ricker wavelet (peak f₀), so each ring
 * is one slow wave of wavelength c/f₀, s its strength. Box edges and the window border reflect
 * (Neumann, no phase flip), waves superpose linearly, so they interfere, and γ = 2/τ lets the whole
 * field, reflections included, ring out over ~20 s. The field is soft-clipped with tanh for display.
 * Peaks (u > 0): bigger, brighter dots and more grain. Valleys (u < 0): smaller, dimmer dots, less grain.
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
    tau: 3.5,           // amplitude e-folding time, s (γ = 2/τ); the field rings out in ~20 s
    gain: 65,           // source strength A: one downbeat peaks near u ≈ 0.9
    border: true,       // the window border emits as well as reflects
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

  // rects: [{x, y, w, h}] in CSS px relative to the canvas — the boxes that emit and reflect
  Field.prototype.setSources = function (rects) {
    this.rects = rects || [];
    this._grid();
    this.draw();
  };

  // simulation grid: walls (inside a box), sources (open, next to a wall or on the border)
  Field.prototype._grid = function () {
    var o = this.o, h = o.cell, nx = this.nx = Math.floor(this.w / h) + 1, ny = this.ny = Math.floor(this.h / h) + 1, n = nx * ny;
    this.u = new Float32Array(n); this.up = new Float32Array(n); this.un = new Float32Array(n);
    var wall = this.wall = new Uint8Array(n), i, j, r;
    for (j = 0; j < ny; j++) for (i = 0; i < nx; i++) {
      var x = i * h, y = j * h;
      for (r = 0; r < this.rects.length; r++) {
        var b = this.rects[r];
        if (x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h) { wall[j * nx + i] = 1; break; }
      }
    }
    var src = [];
    for (j = 0; j < ny; j++) for (i = 0; i < nx; i++) {
      var q = j * nx + i;
      if (wall[q]) continue;
      var edge = i === 0 || j === 0 || i === nx - 1 || j === ny - 1;
      var near = (i > 0 && wall[q - 1]) || (i < nx - 1 && wall[q + 1]) || (j > 0 && wall[q - nx]) || (j < ny - 1 && wall[q + nx]);
      if (near || (o.border && edge)) src.push(q);
    }
    this.src = new Int32Array(src);
    this.rings = []; this.t = 0;
  };

  // one ring; s in 0..1 (1 on a downbeat, about half on any other beat)
  Field.prototype.trigger = function (s) {
    if (this.reduced || !this.enabled || !this.src.length) return;
    this.rings.push({ t0: this.t, s: clamp(s == null ? 1 : s, 0, 1) });
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

  // one explicit step of the damped wave equation, Neumann at walls and at the border
  Field.prototype._step = function () {
    var o = this.o, nx = this.nx, ny = this.ny, u = this.u, up = this.up, un = this.un, wall = this.wall;
    var dt = o.dt, k2 = (o.speed * dt / o.cell) * (o.speed * dt / o.cell), g = (2 / o.tau) * dt / 2;
    var a = 1 / (1 + g), b = 1 - g;
    for (var j = 0; j < ny; j++) {
      var row = j * nx;
      for (var i = 0; i < nx; i++) {
        var q = row + i;
        if (wall[q]) { un[q] = 0; continue; }
        var c = u[q];
        var l = (i > 0 && !wall[q - 1]) ? u[q - 1] : c;
        var r = (i < nx - 1 && !wall[q + 1]) ? u[q + 1] : c;
        var t = (j > 0 && !wall[q - nx]) ? u[q - nx] : c;
        var d = (j < ny - 1 && !wall[q + nx]) ? u[q + nx] : c;
        un[q] = (2 * c - b * up[q] + k2 * (l + r + t + d - 4 * c)) * a;
      }
    }
    // sources: the sum of every active ring's wavelet
    var F = 0, live = [], span = 2.5 / o.freq;
    for (var k = 0; k < this.rings.length; k++) {
      var age = this.t - this.rings[k].t0;
      if (age < span) { F += this.rings[k].s * ricker(age, o.freq); live.push(this.rings[k]); }
    }
    this.rings = live;
    if (F !== 0) {
      var f = dt * dt * o.gain * F * a, S = this.src;
      for (k = 0; k < S.length; k++) un[S[k]] += f;
    }
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
    var x = this.ctx, u = this.u, wall = this.wall, nx = this.nx, step = Math.round(this.o.pitch / this.o.cell);
    x.setTransform(1, 0, 0, 1, 0, 0);
    x.globalAlpha = 1;
    x.drawImage(this.baked, 0, 0);
    for (var j = 0; j < this.rows; j++) {
      var gj = j * step; if (gj >= this.ny) break;
      for (var i = 0; i < this.cols; i++) {
        var gi = i * step; if (gi >= nx) break;
        var q = gj * nx + gi;
        if (wall[q]) continue;
        var level = Math.round((Math.tanh(u[q]) + 1) * MID);
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

  root.UVGround = { version: '1.1.0', Field: Field, BeatClock: BeatClock, defaults: DEFAULTS, beatDefaults: BEAT };
})(window);

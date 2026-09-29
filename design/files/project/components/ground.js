/* Ultraviolet Ground 1.0.0 — bass-triggered wave field on the dot-paper ground.
 * Reference implementation, no dependencies. Two parts:
 *   UVGround.Field        simulates and renders the ground (dots + grain) into a <canvas>
 *   UVGround.BassDetector turns a Web Audio signal into kick onsets (20–80 Hz)
 *
 * Model: the damped 2D wave equation  u_tt = c²∇²u − γ·u_t + A·s·ψ(t − t₀)·S(x)
 * on a 6 px grid (every second node is a dot). S marks the sources: every open node next to a
 * box edge and every node on the window border. ψ is a Ricker wavelet (peak f₀), so each kick
 * emits one slow ring of wavelength c/f₀. Box edges and the window border reflect (Neumann, no
 * phase flip), waves superpose linearly, so they interfere, and γ = 2/τ lets the whole field,
 * reflections included, ring out over ~14 s. The field is soft-clipped with tanh for display.
 * Peaks (u > 0): bigger, brighter dots and more grain. Valleys (u < 0): smaller, dimmer dots, less grain.
 */
(function (root) {
  'use strict';

  var DEFAULTS = {
    pitch: 12,          // dot pitch, px (space-3)
    cell: 6,            // simulation grid, px (half a pitch)
    speed: 100,         // c, px/s
    freq: 1.5,          // f₀ of the source wavelet, Hz → wavelength c/f₀ ≈ 67 px; keeps a steady kick from cancelling itself (90–140 BPM)
    tau: 3.5,           // amplitude e-folding time, s (γ = 2/τ); a kick rings out in ~14 s
    gain: 65,           // source strength A: one kick peaks near u ≈ 0.9
    border: true,       // the window border emits as well as reflects
    dt: 1 / 60,         // fixed simulation step, s
    maxSteps: 6,        // per frame, after a stall
    rest: 0.004,        // below this everywhere (and no source active) the field is reset and the loop stops
    dotR: 1.0,          // dot radius at rest, px
    dotDR: 0.4,         // ± radius at |v| = 1
    dotDA: 0.3,         // ± relative dot opacity at |v| = 1
    grainBase: 0.5,     // grain layer opacity at rest (tile mean 10% → 5% effective)
    grainK: 0.8,        // grain gain per unit v
    grainMin: 0.2,      // → 2% effective in the deepest valley
    grainMax: 0.9,      // → 9% effective on the highest peak
    fps: 30,
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
    this.kicks = [];
    this.rects = [];
    this.enabled = true;
    this.raf = 0; this.lastFrame = 0; this.lastTick = 0; this.acc = 0; this.t = 0;
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
    this.kicks = []; this.t = 0;
  };

  // one kick; s in 0..1
  Field.prototype.trigger = function (s) {
    if (this.reduced || !this.enabled || !this.src.length) return;
    this.kicks.push({ t0: this.t, s: clamp(s == null ? 1 : s, 0, 1) });
    this._start();
  };

  Field.prototype.setEnabled = function (on) {
    this.enabled = !!on;
    if (!on) { this.u.fill(0); this.up.fill(0); this.kicks = []; this.draw(); }
  };

  Field.prototype._start = function () {
    if (!this.raf) { this.lastTick = now(); this.raf = root.requestAnimationFrame(this._tick); }
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
    // sources: the sum of every active kick's wavelet
    var F = 0, live = [], span = 2.5 / o.freq;
    for (var k = 0; k < this.kicks.length; k++) {
      var age = this.t - this.kicks[k].t0;
      if (age < span) { F += this.kicks[k].s * ricker(age, o.freq); live.push(this.kicks[k]); }
    }
    this.kicks = live;
    if (F !== 0) {
      var f = dt * dt * o.gain * F * a, S = this.src;
      for (k = 0; k < S.length; k++) un[S[k]] += f;
    }
    this.up = u; this.u = un; this.un = up;
    this.t += dt;
  };

  Field.prototype._tick = function () {
    this.raf = 0;
    var o = this.o, t = now();
    this.acc += Math.min(0.1, (t - this.lastTick) / 1000); this.lastTick = t;
    var n = 0;
    while (this.acc >= o.dt && n < o.maxSteps) { this._step(); this.acc -= o.dt; n++; }
    if (this.acc > o.dt) this.acc = 0;   // after a long stall, drop the backlog instead of racing
    var peak;
    if (t - this.lastFrame >= 1000 / o.fps - 2) { this.lastFrame = t; peak = this.draw(); }
    else peak = this._peak();
    if (this.kicks.length || peak > o.rest) this._start();
    else { this.u.fill(0); this.up.fill(0); this.draw(); }   // rung out: back to the exact static ground, loop stops
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
    if (this.raf) root.cancelAnimationFrame(this.raf);
    this.raf = 0; this.kicks = [];
  };

  /* ---------- bass onset detector ---------- */
  var BASS = {
    lo: 20, hi: 80,       // band, Hz — two 12 dB/oct Butterworth sections
    attack: 5,            // envelope attack, ms
    release: 150,         // envelope release, ms
    window: 300,          // running mean, ms
    ratio: 1.8,           // onset when envelope > ratio × running mean
    rearm: 1.2,           // re-arm when envelope falls below rearm × mean
    floor: 0.01,          // ignore anything below this RMS
    refractory: 120,      // ms between onsets
    poll: 10              // ms
  };

  function BassDetector(audioCtx, onOnset, opts) {
    var o = this.o = {};
    for (var k in BASS) o[k] = BASS[k];
    for (k in (opts || {})) o[k] = opts[k];
    this.ctx = audioCtx; this.onOnset = onOnset;
    this.input = audioCtx.createGain();
    var hp = audioCtx.createBiquadFilter(); hp.type = 'highpass'; hp.frequency.value = o.lo; hp.Q.value = Math.SQRT1_2;
    var lp = audioCtx.createBiquadFilter(); lp.type = 'lowpass'; lp.frequency.value = o.hi; lp.Q.value = Math.SQRT1_2;
    this.analyser = audioCtx.createAnalyser(); this.analyser.fftSize = 2048;
    var sink = audioCtx.createGain(); sink.gain.value = 0;   // keeps the graph pulled, silently
    this.input.connect(hp); hp.connect(lp); lp.connect(this.analyser); this.analyser.connect(sink); sink.connect(audioCtx.destination);
    this.buf = new Float32Array(this.analyser.fftSize);
    this.span = Math.min(this.buf.length, Math.round(audioCtx.sampleRate * 0.02));   // 20 ms RMS
    this.env = 0; this.mean = 0; this.armed = true; this.lastOn = -1e9; this.lastT = 0; this.timer = 0;
  }

  BassDetector.prototype.start = function () {
    var self = this;
    if (this.timer) return;
    this.lastT = now();
    this.timer = root.setInterval(function () { self._poll(); }, this.o.poll);
  };

  BassDetector.prototype.stop = function () {
    root.clearInterval(this.timer); this.timer = 0;
  };

  BassDetector.prototype._poll = function () {
    var o = this.o, t = now(), dt = Math.max(1, t - this.lastT); this.lastT = t;
    this.analyser.getFloatTimeDomainData(this.buf);
    var sum = 0, n = this.buf.length;
    for (var i = n - this.span; i < n; i++) sum += this.buf[i] * this.buf[i];
    var rms = Math.sqrt(sum / this.span);
    var a = 1 - Math.exp(-dt / (rms > this.env ? o.attack : o.release));
    this.env += (rms - this.env) * a;
    var m = this.mean;
    this.mean += (this.env - this.mean) * (1 - Math.exp(-dt / o.window));
    if (!this.armed && this.env < m * o.rearm) this.armed = true;
    if (this.armed && this.env > o.floor && this.env > m * o.ratio && t - this.lastOn > o.refractory) {
      this.armed = false; this.lastOn = t;
      var r = m > 1e-6 ? this.env / m : 10;
      this.onOnset(clamp(0.6 * Math.sqrt(r / o.ratio), 0.3, 1));
    }
  };

  root.UVGround = { version: '1.0.0', Field: Field, BassDetector: BassDetector, defaults: DEFAULTS, bassDefaults: BASS };
})(window);

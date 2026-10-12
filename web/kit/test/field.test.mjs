// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The animated ground's simulation.
 *
 * TWO KINDS OF CLAIM HERE, and they are different in status.
 *
 * The first is that this is still a PORT. src/lib/field.js was copied from the
 * design system's own reference implementation, and the numbers in it are the
 * design's parameter table -- so the test reads the reference's defaults out of
 * design/scheme/project/components/ground.js and diffs them. That is the same
 * argument the token guard makes about colours, applied to the one other place
 * the design system hands us values: a generator would overwrite a
 * disagreement, a test names it. A table can stay put while the MODEL moves --
 * Ultraviolet 1.3.0 changed both -- so the release is held to the reference's
 * too, and the reference itself is RUN beside the port, on the same stub
 * canvas, and the two fields compared: the same emitters in the same order, the
 * same wave to float rounding. The reference is plain JavaScript with no
 * dependency, so this is the one oracle the native port (tests/ui/
 * groundfield.cpp) could not have.
 *
 * The second is that the physics behaves: a drop spreads from its emitter as a
 * circle, the downbeat lands on the anchor and the bar walks clockwise, boxes
 * are solid and reflect, the border lets half a wave go, the field rings out to
 * EXACTLY zero rather than nearly zero, and the Motion switch stops it dead.
 * Those are the properties the design's Motion section promises a user, and
 * none of them is visible in a screenshot. Most are the native suite's, ported
 * with the same oracles, so the two kits are held to one description.
 *
 * THE DOM IS STUBBED rather than emulated. `Field` needs a canvas, a 2D context
 * and three custom properties; it does not need a layout engine, and pulling one
 * in would make this suite slower than the plugin build. The stub records what
 * it is asked to draw only where a test looks at it.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const HERE = dirname(fileURLToPath(import.meta.url));
const ROOT = join(HERE, '..', '..', '..');
const REFERENCE = join(ROOT, 'design', 'scheme', 'project', 'components', 'ground.js');
const TOKENS = join(HERE, '..', 'src', 'tokens.css');
const COMPONENTS = join(HERE, '..', 'src', 'components.css');
const BUNDLE = join(ROOT, 'design', 'scheme', 'project', 'components', 'bundle.css');

/* ---------- the stub ---------- */

/** A 2D context that accepts everything and remembers only the blits. */
function stubContext() {
  return {
    canvas: null,
    globalAlpha: 1,
    fillStyle: '',
    blits: 0,
    setTransform() {},
    fillRect() {},
    drawImage() { this.blits++; },
    beginPath() {},
    arc() {},
    fill() {},
    createImageData(w, h) { return { data: new Uint8ClampedArray(w * h * 4) }; },
    putImageData() {},
  };
}

function stubCanvas(w, h, k = 1) {
  const ctx = stubContext();
  return {
    clientWidth: w,
    clientHeight: h,
    width: 0,
    height: 0,
    getContext: () => ctx,
    /* What an editor's `transform: scale(k)` does to the on-screen box and not
     * to the layout one. */
    getBoundingClientRect() {
      return { left: 0, top: 0, width: this.clientWidth * k, height: this.clientHeight * k };
    },
    _ctx: ctx,
  };
}

/** A minimal EventTarget: enough for `document` and a MediaQueryList. */
function target(extra = {}) {
  const on = {};
  return {
    ...extra,
    addEventListener(type, fn) { (on[type] ||= new Set()).add(fn); },
    removeEventListener(type, fn) { on[type]?.delete(fn); },
    dispatch(type, ev = {}) { for (const fn of on[type] || []) fn(ev); },
    listeners(type) { return on[type]?.size || 0; },
  };
}

/** The three colours Field reads, taken from the real tokens.css. */
function tokenValues() {
  const css = (readFileSync(TOKENS.replace(/tokens\.css$/, 'tokens.gen.css'), 'utf8') + readFileSync(TOKENS, 'utf8'));
  const out = {};
  for (const name of ['--bg-000', '--bg-dot', '--uv-deep']) {
    const m = css.match(new RegExp(`${name}:\\s*(#[0-9a-fA-F]{6})\\s*;`));
    assert.ok(m, `${name} is not declared in tokens.css`);
    out[name] = m[1];
  }
  return out;
}

/**
 * Install the globals Field touches, and return a teardown.
 *
 * `reducedMotion` is a parameter because it is a branch the design calls a rule:
 * with it set, `trigger` must do nothing at all.
 */
function installDom({ reducedMotion = false, dpr = 1 } = {}) {
  const values = tokenValues();
  const saved = { ...globalThis };
  globalThis.document = target({
    documentElement: { __root: true },
    hidden: false,
    createElement: () => stubCanvas(96, 96),
  });
  globalThis.getComputedStyle = () => ({
    getPropertyValue: (name) => values[name] ?? '',
  });
  /* One list per query, kept, so a test can flip it and dispatch `change`. */
  const media = {};
  globalThis.matchMedia = (q) =>
    (media[q] ||= target({ matches: q.includes('reduced-motion') ? reducedMotion : false }));
  globalThis.__media = media;
  globalThis.devicePixelRatio = dpr;
  /* THE FRAME CLOCK IS THE PLUGIN'S TICKS, with a page timer as fallback, and
   * here that timer never calls back on its own: every test below steps the
   * simulation itself, so a tick that fired by itself would make the number of
   * steps depend on how busy the machine was. The callbacks are kept in
   * `__intervals` for the tests that drive the fallback by hand.
   *
   * requestAnimationFrame THROWS: a plugin host shows the editor as hidden and
   * all but stops animation frames, so the field must never depend on one. */
  let handle = 0;
  const intervals = new Map();
  globalThis.__intervals = intervals;
  globalThis.setInterval = (fn) => { intervals.set(++handle, fn); return handle; };
  globalThis.clearInterval = (id) => { intervals.delete(id); };
  globalThis.requestAnimationFrame = () => {
    throw new Error('the field asked for an animation frame');
  };
  globalThis.cancelAnimationFrame = () => {};
  return () => {
    for (const k of ['document', 'getComputedStyle', 'matchMedia', 'devicePixelRatio',
      'requestAnimationFrame', 'cancelAnimationFrame', 'setInterval', 'clearInterval',
      '__media', '__intervals']) {
      if (k in saved) globalThis[k] = saved[k]; else delete globalThis[k];
    }
  };
}

/** Import Field with the DOM already in place. */
async function loadField() {
  const { Field, DEFAULTS, LEVELS, MID, VERSION } = await import('../src/lib/field.js');
  return { Field, DEFAULTS, LEVELS, MID, VERSION };
}

/**
 * The design's own ground.js, run as the page it was written for would run
 * it: a script that hangs `UVGround` on `window`. installDom's globals are the
 * window it gets, so it draws into the same stub canvas as the port.
 */
function loadReference() {
  new Function('window', readFileSync(REFERENCE, 'utf8'))(globalThis); // eslint-disable-line no-new-func
  const ref = globalThis.UVGround;
  delete globalThis.UVGround;
  return ref;
}

/** Step the simulation `secs` of simulated time, ignoring the frame clock. */
function advance(field, secs) {
  const steps = Math.round(secs / field.o.dt);
  for (let i = 0; i < steps; i++) field._step();
  return field._peak();
}

/** A field of w x h px with these boxes. */
function sized(Field, w, h, rects = []) {
  const field = new Field(stubCanvas(w, h));
  field.setSources(rects);
  return field;
}

/** The displacement at the node nearest (x, y), in px. */
function at(field, x, y) {
  const h = field.o.cell;
  return field.u[Math.round(y / h) * field.nx + Math.round(x / h)];
}

/** Where |u| is largest, in px. */
function loudest(field) {
  let best = 0;
  for (let q = 1; q < field.u.length; q++) {
    if (Math.abs(field.u[q]) > Math.abs(field.u[best])) best = q;
  }
  return { x: (best % field.nx) * field.o.cell, y: Math.floor(best / field.nx) * field.o.cell };
}

/** u at (x, y), one sample a step for `secs`. */
function trace(field, x, y, secs) {
  const out = [];
  for (let i = 0; i < Math.round(secs / field.o.dt); i++) {
    field._step();
    out.push(at(field, x, y));
  }
  return out;
}

/** When a trace's |u| peaked inside [from, to), and how high. */
function loudestBetween(samples, dt, from, to) {
  let best = { t: 0, height: 0 };
  samples.forEach((v, s) => {
    const t = (s + 1) * dt;
    if (t >= from && t < to && Math.abs(v) > best.height) best = { t, height: Math.abs(v) };
  });
  return best;
}

const distance = (p, q) => Math.hypot(p.x - q.x, p.y - q.y);

/* ---------- it is still a port ---------- */

test('every parameter matches the design system\'s reference implementation', async () => {
  const teardown = installDom();
  try {
    const { DEFAULTS } = await loadField();
    const src = readFileSync(REFERENCE, 'utf8');
    /* The reference's DEFAULTS block, as `name: number` pairs. Its colour
     * entries are arrays and are deliberately NOT compared: this port reads
     * those three from tokens.css instead, which is the one intentional
     * difference and is documented in both files. */
    const block = src.match(/var DEFAULTS = \{([\s\S]*?)\n  \};/);
    assert.ok(block, 'could not find DEFAULTS in the reference implementation');

    const reference = {};
    for (const line of block[1].split('\n')) {
      const m = line.match(/^\s*(\w+):\s*([-\d.]+(?:\s*\/\s*[\d.]+)?|true|false)\s*,/);
      if (!m) continue;
      const raw = m[2].trim();
      reference[m[1]] =
        raw === 'true' ? true
          : raw === 'false' ? false
            : raw.includes('/') ? eval(raw) /* eslint-disable-line no-eval */
              : Number(raw);
    }

    /* Enough of them that a truncated parse would fail loudly rather than pass
     * by comparing nothing: 1.3.0 has twenty-five. */
    assert.ok(Object.keys(reference).length >= 25,
      `only parsed ${Object.keys(reference).length} reference parameters`);

    for (const [name, value] of Object.entries(reference)) {
      assert.equal(DEFAULTS[name], value,
        `${name} is ${DEFAULTS[name]} here and ${value} in the design system's ` +
        'ground.js -- the field is a port, so this is either a transcription ' +
        'error or a design change that has not been recorded');
    }
    /* And the other way round: a parameter the design has dropped is a model
     * that has moved on without this port. 1.3.0 dropped `border`, the switch
     * that made the window's border emit. */
    for (const name of Object.keys(DEFAULTS)) {
      assert.ok(name in reference,
        `${name} is a parameter here and not in the design system's ground.js`);
    }
  } finally { teardown(); }
});

test('the port names the release it ports, and it is the reference\'s', async () => {
  /* A release can change the model and keep every number -- so the version is
   * held to the reference as well. A failure here means ground.js moved on:
   * read its diff, port it, then change VERSION. */
  const teardown = installDom();
  try {
    const { VERSION } = await loadField();
    const m = readFileSync(REFERENCE, 'utf8').match(/version:\s*'([\d.]+)'/);
    assert.ok(m, 'could not find UVGround.version in the reference implementation');
    assert.equal(VERSION, m[1], `field.js ports Ultraviolet Ground ${VERSION}; the design is at ${m[1]}`);
  } finally { teardown(); }
});

test('the port computes the reference\'s field: the same emitters, the same wave', async () => {
  /*
   * THE STRONGEST CLAIM A PORT CAN MAKE, and the one only a JavaScript port
   * can test this way: ground.js is run beside field.js on the same layout
   * with the same rings, and the two are compared.
   *
   * The emitters must match EXACTLY -- positions, order, footprints -- because
   * they are the same arithmetic on the same numbers. The wave matches to
   * float rounding and not exactly, because field.js takes the elastic and
   * viscous terms in one stencil pass over a Float32 mix of u and u_prev (see
   * its header) where the reference takes two in doubles. Measured: 2.5e-6 at
   * worst, against a peak near 4, over twenty seconds of a bar walking two
   * boxes' corners and ringing out.
   */
  const teardown = installDom();
  try {
    const { Field } = await loadField();
    const UVGround = loadReference();
    const layouts = [
      [320, 200, [{ x: 40, y: 40, w: 100, h: 60 }, { x: 200, y: 90, w: 80, h: 60 }]],
      [360, 240, [{ x: 30, y: 40, w: 80, h: 60 }, { x: 120, y: 40, w: 80, h: 60 }]],   // a 10 px gutter
      [240, 150, [{ x: 4, y: 4, w: 100, h: 60 }]],                                     // against the border
      [400, 300, []],                                                                  // no box: the centre
    ];
    for (const [w, h, rects] of layouts) {
      const port = sized(Field, w, h, rects);
      const ref = new UVGround.Field(stubCanvas(w, h));
      ref.setSources(rects);
      assert.deepEqual(
        port.emitters.map((e) => [e.x, e.y]),
        ref.emitters.map((e) => [e.x, e.y]),
        `${w}x${h}: the emitters, or their order, differ from the reference's`);
      port.emitters.forEach((e, k) => assert.deepEqual(e.fp, ref.emitters[k].fp,
        `${w}x${h}: emitter ${k}'s footprint differs from the reference's`));

      let worst = 0;
      let lit = 0;
      const bar = [1, 0.4, 0.4, 0.4, 1, 0.4, 0.4, 0.4];
      for (let k = 0; k < bar.length + 20; k++) {
        if (k < bar.length) {
          port.trigger(bar[k]);
          ref.trigger(bar[k]);
        }
        for (let s = 0; s < 30; s++) {
          port._step();
          ref._step();
        }
        for (let q = 0; q < port.u.length; q++) worst = Math.max(worst, Math.abs(port.u[q] - ref.u[q]));
        for (let j = 0; j < port.ny; j += 2) {
          for (let i = 0; i < port.nx; i += 2) {
            if (!port.wall[j * port.nx + i]) lit = Math.max(lit, Math.abs(port._lit(i, j) - ref._lit(i, j)));
          }
        }
      }
      assert.ok(worst < 1e-4, `${w}x${h}: the wave is ${worst} from the reference's`);
      assert.ok(lit < 1e-4, `${w}x${h}: the lit display is ${lit} from the reference's`);
      assert.equal(port.rings.length, ref.rings.length);
    }
  } finally { teardown(); }
});

/* ---------- the physics ---------- */

test('a ring raises the field and it rings out to exactly zero', async () => {
  const teardown = installDom();
  try {
    const { Field } = await loadField();
    const field = new Field(stubCanvas(400, 300));

    assert.equal(field._peak(), 0, 'a fresh field is flat');
    field.trigger(1);
    /* The wavelet peaks a little after the trigger, not on it. */
    const peak = advance(field, 0.5);
    assert.ok(peak > 0.05, `a full-strength ring barely moved the field (${peak})`);

    /* REWRITTEN FOR 1.3.0, whose numbers moved: tau went from 3.5 s to 10, and
     * the border, which used to reflect everything, now lets half of a wave
     * go. The design's "at rest about ten seconds after the last ring" is
     * what a person sees -- measured here, the last dot is back at its resting
     * sprite after 11 s -- while the timer waits for |u| under `rest`, far
     * below one sprite level, which this window reaches after about 17 s.
     * Thirty is that with room, and still fails a field that does not fade. */
    const late = advance(field, 30);
    assert.ok(late < field.o.rest,
      `after 30 s the field should be under rest (${field.o.rest}); it is ${late}`);

    /* And `_tick` is what takes it from "under rest" to EXACTLY zero -- the
     * design requires a still frame to look like the static design, and 1e-9
     * of wave is a dot drawn at the wrong level. */
    field._tick();
    assert.equal(field._peak(), 0, 'the field did not return to exactly zero');
    assert.equal(field.running, false, 'the field still counts as moving at rest');
    assert.equal(globalThis.__intervals.size, 0, 'and its fallback timer is still set');
  } finally { teardown(); }
});

test('a strong ring is stronger than a weak one, and strength is clamped to 0..1', async () => {
  const teardown = installDom();
  try {
    const { Field } = await loadField();
    const peakFor = (s) => {
      const field = new Field(stubCanvas(400, 300));
      field.trigger(s);
      return advance(field, 0.5);
    };
    assert.ok(peakFor(1) > peakFor(0.4), 'a downbeat must move the field more than a beat');
    /* Out-of-range input is clamped rather than trusted: the plugin sends 0.4
     * or 1 but the field is what has to survive a bug upstream. */
    assert.equal(peakFor(5), peakFor(1), 'a strength above 1 is not clamped');
    assert.equal(peakFor(-1), peakFor(0), 'a negative strength is not clamped');
  } finally { teardown(); }
});

test('a beat ring reads clearly as the lesser of the two, and still moves the ground', async () => {
  /*
   * The plugin rings 1.0 on each bar's downbeat and 0.4 on every other beat
   * (engines/ground/crates/ground-core, beat.rs). The field is linear in
   * strength, so a beat is a ring two fifths the height -- but what a person
   * sees is dots drawn at another LEVEL, through the light, tanh and 25
   * quantised levels, so that is what is measured: the largest share of the
   * window's dots that a ring moves two levels or more from rest.
   *
   * REWRITTEN FOR 1.3.0. It used to read tanh(u) at every grid node and ask
   * for a downbeat 2.5 times the beat's share (measured then: 53 % and 14 %).
   * A dot now shows the height PLUS the slope towards the light, and a drop of
   * gain 220 is soft-clipped near its emitter, so the shares came closer:
   * measured, 61 % for a downbeat, 31 % for a beat, from the dots the editor
   * draws. Twice the share, at two fifths the height, is still one strong
   * pulse and its lighter beats; 1.8 holds that with room.
   */
  const teardown = installDom();
  try {
    const { Field, MID } = await loadField();
    const ring = (s) => {
      const field = new Field(stubCanvas(400, 300));
      field.trigger(s);
      let peak = 0;
      let share = 0;
      for (let i = 0; i < 120; i++) {
        field._step();
        peak = Math.max(peak, field._peak());
        let moved = 0;
        let open = 0;
        for (let j = 0; j < field.rows; j++) {
          for (let k = 0; k < field.cols; k++) {
            if (field.wall[2 * j * field.nx + 2 * k]) continue;
            open++;
            if (Math.abs(field._level(k, j) - MID) >= 2) moved++;
          }
        }
        share = Math.max(share, moved / open);
      }
      return { peak, share };
    };
    const down = ring(1);
    const beat = ring(0.4);
    assert.ok(Math.abs(beat.peak / down.peak - 0.4) < 0.01,
      `the field is no longer linear in strength (${beat.peak} / ${down.peak})`);
    assert.ok(beat.share > 0.08, `a beat ring barely shows (${beat.share})`);
    assert.ok(down.share > 1.8 * beat.share,
      `a downbeat (${down.share}) does not read clearly above a beat (${beat.share})`);
  } finally { teardown(); }
});

test('a drop spreads from its emitter as a circle, at the speed c', async () => {
  /*
   * The ring of 1.3.0 is a drop at ONE point, where 1.1.0 raised every box
   * edge and the whole border at once. An open window has one emitter, its
   * centre; the ring reaches a probe on the axis and one on the diagonal
   * (93.3 px out against 96) at the same speed and with the same height --
   * the 9-point stencil is what keeps it round on the square grid -- and a
   * probe twice as far out hears it 0.48 s later, which is c.
   */
  const teardown = installDom();
  try {
    const { Field } = await loadField();
    const field = sized(Field, 480, 480);
    assert.deepEqual(field.emitters.map((e) => [e.x, e.y]), [[240, 240]]);
    field.trigger(1);
    const dt = field.o.dt;
    const near = [];
    const axis = [];
    const diagonal = [];
    for (let i = 0; i < Math.round(2.4 / dt); i++) {
      field._step();
      near.push(at(field, 240 + 48, 240));
      axis.push(at(field, 240 + 96, 240));
      diagonal.push(at(field, 240 + 66, 240 + 66));
    }
    const n = loudestBetween(near, dt, 0, 2.4);
    const a = loudestBetween(axis, dt, 0, 2.4);
    const d = loudestBetween(diagonal, dt, 0, 2.4);
    assert.ok(Math.abs(a.t - n.t - 48 / field.o.speed) < 0.05,
      `48 px further out took ${a.t - n.t} s; at c it is ${48 / field.o.speed}`);
    assert.ok(n.height > a.height, 'the ring does not thin as it widens');
    assert.ok(Math.abs(a.t - d.t - (96 - 93.34) / field.o.speed) < 0.05,
      `the ring is not round: axis at ${a.t} s, diagonal at ${d.t} s`);
    assert.ok(Math.abs(d.height / a.height - 1) < 0.1,
      `the ring is not round: axis ${a.height}, diagonal ${d.height}`);
  } finally { teardown(); }
});

test('emitters stand off the open corners, the anchor nearest the centre, then clockwise', async () => {
  const teardown = installDom();
  try {
    const { Field } = await loadField();
    const field = sized(Field, 240, 180, [{ x: 60, y: 60, w: 60, h: 36 }]);
    const e = field.o.inset;
    /* The bottom right corner is nearest the centre (120, 90): the anchor.
     * Clockwise ON SCREEN from it: bottom left, top left, top right. */
    assert.deepEqual(field.emitters.map((p) => [p.x, p.y]), [
      [120 + e, 96 + e], [60 - e, 96 + e], [60 - e, 60 - e], [120 + e, 60 - e],
    ]);
    /* A drop's footprint is open ground only: none of it lies in the box. */
    for (const p of field.emitters) {
      for (let k = 0; k < p.fp.length; k += 2) assert.equal(field.wall[p.fp[k]], 0);
    }
  } finally { teardown(); }
});

test('no emitter in a narrow gutter, near the border, or inside a box', async () => {
  /* REPLACES "a window with a box has more sources than one without". The
   * sources of 1.1.0 -- every open node along a box edge and the border --
   * are gone; what a box adds now is the corners that face open ground, and
   * which those are is the whole rule. */
  const teardown = installDom();
  try {
    const { Field } = await loadField();
    /* Two boxes 10 px apart: the corners facing the gutter are too close to
     * the other box; the outer four are free. */
    const gutter = sized(Field, 240, 150, [{ x: 30, y: 40, w: 80, h: 60 }, { x: 120, y: 40, w: 80, h: 60 }]);
    assert.equal(gutter.emitters.length, 4);
    for (const p of gutter.emitters) assert.ok(p.x < 30 || p.x > 200, `an emitter in the gutter at ${p.x}`);

    /* A box against the border keeps only the corner that faces open ground. */
    const edge = sized(Field, 240, 150, [{ x: 4, y: 4, w: 100, h: 60 }]);
    assert.deepEqual(edge.emitters.map((p) => [p.x, p.y]), [[104 + edge.o.inset, 64 + edge.o.inset]]);

    /* No box: the window centre. A window that is all box: nowhere, and a
     * ring there is nothing at all -- not a ring dropped somewhere else. */
    assert.deepEqual(sized(Field, 200, 120).emitters.map((p) => [p.x, p.y]), [[100, 60]]);
    const shut = sized(Field, 60, 60, [{ x: -1, y: -1, w: 62, h: 62 }]);
    assert.equal(shut.emitters.length, 0);
    shut.trigger(1);
    assert.equal(shut.running, false, 'a ring with nowhere to fall started the clock');

    /* A zero-area box used to add no source, and this test said the field
     * must not depend on Ground filtering it. Under 1.3.0 that no longer
     * holds, in the reference as here: a box of no area is a point with four
     * corners, and the open one becomes an emitter in place of the centre.
     * So the filter in ground-geometry's toCanvasRects (tested there, "a box
     * with no area is dropped") is now what keeps a panel that is not laid
     * out yet from planting a drop, and this pins why it matters. */
    const point = sized(Field, 400, 300, [{ x: 10, y: 10, w: 0, h: 0 }]);
    assert.deepEqual(point.emitters.map((p) => [p.x, p.y]), [[10 + point.o.inset, 10 + point.o.inset]]);
  } finally { teardown(); }
});

test('the downbeat lands on the anchor, and the bar\'s other beats walk the corners', async () => {
  /* A downbeat, three beats, the next downbeat: anchor, 1, 2, 3, anchor. The
   * rings that came before are let fall first, then the field is flattened
   * -- which keeps the walk -- so each drop is found alone, half a second
   * on, still where it fell. */
  const teardown = installDom();
  try {
    const { Field } = await loadField();
    const box = [{ x: 150, y: 120, w: 60, h: 36 }];
    const bar = [1, 0.4, 0.4, 0.4, 1];
    const where = sized(Field, 360, 276, box).emitters;
    assert.equal(where.length, 4);
    for (let k = 0; k < bar.length; k++) {
      const field = sized(Field, 360, 276, box);
      for (let r = 0; r < k; r++) field.trigger(bar[r]);
      field.setEnabled(false);
      field.setEnabled(true);
      field.trigger(bar[k]);
      advance(field, 0.5);
      const d = distance(loudest(field), where[k % 4]);
      assert.ok(d < 2 * field.o.cell, `ring ${k} fell ${d} px from emitter ${k % 4}`);
    }

    /* A downbeat restarts the walk wherever it was: after two beats, the
     * next downbeat is the anchor's again, and the beat after it emitter 1. */
    const field = sized(Field, 360, 276, box);
    for (const s of [1, 0.4, 0.4, 1]) field.trigger(s);
    assert.equal(field.walk, 0);
    field.trigger(0.4);
    assert.equal(field.rings.at(-1).fp, field.emitters[1].fp);

    /* `at` drops a ring where it is told -- the design preview's click. */
    const free = sized(Field, 360, 276, box);
    free.trigger(1, { x: 60, y: 60 });
    advance(free, 0.5);
    assert.ok(distance(loudest(free), { x: 60, y: 60 }) < 2 * free.o.cell);
  } finally { teardown(); }
});

test('a box sends a ring back; the window border only some of it', async () => {
  /*
   * NEW IN 1.3.0: the border is an impedance edge reflecting `borderR`, where
   * every edge used to be a mirror. A ring from the centre of a 360 x 360
   * window. On the right a box from x = 300 on, 120 px away: a probe 60 px
   * before it hears the ring, then its echo. On the left the border, 180 px
   * away: a probe 60 px before it hears the ring, then the border's echo --
   * half of it, head on. Each echo is measured against its own ring, so the
   * spreading on the way cancels as far as it can. The native suite's test,
   * with its oracle.
   */
  const teardown = installDom();
  try {
    const { Field } = await loadField();
    const field = sized(Field, 360, 360, [{ x: 300, y: -10, w: 200, h: 380 }]);
    assert.deepEqual(field.emitters.map((p) => [p.x, p.y]), [[180, 180]]);
    field.trigger(1);
    /* Ring 0 of a fresh field is the hash's -1: its frequency is f0 (1 - varyF),
     * and its wavelet peaks one period after the trigger. */
    const lag = 1 / (field.o.freq * (1 - field.o.varyF));
    const dt = field.o.dt;
    const right = [];
    const left = [];
    for (let i = 0; i < Math.round(4 / dt); i++) {
      field._step();
      right.push(at(field, 240, 180));
      left.push(at(field, 60, 180));
    }
    /* Direct at 60 and 120 px, echoes after 180 and 240 px. */
    const rd = loudestBetween(right, dt, 0, 1.2 + lag);
    const re = loudestBetween(right, dt, 1.2 + lag, 2.4 + lag);
    const ld = loudestBetween(left, dt, 0, 1.8 + lag);
    const le = loudestBetween(left, dt, 1.8 + lag, 3.0 + lag);
    const near = (t, want) => Math.abs(t - want) <= 0.15 * want;
    assert.ok(near(rd.t, 0.6 + lag), `the ring reached the right probe at ${rd.t} s`);
    assert.ok(near(re.t, 1.8 + lag), `the box's echo came back at ${re.t} s`);
    assert.ok(near(le.t, 2.4 + lag), `the border's echo came back at ${le.t} s`);

    /* A box is a mirror: the echo is all there, less the spreading
     * (sqrt(60 / 180) = 0.58) and the damping. */
    const box = re.height / rd.height;
    assert.ok(box > 0.4, `the box sent back ${box} of the ring`);
    /* The border keeps half back, and spreading thins it less there
     * (sqrt(120 / 240) = 0.71) -- yet its echo is the weaker. */
    const border = le.height / ld.height;
    assert.ok(border > 0.1, `the border sent back nothing (${border})`);
    assert.ok(border < 0.8 * box, `the border (${border}) reflects like a box (${box})`);
  } finally { teardown(); }
});

test('between two boxes a ring echoes on and on', async () => {
  /* A 120 px channel between two boxes, and the same window without them:
   * long after the ring has passed, the channel is still moving; the open
   * window, its border letting the ring go, is all but still. */
  const teardown = installDom();
  try {
    const { Field } = await loadField();
    const boxed = sized(Field, 360, 360,
      [{ x: -10, y: -10, w: 380, h: 130 }, { x: -10, y: 240, w: 380, h: 130 }]);
    const open = sized(Field, 360, 360);
    assert.equal(boxed.emitters.length, 1);
    boxed.trigger(1);
    open.trigger(1);
    advance(boxed, 3);
    advance(open, 3);
    let inChannel = 0;
    let inOpen = 0;
    for (let k = 0; k < 60; k++) {
      boxed._step();
      open._step();
      for (let x = 30; x <= 330; x += 30) {
        inChannel = Math.max(inChannel, Math.abs(at(boxed, x, 180)));
        inOpen = Math.max(inOpen, Math.abs(at(open, x, 180)));
      }
    }
    assert.ok(inChannel > 3 * inOpen, `channel ${inChannel}, open window ${inOpen}`);
  } finally { teardown(); }
});

test('no ripple at the grid\'s scale', async () => {
  /* NEW IN 1.3.0, with the viscous term. A ring is a long wave: one node's
   * curvature against its height is about (k h)^2, 0.3 for 67 px. Ripple at
   * the grid's scale would be over 1, and none builds up, even after two bars
   * echoing between two boxes. (Measured 0.17. The soft drop is most of why;
   * the next test is the one that holds nu to its job.) */
  const teardown = installDom();
  try {
    const { Field } = await loadField();
    const field = sized(Field, 360, 240,
      [{ x: 60, y: 60, w: 90, h: 60 }, { x: 210, y: 120, w: 90, h: 60 }]);
    for (let beat = 0; beat < 8; beat++) {
      field.trigger(beat % 4 === 0 ? 1 : 0.4);
      advance(field, 0.5);
    }
    const { nx, ny, u, wall } = field;
    let curve = 0;
    let height = 0;
    for (let j = 1; j < ny - 1; j++) {
      for (let i = 1; i < nx - 1; i++) {
        let clear = true;
        for (let dj = -1; dj <= 1; dj++) {
          for (let di = -1; di <= 1; di++) clear = clear && !wall[(j + dj) * nx + i + di];
        }
        if (!clear) continue;
        const q = j * nx + i;
        curve = Math.max(curve, Math.abs(u[q - 1] + u[q + 1] + u[q - nx] + u[q + nx] - 4 * u[q]));
        height = Math.max(height, Math.abs(u[q]));
      }
    }
    assert.ok(height > 0.1, `the field is too still to judge (${height})`);
    assert.ok(curve / height < 0.6, `grid-scale ripple: curvature ${curve} against height ${height}`);
  } finally { teardown(); }
});

test('grid-scale ripple dies within a second; a wave of the ring\'s length lives on', async () => {
  /*
   * The design's own words for `visc`, and the sharp test of it. The one
   * above is the native suite's and holds -- but a drop of sigma 16 px barely
   * excites grid-scale ripple in the first place, so it passes with nu at
   * zero as well. This one plants the ripple: the finest wave the grid holds
   * (a checkerboard) and, in a second field, a standing wave as long as a
   * ring's, both at rest, and looks at the window's middle a second on, over
   * one period. Measured: the ripple keeps 0.17 of itself, the long wave all
   * of it; without nu the ripple would keep 0.95 (tau alone).
   */
  const teardown = installDom();
  try {
    const { Field } = await loadField();
    const after = (shape) => {
      const field = sized(Field, 240, 240);
      const { nx, ny, o } = field;
      for (let j = 0; j < ny; j++) {
        for (let i = 0; i < nx; i++) field.u[j * nx + i] = field.up[j * nx + i] = shape(i, j, o);
      }
      const middle = () => {
        let m = 0;
        for (let j = 10; j < ny - 10; j++) {
          for (let i = 10; i < nx - 10; i++) m = Math.max(m, Math.abs(field.u[j * nx + i]));
        }
        return m;
      };
      const start = middle();
      advance(field, 1);
      let most = 0;
      for (let k = 0; k < Math.round(1 / o.freq / o.dt); k++) {
        field._step();
        most = Math.max(most, middle());
      }
      return most / start;
    };
    const ripple = after((i, j) => ((i + j) % 2 ? -1 : 1));
    const long = after((i, j, o) => Math.cos((2 * Math.PI * i * o.cell * o.freq) / o.speed));
    assert.ok(ripple < 0.3, `grid-scale ripple kept ${ripple} of itself after a second`);
    assert.ok(long > 0.7, `a wave as long as a ring kept only ${long} of itself`);
  } finally { teardown(); }
});

test('strength scales the wave exactly, and rings add up exactly', async () => {
  /* The equation is linear, so half the ring is half the wave and two rings
   * are the sum of their waves. Each field drops the same number of rings in
   * an open window, where every ring falls at the centre, so the k-th ring of
   * each is varied alike and only the strengths differ. */
  const teardown = installDom();
  try {
    const { Field } = await loadField();
    const field = (rings) => {
      const f = sized(Field, 240, 120);
      for (const s of rings) f.trigger(s);
      advance(f, 0.8);
      return f;
    };
    const a = field([1, 0]);
    const b = field([0, 0.4]);
    const both = field([1, 0.4]);
    const half = field([0.5]);
    const one = field([1]);
    let checked = 0;
    for (let q = 0; q < one.u.length; q += 3) {
      if (Math.abs(one.u[q]) < 0.05) continue;
      checked++;
      assert.ok(Math.abs(half.u[q] / one.u[q] - 0.5) < 0.005, `half a ring is ${half.u[q] / one.u[q]} of one`);
      assert.ok(Math.abs(both.u[q] - (a.u[q] + b.u[q])) <= 0.01 * Math.abs(both.u[q]) + 1e-6,
        `two rings are not the sum of each: ${both.u[q]} against ${a.u[q] + b.u[q]}`);
    }
    assert.ok(checked > 10, `only ${checked} nodes were moving enough to compare`);
  } finally { teardown(); }
});

test('each ring varies a little, and the same rings make the same field', async () => {
  /* NEW IN 1.3.0: a ring's frequency and strength vary by a hash of its
   * count, so the ground never repeats like a machine -- and since it is a
   * hash and not Math.random, two fields given the same rings agree to the
   * last bit, which every test above relies on. */
  const teardown = installDom();
  try {
    const { Field } = await loadField();
    const field = sized(Field, 240, 120);
    const o = field.o;
    field.trigger(1);
    field.trigger(1);
    const [r0, r1] = field.rings;
    /* The hash of 0 is -1 on both: ring 0 is the lowest and the weakest. */
    assert.equal(r0.f, o.freq * (1 - o.varyF));
    assert.equal(r0.s, 1 - o.varyS);
    assert.notEqual(r1.f, r0.f, 'two rings in a row are identical');
    for (const r of field.rings) {
      assert.ok(Math.abs(r.f / o.freq - 1) <= o.varyF + 1e-12);
      assert.ok(Math.abs(r.s - 1) <= o.varyS + 1e-12);
    }

    const boxes = [{ x: 40, y: 40, w: 100, h: 60 }, { x: 200, y: 90, w: 80, h: 60 }];
    const a = sized(Field, 320, 200, boxes);
    const b = sized(Field, 320, 200, boxes);
    for (let k = 0; k < 6; k++) {
      a.trigger(k % 4 === 0 ? 1 : 0.4);
      b.trigger(k % 4 === 0 ? 1 : 0.4);
      advance(a, 0.5);
      advance(b, 0.5);
    }
    assert.deepEqual(a.u, b.u, 'the same rings made different fields');
  } finally { teardown(); }
});

test('lit from the top left: a ring draws above rest and below, one side of a crest lit', async () => {
  /* NEW IN 1.3.0, the display. A dot shows the height plus the slope towards
   * a light at the top left. Across the light's axis a ring is drawn the same
   * on both sides; along it, one side of each crest is lit and the other
   * shaded, so the two sides differ. */
  const teardown = installDom();
  try {
    const { Field, LEVELS, MID } = await loadField();
    const field = sized(Field, 240, 120);
    field.trigger(1);
    let above = false;
    let below = false;
    for (let k = 0; k < 60; k++) {
      field._step();
      field._step();
      for (let j = 0; j < field.rows; j++) {
        for (let i = 0; i < field.cols; i++) {
          const l = field._level(i, j);
          assert.ok(l >= 0 && l < LEVELS, `level ${l} is outside the atlas`);
          above ||= l > MID;
          below ||= l < MID;
        }
      }
    }
    assert.ok(above && below, 'a ring must draw both above and below rest');

    const lit = sized(Field, 480, 480);
    lit.trigger(1);
    advance(lit, 1.5);
    let differs = 0;
    for (let d = 1; d < 20; d++) {
      assert.equal(lit._level(20 + d, 20 - d), lit._level(20 - d, 20 + d),
        'across the light\'s axis the two sides must match');
      differs += Math.abs(lit._level(20 - d, 20 - d) - lit._level(20 + d, 20 + d));
    }
    assert.ok(differs > 4, 'along the light\'s axis both sides are drawn alike: no light');
  } finally { teardown(); }
});

test('once the ring is out the field only ever fades', async () => {
  const teardown = installDom();
  try {
    const { Field } = await loadField();
    const field = sized(Field, 360, 240, [{ x: 60, y: 60, w: 90, h: 60 }]);
    field.trigger(1);
    advance(field, 2);
    assert.equal(field.rings.length, 0, 'the wavelet should be over after 2 s');
    let last = Infinity;
    for (let second = 0; second < 8; second++) {
      let energy = 0;
      for (let k = 0; k < 60; k++) {
        field._step();
        for (let q = 0; q < field.u.length; q++) energy += field.u[q] * field.u[q];
      }
      assert.ok(energy < last, `second ${second}: the energy rose (${energy} after ${last})`);
      last = energy;
    }
  } finally { teardown(); }
});

test('a box is solid: the field never enters it', async () => {
  const teardown = installDom();
  try {
    const { Field } = await loadField();
    const field = new Field(stubCanvas(400, 300));
    /* One panel in the middle, well away from the border. */
    const box = { x: 120, y: 90, w: 160, h: 120 };
    field.setSources([box]);

    /* Its centre, in grid coordinates. */
    const cell = field.o.cell;
    const gi = Math.round((box.x + box.w / 2) / cell);
    const gj = Math.round((box.y + box.h / 2) / cell);
    const q = gj * field.nx + gi;
    assert.equal(field.wall[q], 1, 'the centre of a box is not marked as a wall');

    field.trigger(1);
    /* Long enough that a ring crossing the window would have reached the middle
     * several times over: at 100 px/s, 160 px of box is 1.6 s. */
    for (let i = 0; i < Math.round(5 / field.o.dt); i++) {
      field._step();
      assert.equal(field.u[q], 0,
        'the field entered a panel -- panels and wells are opaque on purpose');
    }
  } finally { teardown(); }
});

test('the Motion switch stops the field immediately', async () => {
  const teardown = installDom();
  try {
    const { Field } = await loadField();
    const field = new Field(stubCanvas(400, 300));
    field.trigger(1);
    assert.ok(advance(field, 0.5) > 0.05, 'the field did not start');

    field.setEnabled(false);
    assert.equal(field._peak(), 0, 'switching Motion off did not flatten the field');
    assert.equal(field.rings.length, 0, 'a pending ring survived Motion being switched off');

    /* And it stays off: a trigger while disabled must do nothing at all. */
    field.trigger(1);
    assert.equal(advance(field, 0.5), 0, 'a ring fell while Motion was off');
  } finally { teardown(); }
});

test('prefers-reduced-motion disables the field outright', async () => {
  const teardown = installDom({ reducedMotion: true });
  try {
    const { Field } = await loadField();
    const said = [];
    const field = new Field(stubCanvas(400, 300), { onRunning: (on) => said.push(on) });
    assert.equal(field.reduced, true);
    field.trigger(1);
    assert.equal(advance(field, 1), 0,
      'the field moved with prefers-reduced-motion set -- the design makes this a ' +
      'rule, not a default the Motion switch can override');
    /* Not a ring held back, not a clock started, not a step of the walk: a
     * ring under reduced motion did not happen. */
    assert.equal(field.rings.length, 0);
    assert.equal(field.running, false);
    assert.deepEqual(said, [], 'the plugin was asked for frame ticks under reduced motion');
    assert.equal(field.walk, 0);
  } finally { teardown(); }
});

test('two rings interfere rather than the second replacing the first', async () => {
  const teardown = installDom();
  try {
    const { Field } = await loadField();
    const one = new Field(stubCanvas(400, 300));
    one.trigger(1);
    advance(one, 0.2);
    const single = one._peak();

    const two = new Field(stubCanvas(400, 300));
    two.trigger(1);
    advance(two, 0.1);
    two.trigger(1);
    advance(two, 0.1);

    /* Superposition: the drops sum, so two rings a tenth of a second apart
     * cannot leave the field exactly where one did. (The exact sum is pinned
     * by "rings add up exactly" above.) */
    assert.notEqual(two._peak(), single,
      'a second ring made no difference -- the drops are replacing rather than summing');
    assert.equal(two.rings.length, 2, 'both rings are still inside their wavelets');
  } finally { teardown(); }
});

test('a resize rebuilds the grid to the new size', async () => {
  const teardown = installDom();
  try {
    const { Field } = await loadField();
    const canvas = stubCanvas(400, 300);
    const field = new Field(canvas);
    const before = field.u.length;

    canvas.clientWidth = 800;
    canvas.clientHeight = 600;
    field.resize();
    assert.ok(field.u.length > before, 'the simulation grid did not grow with the canvas');
    assert.equal(field.canvas.width, 800, 'the backing store did not follow the CSS size');
  } finally { teardown(); }
});

test('destroy stops the loop and drops pending rings', async () => {
  /* Ground.jsx calls this when the editor unmounts, mid-ring as often as
   * not: the timer must go, the plugin must be told it can stop ticking, and
   * a late tick from it must not step a field that is gone. */
  const teardown = installDom();
  try {
    const { Field } = await loadField();
    const said = [];
    const field = new Field(stubCanvas(400, 300), { onRunning: (on) => said.push(on) });
    field.trigger(1);
    advance(field, 0.3);
    field.destroy();
    assert.equal(field.running, false);
    assert.equal(globalThis.__intervals.size, 0);
    assert.equal(field.rings.length, 0);
    assert.deepEqual(said, [true, false]);
    const t = field.t;
    field.lastTick -= 100;
    field.tick();
    assert.equal(field.t, t, 'a tick after destroy stepped the field');
    assert.equal(globalThis.__media['(prefers-reduced-motion: reduce)'].listeners('change'), 0,
      'destroy must unhook the media query');
  } finally { teardown(); }
});

test('a missing token is a reported bug, not a plausible-looking ground', async () => {
  /* ramp.js's rule, applied here: a ground drawn in colours nobody chose looks
   * nearly right, which is the worst possible outcome. It throws instead, where
   * this suite and the review harness both see it at once. */
  const teardown = installDom();
  try {
    const { Field } = await loadField();
    globalThis.getComputedStyle = () => ({ getPropertyValue: () => '' });
    assert.throws(() => new Field(stubCanvas(400, 300)), /tokens\.css/);
  } finally { teardown(); }
});

test('a field that cannot start is reported, not thrown at the editor', async () => {
  /* The companion to the test above, and the reason Ground.jsx catches.
   *
   * Solid mounts children before parents, so the Ground's own onMount runs
   * BEFORE the editor's -- and a throw there would stop the editor registering
   * its message handler, leaving a window that renders once and is then dead.
   * `Field` still throws (that is what the previous test pins); what must not
   * happen is that the throw reaches the editor. This asserts the shape the
   * component depends on: the failure is an ordinary exception at construction,
   * so a try/catch around `new Field` is sufficient and nothing is deferred to a
   * later tick where a catch could not reach it. */
  const teardown = installDom();
  try {
    const { Field } = await loadField();
    globalThis.getComputedStyle = () => ({ getPropertyValue: () => '' });
    let threw = null;
    try { new Field(stubCanvas(400, 300)); } catch (e) { threw = e; }
    assert.ok(threw instanceof Error, 'the failure must be a synchronous throw');
  } finally { teardown(); }
});

test('the ground canvas is stretched by width/height, not by inset alone', () => {
  /*
   * A REGRESSION GUARD FOR A BUG THAT LOOKED LIKE NOTHING AT ALL.
   *
   * `<canvas>` is a REPLACED element with an intrinsic size of 300x150. For a
   * replaced element, `width: auto` means "use the intrinsic width" -- so
   * `position: absolute; inset: 0` does NOT stretch it; the offsets become
   * over-constrained and `right`/`bottom` are dropped. The result is a 300x150
   * canvas in the top-left corner of the window.
   *
   * And that is invisible. A field at rest looks the same as the static CSS
   * ground underneath it, so the window looks exactly right, and only a small
   * patch in the corner ever animates. It shipped that way once.
   *
   * This cannot be caught by constructing a Field -- the tests above hand it a
   * stub canvas with whatever size they like, and it believed them. The claim is
   * about the stylesheet, so the stylesheet is what is read.
   */
  const css = readFileSync(COMPONENTS, 'utf8');
  const rule = /\.ground\s*\{([^}]*)\}/.exec(css);
  assert.ok(rule, '.ground is not declared in components.css');
  const body = rule[1];
  for (const prop of ['width', 'height']) {
    assert.match(body, new RegExp(`(^|[;\\s])${prop}\\s*:\\s*100%`),
      `.ground must set ${prop}: 100% -- inset: 0 alone leaves a canvas at its ` +
      'intrinsic 300x150, pinned to the corner, where nobody will notice it');
  }
});

/* ---------- the page around it ---------- */

test('the backing store includes the editor\'s scale, snapped to whole-pixel dots', async () => {
  /* Every editor fits its design to the viewport with transform: scale(k). A
   * canvas sized by devicePixelRatio alone is then resampled by k on screen --
   * blurred dots at k > 1, wasted pixels at k < 1. */
  const teardown = installDom({ dpr: 1 });
  try {
    const { Field } = await loadField();
    const field = new Field(stubCanvas(400, 300, 2));
    assert.equal(field.canvas.width, 800, 'a 2x-scaled window needs a 2x backing store');
    assert.equal(field.cw, 24, 'one dot pitch is 24 backing px at 2x');

    /* A scale that would make a pitch fractional is snapped, so every dot
     * lands on a whole pixel and the 68th is where the static ground has it. */
    const odd = new Field(stubCanvas(824, 300, 0.83));
    assert.equal(odd.cw, Math.round(12 * 0.83));
    assert.equal(odd.canvas.width, Math.round(824 * odd.cw / 12));
  } finally { teardown(); }
});

test('fit() rebuilds only when the size or the scale actually changed', async () => {
  /* A rebuild resets the field, so a layout notification that changed nothing
   * must not cost a ring in flight. */
  const teardown = installDom();
  try {
    const { Field } = await loadField();
    const canvas = stubCanvas(400, 300);
    const field = new Field(canvas);
    field.trigger(1);
    advance(field, 0.3);
    assert.equal(field.fit(), false, 'nothing changed, so nothing is rebuilt');
    assert.ok(field._peak() > 0, 'the ring survived a no-op fit');
    canvas.clientWidth = 500;
    assert.equal(field.fit(), true);
    assert.equal(field.canvas.width, 500);
  } finally { teardown(); }
});

test('the plugin\'s frame ticks move the field, and it says when it starts and stops', async () => {
  /* No animation frame anywhere: installDom's requestAnimationFrame throws. */
  const teardown = installDom();
  try {
    const { Field } = await loadField();
    const said = [];
    const field = new Field(stubCanvas(400, 300), { onRunning: (on) => said.push(on) });
    field.tick();
    assert.equal(field.t, 0, 'a tick at rest moves nothing');
    assert.deepEqual(said, []);

    field.trigger(1);
    assert.deepEqual(said, [true], 'a ring starts it, and the plugin is told');
    /* A tenth of a second since the last frame: one tick steps by it and
     * draws. */
    field.lastTick -= 100;
    field.tick();
    assert.ok(field.t > 0.09, `one tick after 100 ms stepped ${field.t} s`);

    /* Rung out over ticks, it stops and says so. */
    for (let i = 0; i < 2000 && field.running; i++) {
      field.lastTick -= 50;
      field.tick();
    }
    assert.equal(field.running, false);
    assert.deepEqual(said, [true, false]);
    assert.equal(globalThis.__intervals.size, 0, 'no fallback timer is left set');
  } finally { teardown(); }
});

test('a page timer steps the field only when the plugin\'s ticks stop coming', async () => {
  const teardown = installDom();
  try {
    const { Field } = await loadField();
    const field = new Field(stubCanvas(400, 300));
    field.trigger(1);
    const [fallback] = [...globalThis.__intervals.values()];
    assert.ok(fallback, 'a fallback timer is set while the field moves');

    field.tick();
    const t = field.t;
    field.lastTick -= 50;
    fallback();
    assert.equal(field.t, t, 'with ticks arriving the timer leaves the field to them');

    field.lastOutside -= 1000;
    field.lastTick -= 50;
    fallback();
    assert.ok(field.t > t, 'with none for a while, the timer steps it');
  } finally { teardown(); }
});

test('Motion off stops the field and tells the plugin it can stop ticking', async () => {
  const teardown = installDom();
  try {
    const { Field } = await loadField();
    const said = [];
    const field = new Field(stubCanvas(400, 300), { onRunning: (on) => said.push(on) });
    field.trigger(1);
    field.setEnabled(false);
    assert.deepEqual(said, [true, false]);
    assert.equal(globalThis.__intervals.size, 0);
  } finally { teardown(); }
});

test('a hidden document neither pauses the field nor is listened to', async () => {
  /*
   * In a real host WebKit reports the plugin's editor as hidden -- for as long
   * as it is open. The field used to pause while hidden, so in Live it never
   * moved. It no longer looks: the plugin stops ringing it when the window
   * closes, which is the only "nobody can see this" a plugin can know.
   */
  const teardown = installDom();
  try {
    document.hidden = true;
    const { Field } = await loadField();
    const field = new Field(stubCanvas(400, 300));
    assert.equal(document.listeners('visibilitychange'), 0, 'the field listens for visibility');
    field.trigger(1);
    assert.equal(field.running, true, 'a ring in a hidden document must start the clock');
    assert.ok(advance(field, 0.5) > 0.05, 'and move the field');
    document.dispatch('visibilitychange');
    assert.equal(field.running, true, 'a visibility change must not stop it');
  } finally { teardown(); }
});

test('turning on reduced motion while running flattens the field at once', async () => {
  /* The setting is read once no longer: a user who turns it on mid-session
   * gets it mid-session. */
  const teardown = installDom();
  try {
    const { Field } = await loadField();
    const field = new Field(stubCanvas(400, 300));
    field.trigger(1);
    assert.ok(advance(field, 0.5) > 0.05);

    const mq = globalThis.__media['(prefers-reduced-motion: reduce)'];
    mq.matches = true;
    mq.dispatch('change', { matches: true });
    assert.equal(field.reduced, true);
    assert.equal(field._peak(), 0, 'the field kept moving after reduced motion was turned on');
    field.trigger(1);
    assert.equal(advance(field, 0.5), 0);

    mq.matches = false;
    mq.dispatch('change', { matches: false });
    field.trigger(1);
    assert.ok(advance(field, 0.5) > 0.05, 'turning it off again must bring the field back');
    field.destroy();
    assert.equal(mq.listeners('change'), 0, 'destroy must unhook the media query');
  } finally { teardown(); }
});

test('a frame redraws only the dots whose level changed', async () => {
  /* The cost that matters while music plays. A frame used to copy the whole
   * baked ground and then blit every moving dot again, whether or not it had
   * moved since the last frame. Each sprite covers its own cell exactly and
   * opaquely, so drawing only the changed ones gives the same pixels. */
  const teardown = installDom();
  try {
    const { Field } = await loadField();
    const canvas = stubCanvas(400, 300);
    const field = new Field(canvas);
    field.trigger(1);
    advance(field, 1);
    field.draw();
    const ctx = canvas._ctx;
    ctx.blits = 0;
    field.draw();
    assert.equal(ctx.blits, 0, `an unchanged field redrew ${ctx.blits} sprites`);

    field._step();
    ctx.blits = 0;
    field.draw();
    assert.ok(ctx.blits > 0, 'a step that moved the field drew nothing');
    assert.ok(ctx.blits < field.rows * field.cols, 'one step redrew every dot');
  } finally { teardown(); }
});

test('the static CSS grain is the design\'s own uniform-noise tile', () => {
  /*
   * NO SILENT MISMATCH WITH THE DESIGN. The fallback ground on body used an SVG
   * feTurbulence -- FRACTAL noise, which the design rules out ("uniform white
   * noise, every pixel an independent draw ... not fractal") and which
   * feTurbulence cannot produce any other way. The design's bundle.css ships
   * the tile as a PNG; tokens.css carries the same bytes, used as a MASK over
   * --uv-deep so the colour still comes from the token.
   */
  const png = (css) => /url\("?(data:image\/png;base64,[A-Za-z0-9+/=]+)"?\)/.exec(css)?.[1];
  const design = png(readFileSync(BUNDLE, 'utf8'));
  assert.ok(design, 'no PNG noise tile found in the design\'s bundle.css');
  const tokens = (readFileSync(TOKENS.replace(/tokens\.css$/, 'tokens.gen.css'), 'utf8') + readFileSync(TOKENS, 'utf8'));
  assert.equal(png(tokens), design, 'tokens.css must carry the design\'s tile, byte for byte');
  const rules = tokens.replace(/\/\*[\s\S]*?\*\//g, '');
  assert.doesNotMatch(rules, /fractalNoise|feTurbulence/, 'the fractal fallback is still there');
});

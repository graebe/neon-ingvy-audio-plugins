/*
 * The animated ground's simulation.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * TWO KINDS OF CLAIM HERE, and they are different in status.
 *
 * The first is that this is still a PORT. src/lib/field.js was copied from the
 * design system's own reference implementation, and the numbers in it are the
 * design's parameter table -- so the test reads the reference's defaults out of
 * design/files/project/components/ground.js and diffs them. That is the same
 * argument the token guard makes about colours, applied to the one other place
 * the design system hands us values: a generator would overwrite a
 * disagreement, a test names it.
 *
 * The second is that the physics behaves: a kick raises the field, the field
 * rings out to EXACTLY zero rather than nearly zero, boxes are solid, and the
 * Motion switch stops it dead. Those are the properties the design's Motion
 * section promises a user, and none of them is visible in a screenshot.
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
const ROOT = join(HERE, '..', '..');
const REFERENCE = join(ROOT, 'design', 'files', 'project', 'components', 'ground.js');
const TOKENS = join(HERE, '..', 'src', 'tokens.css');
const COMPONENTS = join(HERE, '..', 'src', 'components.css');

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

function stubCanvas(w, h) {
  const ctx = stubContext();
  return {
    clientWidth: w,
    clientHeight: h,
    width: 0,
    height: 0,
    getContext: () => ctx,
    _ctx: ctx,
  };
}

/** The three colours Field reads, taken from the real tokens.css. */
function tokenValues() {
  const css = readFileSync(TOKENS, 'utf8');
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
  globalThis.document = {
    documentElement: { __root: true },
    createElement: () => stubCanvas(96, 96),
  };
  globalThis.getComputedStyle = () => ({
    getPropertyValue: (name) => values[name] ?? '',
  });
  globalThis.matchMedia = () => ({ matches: reducedMotion });
  globalThis.devicePixelRatio = dpr;
  /* Returns a live handle and never calls back: every test below steps the
   * simulation itself, so a frame that fired on its own would make the number of
   * steps depend on how busy the machine was. */
  globalThis.requestAnimationFrame = () => 1;
  globalThis.cancelAnimationFrame = () => {};
  return () => {
    for (const k of ['document', 'getComputedStyle', 'matchMedia', 'devicePixelRatio',
      'requestAnimationFrame', 'cancelAnimationFrame']) {
      if (k in saved) globalThis[k] = saved[k]; else delete globalThis[k];
    }
  };
}

/** Import Field with the DOM already in place. */
async function loadField() {
  const { Field, DEFAULTS, LEVELS, MID } = await import('../src/lib/field.js');
  return { Field, DEFAULTS, LEVELS, MID };
}

/** Step the simulation `secs` of simulated time, ignoring the frame clock. */
function advance(field, secs) {
  const steps = Math.round(secs / field.o.dt);
  for (let i = 0; i < steps; i++) field._step();
  return field._peak();
}

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
     * by comparing nothing. */
    assert.ok(Object.keys(reference).length >= 17,
      `only parsed ${Object.keys(reference).length} reference parameters`);

    for (const [name, value] of Object.entries(reference)) {
      assert.equal(DEFAULTS[name], value,
        `${name} is ${DEFAULTS[name]} here and ${value} in the design system's ` +
        'ground.js -- the field is a port, so this is either a transcription ' +
        'error or a design change that has not been recorded');
    }
  } finally { teardown(); }
});

/* ---------- the physics ---------- */

test('a kick raises the field and it rings out to exactly zero', async () => {
  const teardown = installDom();
  try {
    const { Field } = await loadField();
    const field = new Field(stubCanvas(400, 300));

    assert.equal(field._peak(), 0, 'a fresh field is flat');
    field.trigger(1);
    /* The wavelet peaks a little after the trigger, not on it. */
    const peak = advance(field, 0.5);
    assert.ok(peak > 0.05, `a full-strength kick barely moved the field (${peak})`);

    /* tau is 3.5 s and the design promises the whole field, reflections
     * included, is at rest in about twenty. */
    const late = advance(field, 25);
    assert.ok(late < field.o.rest,
      `after 25 s the field should be under rest (${field.o.rest}); it is ${late}`);

    /* And `_tick` is what takes it from "under rest" to EXACTLY zero -- the
     * design requires a still frame to be pixel-identical to the static ground,
     * and 1e-9 of wave is a dot drawn at the wrong level. */
    field._tick();
    assert.equal(field._peak(), 0, 'the field did not return to exactly zero');
    assert.equal(field.raf, 0, 'the render loop is still running at rest');
  } finally { teardown(); }
});

test('a kick is stronger than a quiet one, and strength is clamped to 0..1', async () => {
  const teardown = installDom();
  try {
    const { Field } = await loadField();
    const peakFor = (s) => {
      const field = new Field(stubCanvas(400, 300));
      field.trigger(s);
      return advance(field, 0.5);
    };
    assert.ok(peakFor(1) > peakFor(0.3), 'a full kick must move the field more than a weak one');
    /* Out-of-range input is clamped rather than trusted: the detector promises
     * 0.3..1 but the field is what has to survive a bug upstream. */
    assert.equal(peakFor(5), peakFor(1), 'a strength above 1 is not clamped');
    assert.equal(peakFor(-1), peakFor(0), 'a negative strength is not clamped');
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

test('a window with a box has more sources than one without', async () => {
  const teardown = installDom();
  try {
    const { Field } = await loadField();
    const field = new Field(stubCanvas(400, 300));
    const borderOnly = field.src.length;
    assert.ok(borderOnly > 0, 'the window border alone must emit');

    field.setSources([{ x: 120, y: 90, w: 160, h: 120 }]);
    assert.ok(field.src.length > borderOnly,
      'a panel adds emitting edges; the source count did not grow');

    /* And a zero-area box adds nothing -- Ground filters those out, but the
     * field must not be the thing that depends on it. */
    field.setSources([{ x: 10, y: 10, w: 0, h: 0 }]);
    assert.equal(field.src.length, borderOnly,
      'a zero-area box changed the source set');
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
    assert.equal(field.kicks.length, 0, 'a pending kick survived Motion being switched off');

    /* And it stays off: a trigger while disabled must do nothing at all. */
    field.trigger(1);
    assert.equal(advance(field, 0.5), 0, 'a kick fired while Motion was off');
  } finally { teardown(); }
});

test('prefers-reduced-motion disables the field outright', async () => {
  const teardown = installDom({ reducedMotion: true });
  try {
    const { Field } = await loadField();
    const field = new Field(stubCanvas(400, 300));
    assert.equal(field.reduced, true);
    field.trigger(1);
    assert.equal(advance(field, 1), 0,
      'the field moved with prefers-reduced-motion set -- the design makes this a ' +
      'rule, not a default the Motion switch can override');
  } finally { teardown(); }
});

test('two kicks interfere rather than the second replacing the first', async () => {
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

    /* Superposition: the sources sum, so two kicks a tenth of a second apart
     * cannot leave the field exactly where one did. */
    assert.notEqual(two._peak(), single,
      'a second kick made no difference -- the sources are replacing rather than summing');
    assert.equal(two.kicks.length > 0 || two._peak() > 0, true);
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

test('destroy stops the loop and drops pending kicks', async () => {
  const teardown = installDom();
  try {
    const { Field } = await loadField();
    const field = new Field(stubCanvas(400, 300));
    field.trigger(1);
    field.destroy();
    assert.equal(field.raf, 0);
    assert.equal(field.kicks.length, 0);
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
   * And that is invisible. A field at rest is pixel-identical to the static CSS
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

/*
 * The step grid, as a v8ui. Copyright (c) 2026 Torben Gräber. MIT.
 *
 * This is the thing the whole Max for Live device exists for: the pattern,
 * drawn and edited INSIDE Live's chain. No VST3, AU or CLAP can put it there
 * -- Live gives a plugin a floating window and the device row a generic knob
 * strip -- so a step sequencer's one essential picture is the one thing the
 * chain cannot show. Here it can.
 *
 * WHAT IS NOT IN THIS FILE, and deliberately:
 *
 *   the readout decoder  ../../trance-gate/ui/src/lib/readout.js, shared with
 *                        the WebView editor and pinned to a fixture the
 *                        engine prints.
 *   layout and hit-test  ./layout.js, which has no Max object in it and is
 *                        tested in ui/test/layout.test.mjs.
 *   the palette          ./tokens.js, checked against ui-kit's tokens.css.
 *
 * What is left here is only the part that needs Max: paint, the mouse, and
 * the two outlets. That is the same seam wire.c draws on the C side, for the
 * same reason -- a test that needs Live is a test nobody runs.
 *
 * NO ENVELOPE HERE, AND THAT IS THE 169 PIXELS TALKING. The WebView editor
 * draws the gate's shape under the pads from curves.js; in a device row there
 * is no band to put it in once the pads and a row of live.* controls have
 * their space. curves.js is therefore NOT imported -- when a fold-out view
 * earns the room for the plot, it is the file to reach for, unmodified, and
 * it is already pinned to a table the ENGINE generates.
 *
 * v8ui, NOT jsui: jsui's engine is ES5 and could not import any of the above.
 * Max 9 is what Live 12 Suite bundles (9.1.5 here), so the import list above
 * is both the reason this is a v8ui and the reason it works.
 */
import { parseReadout } from '../../trance-gate/ui/src/lib/readout.js';
import { padBoxes, stepAt, depthAt, playheadAt, PADS } from './layout.js';
import { T, PAD_DIP } from './tokens.js';

/* MODE, as the engine's `step` key takes it. Mirrors steps.js in the WebView
 * editor, which cannot be imported: it reaches for @ultraviolet/ui's message
 * transport, and there is no such thing inside a v8ui. */
const MODE = { off: 0, on: 1, tie: 2 };

/* eslint-disable no-undef -- mgraphics, outlet and box are v8ui globals. */

mgraphics.init();
mgraphics.relative_coords = 0;   /* pixels, which is what layout.js speaks */
mgraphics.autofill = 0;

/* The last good readout. NEVER overwritten by a failed decode: parseReadout
 * returns null rather than a partial object precisely so this can survive
 * one, and a confident empty pattern drawn over a real one reads as lost
 * work rather than as a version skew. */
let ui = null;

/* Re-anchored on every readout, so the interpolated playhead can never drift
 * further than one qmetro tick from the engine. */
let anchor = null;

/* The gesture in flight. A drag sets depth; the click that began it has
 * already toggled the step, which is the WebView editor's rule and is worth
 * keeping identical -- muscle memory should survive the shell. */
let dragging = -1;

function width()  { return box.rect[2] - box.rect[0]; }
function height() { return box.rect[3] - box.rect[1]; }

/* ------------------------------------------------------------------ */
/* In                                                                   */

/** `ui <blob>` from tg.gate~. The only way the pattern arrives. */
function anything() {
  if (messagename !== 'ui') return;
  const decoded = parseReadout(arrayfromargs(arguments).join(' '));
  if (!decoded) return;
  ui = decoded;
  anchor = {
    phase: decoded.phase,
    msStep: decoded.msStep,
    length: decoded.length,
    moving: decoded.moving,
    at: Date.now(),
  };
  mgraphics.redraw();
}
anything.local = 1;

/* ------------------------------------------------------------------ */
/* Paint                                                                */

const setColor = (c) => mgraphics.set_source_rgba(c[0], c[1], c[2], c[3]);

function paint() {
  const w = width(), h = height();

  setColor(T.bg000);
  mgraphics.rectangle(0, 0, w, h);
  mgraphics.fill();

  if (!ui) return;

  const n = ui.length;
  const boxes = padBoxes(w, n);
  const padH = Math.min(PADS.h, h);
  const head = playheadAt(anchor, Date.now());

  for (let i = 0; i < n; i++) {
    const { x, w: pw } = boxes[i];
    if (pw <= 0) continue;

    const on = ui.steps[i] && !ui.ties[i];
    const tie = ui.ties[i];

    /*
     * THE WELL. Every fourth step is drawn on the rail colour rather than
     * the well colour, so BARS READ WITHOUT NUMBERS -- which is why there
     * are no numbers. The WebView editor does this with a border; at this
     * width a border would be most of the pad, so it is the ground instead.
     */
    setColor(!on && !tie && i % 4 === 0 ? T.line100 : T.bg200);
    mgraphics.rectangle(x, 0, pw, padH);
    mgraphics.fill();

    if (on) {
      /* THE AMOUNT IS THE LIT HEIGHT, FROM THE BOTTOM. A lit height reading
       * as a level is how every step sequencer works and what the hardware
       * does. Never less than 2px: a step that sounds must look like one. */
      const lit = Math.max(2, Math.round(padH * ui.depths[i]));
      setColor(T.uv);
      mgraphics.rectangle(x, padH - lit, pw, lit);
      mgraphics.fill();

      if (head === i) {
        /* Darken the lit part rather than draw over it -- on-uv at a quarter
         * alpha, so it reads as a shadow crossing the row. NOT uv-deep,
         * which the system reserves for glow and never uses as a fill. */
        setColor(PAD_DIP);
        mgraphics.rectangle(x, padH - lit, pw, lit);
        mgraphics.fill();
      }
    } else if (tie) {
      /* A tie is a hollow uv outline with a bar across it: the step holds
       * through the previous one, so it is not a fill. */
      setColor(T.uvDeep);
      mgraphics.set_line_width(1);
      mgraphics.rectangle(x + 0.5, 0.5, pw - 1, padH - 1);
      mgraphics.stroke();
      setColor(T.uv);
      mgraphics.rectangle(x + 2, padH / 2 - 1, pw - 4, 2);
      mgraphics.fill();
    }

    /* The playhead over an unlit pad still has to be visible. */
    if (head === i && !on) {
      setColor(T.uvDeep);
      mgraphics.rectangle(x, padH - 2, pw, 2);
      mgraphics.fill();
    }

    /* The step being edited, bracketed above the row. */
    if (ui.cursor === i) {
      setColor(T.ink);
      mgraphics.rectangle(x, padH, pw, 2);
      mgraphics.fill();
    }
  }
}

/* ------------------------------------------------------------------ */
/* The mouse                                                            */

/*
 * CLICK TOGGLES, SHIFT-CLICK TIES, DRAG SETS THE AMOUNT -- identical to the
 * WebView editor and to the Move, because the gesture is the part a user
 * carries between them. docs/live.md states the same table.
 *
 * The toggle is computed from the LAST READOUT rather than from a local
 * mirror: the engine is authoritative about what a step currently is, and a
 * UI that decides for itself is how "it toggled on once and then did
 * nothing" happens -- which this repository has already had reported once,
 * against Join Neighbors.
 */
function onclick(x, y, button, mod1, shift) {
  if (!ui) return;
  const i = stepAt(x, width(), ui.length);
  if (i < 0) return;

  outlet(0, 'cursor', i);

  if (shift) {
    outlet(0, 'step', i, ui.ties[i] ? MODE.off : MODE.tie);
  } else {
    outlet(0, 'step', i, ui.steps[i] && !ui.ties[i] ? MODE.off : MODE.on);
  }
  dragging = i;
}
onclick.local = 1;

function ondrag(x, y, button) {
  if (!ui || dragging < 0 || !button) { if (!button) dragging = -1; return; }
  /* The step is the one the gesture STARTED on. Following the pointer
   * sideways would let a drag rewrite the whole row on the way past, which
   * is never what the hand meant. */
  const v = depthAt(y, Math.min(PADS.h, height()));
  outlet(0, 'depth', dragging, Math.round(v * 255));
}
ondrag.local = 1;

function onidle() {}
onidle.local = 1;

/*
 * REPAINTED ON A TICK, NOT ON A MESSAGE. The playhead moves continuously and
 * the readout arrives at whatever rate the patcher's qmetro asks for; between
 * them the position is carried by wall time (playheadAt). Repainting only on
 * arrival would make the playhead as jerky as the poll.
 */
function bang() {
  if (anchor && anchor.moving) mgraphics.redraw();
}
bang.local = 1;

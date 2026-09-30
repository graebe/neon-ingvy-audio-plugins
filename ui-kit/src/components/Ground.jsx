/*
 * The window's animated ground.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * THE DESIGN SYSTEM GIVES EVERY WINDOW ONE OF THESE, and it is the only thing in
 * the whole system that animates. `lib/field.js` is the simulation, ported from
 * the design's own reference; this file is the four things a Solid editor has to
 * do around it: own the canvas, keep the field's idea of the window's size and
 * boxes current, pass the Motion switch through, and stop cleanly.
 *
 * WHERE IT GOES. First child of the window element, which in this repository is
 * each editor's `<main>`. It is `position: absolute; inset: 0` with `z-index: 0`,
 * so it fills the window and everything else paints over it -- see the `.ground`
 * rule in components.css, and the note there about the one thing each editor's
 * app.css has to provide.
 *
 * THE SOURCES ARE MEASURED FROM THE DOM, not declared. The design says the boxes
 * that emit and reflect are the panels, grids, wells and rings the window
 * happens to contain, so asking the document is the statement that stays true
 * when a layout changes. A selector list is overridable because the editors do
 * not agree on class names -- the Spectrogram's picture is a `.spectro`, the
 * Side-Chain's is a `.plot` -- and a box the field should respect but that
 * matches nothing gets `data-wave-source`.
 *
 * WHY A ResizeObserver AND NOT A WINDOW RESIZE LISTENER. Two of these editors
 * resize themselves from the UI (the Trance Gate grows with its rows, the
 * Side-Chain with its shaper) by telling the plugin to resize the host window.
 * That changes `main`'s box without necessarily firing a window resize the
 * canvas would see in the right order, and a field whose grid disagrees with its
 * canvas draws the ground at the wrong pitch. Observing the element asks about
 * the thing that actually matters.
 *
 * WHAT IT DOES NOT DO: idle. There is no animation loop running when nothing has
 * happened -- `Field` stops its own loop once the field is at rest, and at rest
 * the canvas is pixel-identical to the static CSS ground underneath it.
 */

import { onMount, onCleanup, createEffect } from 'solid-js';

import { Field } from '../lib/field.js';

/*
 * The boxes that emit and reflect, by default.
 *
 * `.panel` and `.plot` are this repository's spellings of the design system's
 * `.ph-panel` and its wells; `[data-wave-source]` is the escape hatch for a box
 * that is one of these in the design's sense but does not carry the class.
 */
const DEFAULT_SOURCES = '.panel, .plot, .well, .grid, .ring, [data-wave-source]';

/**
 * @param props.enabled   the Motion switch. Default true.
 * @param props.sources   a selector list for the emitting/reflecting boxes.
 * @param props.ref       called with a handle: `{ trigger(strength) }`.
 */
export function Ground(props) {
  let canvas;
  let field = null;
  let observer = null;

  /*
   * Measure the boxes RELATIVE TO THE CANVAS. Both rects come from
   * getBoundingClientRect, so subtracting one from the other is the same
   * coordinate system the field's grid uses -- CSS px from the canvas's top
   * left -- with no assumption about where either sits on the page.
   */
  const measure = () => {
    if (!field || !canvas) return;
    const root = canvas.parentElement;
    if (!root) return;
    const base = canvas.getBoundingClientRect();
    const selector = props.sources || DEFAULT_SOURCES;
    const rects = Array.from(root.querySelectorAll(selector))
      .map((el) => {
        const b = el.getBoundingClientRect();
        return { x: b.left - base.left, y: b.top - base.top, w: b.width, h: b.height };
      })
      /* A box with no area is a box that is not laid out yet (a collapsed
       * panel, a hidden tab). Passing it through would wall off a single grid
       * node at the origin, which reads as one dot that never moves. */
      .filter((b) => b.w > 0 && b.h > 0);
    field.setSources(rects);
  };

  onMount(() => {
    /*
     * THE GROUND MAY NEVER TAKE THE EDITOR DOWN WITH IT, and that is not
     * defensive habit -- it is a specific failure this had.
     *
     * Solid mounts children before parents, so this runs BEFORE the editor's own
     * onMount. A throw here -- a token missing from the stylesheet, a WebView
     * that refuses a 2D context or `createImageData` -- would therefore stop the
     * editor from ever registering `onMessage`, and the window would render once
     * and then sit there completely dead: no meters, no readouts, no parameter
     * updates, and nothing on screen to say why.
     *
     * That is a catastrophic price for a decoration. The design system already
     * names the fallback: the static CSS ground on `body`, which this canvas
     * merely draws over. So a failure here leaves the canvas transparent, the CSS
     * ground showing through it, and the editor entirely intact.
     *
     * The console line is the whole diagnosis, and it is enough: the review
     * harness and a WebView inspector both show it, and `field.js` still THROWS
     * on a missing token rather than guessing, so ui-kit/test/field.test.mjs
     * catches a stylesheet bug long before a plugin gets here.
     */
    try {
      field = new Field(canvas);
      /* The field's own constructor sizes itself and paints the ground at rest,
       * so there is a correct picture before any of the below runs. */
      measure();
    } catch (err) {
      field = null;
      console.error('Ground: the animated background is off -', err);
    }

    const root = canvas.parentElement;
    if (field && root && globalThis.ResizeObserver) {
      /*
       * ONE OBSERVER ON THE WINDOW, NOT ONE PER BOX. What the field needs is to
       * be told "the layout moved"; which box moved does not change the work,
       * because `measure` re-reads all of them anyway. Observing `main` catches
       * a host resize, and observing the boxes as well would only add callbacks
       * that do the same thing several times in one frame.
       */
      observer = new ResizeObserver(() => {
        /* `field` is re-read rather than captured: a later failure must not turn
         * a resize into a second, louder error. */
        field?.resize();
        measure();
      });
      observer.observe(root);
    }

    props.ref?.({
      /* The only way in. A kick arrives as a message from the plugin; the editor
       * hands the strength straight through. */
      trigger: (strength) => field?.trigger(strength),
    });
  });

  /* The Motion switch. `enabled` defaults to true so that a window which has not
   * wired one still shows the design's ground rather than a dead canvas --
   * though the design does require the switch, and every editor here has one. */
  createEffect(() => {
    const on = props.enabled ?? true;
    field?.setEnabled(on);
  });

  onCleanup(() => {
    observer?.disconnect();
    observer = null;
    field?.destroy();
    field = null;
  });

  /*
   * aria-hidden, and it is not laziness. The ground carries no information -- it
   * is the same picture whatever the plugin is doing, and its only content is
   * that a kick happened, which the user can hear. A screen reader announcing a
   * canvas here would be announcing decoration.
   */
  return <canvas class="ground" ref={canvas} aria-hidden="true" />;
}

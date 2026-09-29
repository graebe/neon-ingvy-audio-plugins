/*
 * The rolling picture.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * TWO CANVASES, AND THE SECOND ONE IS WHY THE SCROLL IS FREE.
 *
 * The obvious way to scroll a spectrogram is to shift the visible canvas left by
 * a pixel and draw the new column at the right edge. That is a full-canvas
 * read-modify-write every frame, and it accumulates: each shift resamples what
 * the last shift already resampled, so after a few seconds the old end of the
 * picture is smeared.
 *
 * So the history is a separate canvas, BANDS tall and COLS wide, written as a
 * RING: each column goes at a cursor that wraps, and nothing ever moves. The
 * visible canvas is then two drawImage calls -- the part after the cursor (the
 * old columns) and the part before it (the new ones) -- which is the same
 * picture with no pixel touched twice.
 *
 * At the shipped sizes there is no stretch at all -- 256 bands into 256 pixels,
 * 606 columns into 606 -- so drawImage is a straight copy. Anything else is left
 * to its own smoothing rather than resampled here, which would invent detail the
 * analysis does not have.
 *
 * PAUSE IS A REPAINT GATE AND NOTHING MORE. Columns keep arriving and keep being
 * written into the history behind the frozen picture, so unpausing shows an
 * up-to-date view with the paused seconds IN it rather than cut out of it. The
 * only thing that stops is `paint`.
 */
import { onMount, onCleanup, createEffect } from 'solid-js';
import { buildLut, stopCss } from './ramp.js';

export default function Spectrogram(props) {
  /* props: width, height (CSS px), cols (the history depth in columns),
   *        batch (the newest {bands, count, data}), scale (page zoom) */
  let view;          /* the visible canvas */
  let hist;          /* the ring of columns, bands x cols */
  let histCtx;
  let viewCtx;
  let lut;
  let strip;         /* one ImageData column, reused */
  /*
   * THE FROZEN PICTURE, KEPT SOMEWHERE OF ITS OWN.
   *
   * While paused, the only copy of what is on screen is the visible canvas --
   * and resizing a canvas CLEARS it, which `setupView` does whenever the host
   * rescales the window or it moves to a display with a different pixel ratio.
   * Without this snapshot, dragging a paused editor to another screen repaints
   * it live: the freeze failing silently, which is the worst way for it to fail.
   */
  let frozen;
  let cursor = 0;    /* the next column to write, and the view's left edge */
  let bands = 0;

  const COLS = () => props.cols;

  /* THE BACKING STORE IS SIZED IN DEVICE PIXELS, and the page's own zoom is
   * part of that. The window is scaled with a CSS transform (see App.jsx), and a
   * canvas scaled by CSS is resampled by the compositor -- so a 1.5x window drew
   * a soft picture with visibly fat columns until the zoom reached here. */
  const setupView = () => {
    if (!view) return;
    const dpr = (window.devicePixelRatio || 1) * (props.scale || 1);
    const w = Math.max(1, Math.round(props.width * dpr));
    const h = Math.max(1, Math.round(props.height * dpr));
    if (view.width !== w || view.height !== h) {
      view.width = w;
      view.height = h;
    }
    viewCtx = view.getContext('2d');
    viewCtx.setTransform(dpr, 0, 0, dpr, 0, 0);
    /* Smoothing ON: the only scaling is vertical (bands to pixels) and the
     * horizontal axis is 1:1 by construction, so this softens the frequency
     * axis and nothing else. */
    viewCtx.imageSmoothingEnabled = true;
    paint();
  };

  /* Rebuilt whenever the band count changes, which in practice is once -- but a
   * host that reopens the editor across a sample-rate change gets a new axis,
   * and the history it held was measured against the old one. */
  const setupHistory = (n) => {
    bands = n;
    hist = document.createElement('canvas');
    hist.width = COLS();
    hist.height = bands;
    histCtx = hist.getContext('2d', { alpha: false });
    strip = histCtx.createImageData(1, bands);
    cursor = 0;

    /* Filled with the floor colour rather than left transparent, so an empty
     * picture is the same black as a silent one -- not a hole showing the well
     * through it. Taken from the stylesheet as written: assembling an rgb()
     * from the LUT here would be a colour spelled outside @ultraviolet/ui's tokens.css. */
    histCtx.fillStyle = stopCss(0);
    histCtx.fillRect(0, 0, hist.width, hist.height);
  };

  const writeColumn = (data, offset) => {
    const px = strip.data;
    for (let b = 0; b < bands; b++) {
      /*
       * BAND 0 IS THE LOWEST FREQUENCY AND BELONGS AT THE BOTTOM. ImageData row
       * 0 is the top, so the picture is written upside down on purpose; drawn
       * the other way the bass sits along the top edge and every reader reads
       * the picture wrong before noticing why.
       */
      const row = (bands - 1 - b) * 4;
      const v = data[offset + b] * 3;
      px[row] = lut[v];
      px[row + 1] = lut[v + 1];
      px[row + 2] = lut[v + 2];
      px[row + 3] = 255;
    }
    histCtx.putImageData(strip, cursor, 0);
    cursor = cursor + 1 === COLS() ? 0 : cursor + 1;
  };

  /** Copy what is on screen, so a resize while paused can put it back. */
  const freeze = () => {
    if (!view) return;
    frozen = frozen ?? document.createElement('canvas');
    frozen.width = view.width;
    frozen.height = view.height;
    frozen.getContext('2d').drawImage(view, 0, 0);
  };

  const paint = () => {
    if (!viewCtx || !hist) return;
    const { width: w, height: h } = props;

    /* Paused: the snapshot, not the history -- which has scrolled on behind it
     * and is no longer what the user is looking at. */
    if (props.paused && frozen) {
      viewCtx.drawImage(frozen, 0, 0, frozen.width, frozen.height, 0, 0, w, h);
      return;
    }
    const cols = COLS();
    /* The oldest column is the one about to be overwritten: the cursor is both
     * the write head and the left edge of the view. */
    const tail = cols - cursor;          /* columns after the cursor: the old ones */
    viewCtx.drawImage(hist, cursor, 0, tail, bands, 0, 0, (tail / cols) * w, h);
    if (cursor > 0) {
      viewCtx.drawImage(hist, 0, 0, cursor, bands, (tail / cols) * w, 0,
                        (cursor / cols) * w, h);
    }
  };

  onMount(() => {
    lut = buildLut();
    setupHistory(props.batch?.bands || 256);
    setupView();
    window.addEventListener('resize', setupView);
  });
  onCleanup(() => window.removeEventListener('resize', setupView));

  /* The page's zoom is a prop, so a host resize repaints at the new resolution
   * rather than at the one the editor opened with. */
  createEffect(() => {
    void props.scale; void props.width; void props.height;
    setupView();
  });

  /*
   * The columns are written WHATEVER the pause state -- that is what "just the
   * view" means, and what makes unpausing instant rather than a restart.
   */
  createEffect(() => {
    const batch = props.batch;
    if (!batch || !histCtx) return;
    if (batch.bands !== bands) setupHistory(batch.bands);
    for (let c = 0; c < batch.count; c++) writeColumn(batch.data, c * batch.bands);
    if (!props.paused) paint();
  });

  /* Pausing snapshots; unpausing repaints at once rather than waiting for the
   * next column, which at ~47 a second would be a visible hesitation. */
  let wasPaused = false;
  createEffect(() => {
    const now = !!props.paused;
    if (now === wasPaused) return;
    wasPaused = now;
    if (now) freeze();
    else paint();
  });

  /* A new range means every column in the history is an answer about other
   * frequencies. Clearing is the honest thing to do with them. */
  createEffect(() => {
    void props.generation;
    if (!histCtx) return;
    histCtx.fillStyle = stopCss(0);
    histCtx.fillRect(0, 0, hist.width, hist.height);
    cursor = 0;
    paint();
  });

  return (
    <canvas
      ref={view}
      class="spectro-canvas"
      style={{ width: `${props.width}px`, height: `${props.height}px` }}
    />
  );
}

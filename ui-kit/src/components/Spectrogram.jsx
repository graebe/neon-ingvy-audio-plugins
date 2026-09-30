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
 *
 * AND SO IS THE VIEW SWITCH, FOR THE SAME REASON AND BY THE SAME MEANS.
 *
 * There are TWO pictures here, not one: the scrolling history above, and a
 * SWEEP whose columns are placed where a caller says rather than appended --
 * which is what lets an x-axis be the host's bars instead of the clock. Both
 * are written from every column, always, whichever one is on screen. So
 * switching between them is a choice of what to show and never a restart: the
 * other picture was being kept the whole time and is complete the moment it
 * appears.
 *
 * The component does not know what a bar IS. It is handed a slot per column and
 * draws there; what a slot means is the caller's, which is the same seam every
 * control in this kit follows.
 */
import { onMount, onCleanup, createEffect, untrack, createSignal, Show } from 'solid-js';
import { buildLut, readRgb, stopCss } from '../lib/ramp.js';
import { useFrame } from './EditorFrame.jsx';

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
  /* The levels as they were when the picture froze, and the cursor that
   * oriented them -- the crosshair's half of the snapshot. */
  let frozenLevels;
  let frozenCursor = 0;
  let cursor = 0;    /* the next column to write, and the view's left edge */
  let bands = 0;
  /*
   * THE LEVELS, KEPT AS NUMBERS ALONGSIDE THE PICTURE.
   *
   * The canvas holds colour, and colour is lossy: the ramp maps 256 levels onto
   * five interpolated stops, so reading a pixel back and inverting the LUT would
   * answer a question about the PALETTE rather than about the audio. The
   * crosshair has to report what the analyzer measured, so the bytes are kept
   * as they arrived -- one ring, same geometry, 606 x 256 = 155 KB.
   */
  let levels;
  /*
   * THE SWEEP: the same geometry, indexed by SLOT rather than by arrival.
   *
   * A second canvas rather than a second way of reading the first, because the
   * two disagree about what a column's x means -- one is "how long ago", the
   * other "where in the bar" -- and a single buffer would have to be rewritten
   * end to end every time the answer changed.
   */
  let sweep;
  let sweepCtx;
  let sweepLevels;
  /*
   * THE CLASH, ON ITS OWN LAYER AND IN ITS OWN COLOUR.
   *
   * It is not part of the picture, it is a reading OF the picture, so it is
   * composited over rather than mixed in -- which also means the ramp
   * underneath keeps meaning level, and the system's one-hue rule survives a
   * second colour appearing on the canvas.
   *
   * Kept per view, like the picture, because the same cell is at a different x
   * in each: the scrolling view is indexed by arrival and the sweep by slot.
   */
  let clashHist;
  let clashHistCtx;
  let clashSweep;
  let clashSweepCtx;
  let clashStrip;
  /* The amber the overlay is drawn in, read back from the stylesheet exactly as
   * the ramp's stops are -- no colour may be spelled in here, not even as a
   * fallback. Null (no overlay) only if the token is missing, which the ramp
   * would already have thrown on. */
  let clashRgb = null;
  /* The last slot written, which is where the playhead goes, and the column
   * that was written there -- the gap filler interpolates from it. */
  let head = -1;
  let headCol;
  let host;          /* the positioned box the crosshair is drawn in */
  const [playhead, setPlayhead] = createSignal(-1);
  const [hover, setHover] = createSignal(null);
  let at = null;     /* the pointer, in picture pixels, or null */

  const COLS = () => props.cols;
  /* The page's zoom: a prop, or the scale of the EditorFrame it is drawn in. */
  const frame = useFrame();
  const zoom = () => props.scale ?? frame?.scale() ?? 1;
  /* 'bars' draws the sweep, anything else the scrolling history. Only ever a
   * choice of which finished picture to show: both are written regardless. */
  const isBars = () => props.view === 'bars';

  /* THE BACKING STORE IS SIZED IN DEVICE PIXELS, and the page's own zoom is
   * part of that. The window is scaled with a CSS transform (see App.jsx), and a
   * canvas scaled by CSS is resampled by the compositor -- so a 1.5x window drew
   * a soft picture with visibly fat columns until the zoom reached here. */
  const setupView = () => {
    if (!view) return;
    const dpr = (window.devicePixelRatio || 1) * zoom();
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
    levels = new Uint8Array(COLS() * bands);

    sweep = document.createElement('canvas');
    sweep.width = COLS();
    sweep.height = bands;
    sweepCtx = sweep.getContext('2d', { alpha: false });
    sweepLevels = new Uint8Array(COLS() * bands);

    clashHist = document.createElement('canvas');
    clashHist.width = COLS();
    clashHist.height = bands;
    clashHistCtx = clashHist.getContext('2d');
    clashSweep = document.createElement('canvas');
    clashSweep.width = COLS();
    clashSweep.height = bands;
    clashSweepCtx = clashSweep.getContext('2d');
    /* alpha:true on both, unlike the picture's -- a clash layer is mostly
     * nothing, and the nothing has to let the picture through. */
    clashStrip = clashHistCtx.createImageData(1, bands);

    headCol = new Uint8Array(bands);
    head = -1;
    setPlayhead(-1);

    cursor = 0;

    /* Filled with the floor colour rather than left transparent, so an empty
     * picture is the same black as a silent one -- not a hole showing the well
     * through it. Taken from the stylesheet as written: assembling an rgb()
     * from the LUT here would be a colour spelled outside @ultraviolet/ui's tokens.css. */
    histCtx.fillStyle = stopCss(0);
    histCtx.fillRect(0, 0, hist.width, hist.height);
    sweepCtx.fillStyle = stopCss(0);
    sweepCtx.fillRect(0, 0, sweep.width, sweep.height);
  };

  /*
   * BAND 0 IS THE LOWEST FREQUENCY AND BELONGS AT THE BOTTOM. ImageData row 0
   * is the top, so the picture is written upside down on purpose; drawn the
   * other way the bass sits along the top edge and every reader reads the
   * picture wrong before noticing why. fillStrip above is where that happens.
   */
  const writeColumn = (data, offset) => {
    fillStrip(data, offset);
    histCtx.putImageData(strip, cursor, 0);
    levels.set(data.subarray(offset, offset + bands), cursor * bands);
    cursor = cursor + 1 === COLS() ? 0 : cursor + 1;
  };

  /* One column of bytes into the strip, ready to be put somewhere. */
  const fillStrip = (src, offset) => {
    const px = strip.data;
    for (let b = 0; b < bands; b++) {
      const row = (bands - 1 - b) * 4;
      const v = src[offset + b] * 3;
      px[row] = lut[v];
      px[row + 1] = lut[v + 1];
      px[row + 2] = lut[v + 2];
      px[row + 3] = 255;
    }
  };

  /*
   * ONE COLUMN OF CLASH, AS ORANGE OVER WHATEVER IS BEHIND IT.
   *
   * INTENSITY IS ALPHA, not a second ramp. The cell keeps the picture's own
   * colour underneath and gains orange in proportion to how hard the two
   * sources are fighting there, so a reader can still see WHAT is clashing
   * rather than just that something is.
   *
   * AND THE EDGE IS DRAWN. A gradient alone has no boundary, so a broad shallow
   * clash and a narrow fierce one look like the same smudge at a glance. A cell
   * that is lit with a neighbour that is not gets full alpha, which gives the
   * region an outline for free -- no second pass, no marching squares, and it
   * costs one comparison per band.
   */
  const CLASH_EDGE = 12;   /* below this a cell is not "in" the region at all */

  const fillClashStrip = (data, offset, prev) => {
    const px = clashStrip.data;
    if (!clashRgb) { px.fill(0); return; }
    const [r, g, b] = clashRgb;
    for (let i = 0; i < bands; i++) {
      const row = (bands - 1 - i) * 4;
      const v = data[offset + i];
      px[row] = r;
      px[row + 1] = g;
      px[row + 2] = b;

      if (v < CLASH_EDGE) {
        px[row + 3] = 0;
        continue;
      }
      /* A neighbour outside the region -- above, below, or the column before --
       * makes this cell an edge. */
      const up = i + 1 < bands ? data[offset + i + 1] : 0;
      const down = i > 0 ? data[offset + i - 1] : 0;
      const back = prev ? prev[i] : 0;
      const edge = up < CLASH_EDGE || down < CLASH_EDGE || back < CLASH_EDGE;
      /* The gradient tops out short of opaque so the partial underneath stays
       * readable through it; the outline does not, because an outline that can
       * be seen through is not one. */
      px[row + 3] = edge ? 255 : Math.round((v / 255) * 200);
    }
  };

  /*
   * THE SWEEP WRITER, AND THE GAP IS THE INTERESTING PART.
   *
   * A column arrives every ~21 ms whatever the window is, so a picture 606
   * pixels wide spanning two seconds gets 94 columns for 606 slots: the slot
   * jumps five or six at a time and the pixels between them were never
   * measured.
   *
   * Held flat, those become blocks -- the same staircase the sub-bin bands had,
   * and the same answer: INTERPOLATE, and do it in bytes. A byte here is
   * already linear in dB (see the ramp and the engine's amplitude_to_byte), so
   * a straight line between two bytes IS a straight line in dB, which is the
   * axis the picture draws. Nothing is invented that a hold would not also have
   * claimed; it is claimed smoothly instead of in steps.
   */
  const writeSweep = (data, offset, slot) => {
    if (!sweepCtx || slot < 0) return;
    const cols = COLS();

    /* How far the head moved, the short way round the ring. A first column, or
     * one that has not moved on, is just written where it is. */
    let gap = head < 0 ? 0 : slot - head;
    if (gap < 0) gap += cols;              /* the wrap */
    /* A jump most of the way round is a seek, not a gap worth painting
     * through: drawing a ramp across it would invent a sweep that never
     * happened. */
    if (gap > cols / 2) gap = 0;

    for (let g = 1; g < gap; g++) {
      const t = g / gap;
      const at = (head + g) % cols;
      for (let b = 0; b < bands; b++) {
        const v = Math.round(headCol[b] + (data[offset + b] - headCol[b]) * t);
        const row = (bands - 1 - b) * 4;
        const c = v * 3;
        strip.data[row] = lut[c];
        strip.data[row + 1] = lut[c + 1];
        strip.data[row + 2] = lut[c + 2];
        strip.data[row + 3] = 255;
        sweepLevels[at * bands + b] = v;
      }
      sweepCtx.putImageData(strip, at, 0);
    }

    fillStrip(data, offset);
    sweepCtx.putImageData(strip, slot, 0);
    sweepLevels.set(data.subarray(offset, offset + bands), slot * bands);
    headCol.set(data.subarray(offset, offset + bands));
    head = slot;
  };

  /* The clash follows the picture: the same cursor, the same slot, so the two
   * layers can never disagree about where a moment is. */
  let clashPrev;
  const writeClash = (data, offset, slot) => {
    if (!clashHistCtx) return;
    fillClashStrip(data, offset, clashPrev);
    /* The scroll layer writes at the cursor the picture just left behind. */
    const at = cursor === 0 ? COLS() - 1 : cursor - 1;
    clashHistCtx.clearRect(at, 0, 1, bands);
    clashHistCtx.putImageData(clashStrip, at, 0);
    if (slot >= 0 && clashSweepCtx) {
      clashSweepCtx.clearRect(slot, 0, 1, bands);
      clashSweepCtx.putImageData(clashStrip, slot, 0);
    }
    if (!clashPrev || clashPrev.length !== bands) clashPrev = new Uint8Array(bands);
    clashPrev.set(data.subarray(offset, offset + bands));
  };

  /*
   * WHAT IS UNDER THE POINTER, IN THE COMPONENT'S OWN TERMS.
   *
   * It reports a band INDEX and a level BYTE, never a frequency or a decibel:
   * what those mean is the analyzer's, and the kit's rule is that a control
   * takes and returns numbers whose meaning belongs to the caller. App.jsx owns
   * the axis the plugin sent and turns these into Hz, seconds and dB.
   */
  const sample = () => {
    if (!at || !levels || !bands) return null;
    /* Paused: the snapshot's numbers, to match the snapshot's pixels. */
    const paused = props.paused && frozenLevels;
    const bars = isBars();
    const src = paused ? frozenLevels : (bars ? sweepLevels : levels);
    /* In the sweep a slot IS an x, so there is nothing to rotate past. */
    const origin = paused ? frozenCursor : (bars ? 0 : cursor);
    const cols = COLS();
    const px = Math.min(cols - 1, Math.max(0, Math.floor((at.x / props.width) * cols)));
    const row = Math.min(bands - 1, Math.max(0, Math.floor((at.y / props.height) * bands)));
    /* Band 0 is the bottom -- the same inversion writeColumn draws with. */
    const band = bands - 1 - row;
    /* The cursor is the left edge, so the slot is the cursor plus the offset. */
    const slot = (origin + px) % cols;
    return {
      x: at.x,
      y: at.y,
      band,
      /* Columns left of the newest, which is the one at the right edge. Only
       * meaningful in the scrolling view; the bar view reads `slot`, which the
       * caller turns into a bar and a beat because only it knows the metre. */
      age: cols - 1 - px,
      slot: px,
      level: src[slot * bands + band],
    };
  };

  /* One place, so a pointer move and a new column report the same way. */
  const report = () => {
    const s = sample();
    setHover(s);
    props.onHover?.(s);
  };

  const onPointerMove = (e) => {
    if (!host) return;
    const r = host.getBoundingClientRect();
    if (!(r.width > 0) || !(r.height > 0)) return;
    /*
     * MEASURED THROUGH THE BOUNDING RECT, so the page's CSS zoom needs no
     * arithmetic here: the host scales the whole window with a transform (see
     * App.jsx) and the rect is already in scaled pixels. Dividing by it and
     * multiplying by the logical size lands in picture coordinates whatever
     * the zoom.
     */
    at = {
      x: Math.min(props.width - 0.001, Math.max(0, ((e.clientX - r.left) / r.width) * props.width)),
      y: Math.min(props.height - 0.001, Math.max(0, ((e.clientY - r.top) / r.height) * props.height)),
    };
    report();
  };

  const onPointerLeave = () => {
    at = null;
    setHover(null);
    props.onHover?.(null);
  };

  /** Copy what is on screen, so a resize while paused can put it back. */
  const freeze = () => {
    if (!view) return;
    frozen = frozen ?? document.createElement('canvas');
    frozen.width = view.width;
    frozen.height = view.height;
    frozen.getContext('2d').drawImage(view, 0, 0);
    /*
     * THE NUMBERS FREEZE WITH THE PICTURE, and they have to.
     *
     * Pause shows a snapshot while the ring rolls on behind it. A crosshair
     * that kept reading the ring would name the level of a column that has
     * already left the screen -- the readout and the pixel under it describing
     * different moments, with nothing to show that they had parted.
     */
    const src = isBars() ? sweepLevels : levels;
    if (src) {
      frozenLevels = frozenLevels?.length === src.length
        ? frozenLevels : new Uint8Array(src.length);
      frozenLevels.set(src);
      /* The sweep's slots are already the picture's x, so it needs no rotation
       * to read back -- head 0 says "slot is x". */
      frozenCursor = isBars() ? 0 : cursor;
    }
  };

  /*
   * UNTRACKED, AND THAT IS THE WHOLE REASON PAUSE USED TO ERASE THE PICTURE.
   *
   * Solid tracks through the CALL STACK, not the lexical scope: a helper called
   * synchronously from inside a createEffect subscribes that effect to every
   * prop the helper happens to read. This one reads `paused`, `width`, `height`
   * and `cols`, and it is called from four effects -- including the one whose
   * entire job is to WIPE the history on a range change. So every click of
   * Pause, and every click of Resume, ran that wipe: thirteen seconds gone, the
   * cursor back to 0, and a near-black canvas refilling from the left edge on
   * resume. Which is the exact opposite of what the header above promises, and
   * of what ui/README.md and docs/live.md tell the user to expect.
   *
   * `width` and `height` are the same hazard one step back: they are literals
   * at the Spectrogram's only call site today, so the wipe never fired on a
   * resize -- but a caller that sized the picture from a signal would have hit
   * it, and nothing in the component said so.
   *
   * Untracking here rather than at each call site is deliberate: this is a
   * drawing helper, it is not the author of any dependency, and a fix spelled
   * four times is a fix that grows a fifth call site without one.
   */
  const paint = () => untrack(() => {
    if (!viewCtx || !hist) return;
    const { width: w, height: h } = props;

    /* Paused: the snapshot, not the history -- which has scrolled on behind it
     * and is no longer what the user is looking at. */
    if (props.paused && frozen) {
      viewCtx.drawImage(frozen, 0, 0, frozen.width, frozen.height, 0, 0, w, h);
      return;
    }
    /*
     * The sweep needs no un-wrapping: its slots ARE the picture's x, so it goes
     * out in one copy. The scroll's ring has to be rotated into place, which is
     * the two calls below -- so the bar view is strictly less drawing, not more.
     */
    if (isBars()) {
      viewCtx.drawImage(sweep, 0, 0, sweep.width, bands, 0, 0, w, h);
      if (props.clash && clashSweep) {
        viewCtx.drawImage(clashSweep, 0, 0, clashSweep.width, bands, 0, 0, w, h);
      }
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
    /* The overlay is rotated the same way, or the orange would sit over the
     * wrong moment the instant the ring wrapped. */
    if (props.clash && clashHist) {
      viewCtx.drawImage(clashHist, cursor, 0, tail, bands, 0, 0, (tail / cols) * w, h);
      if (cursor > 0) {
        viewCtx.drawImage(clashHist, 0, 0, cursor, bands, (tail / cols) * w, 0,
                          (cursor / cols) * w, h);
      }
    }
  });

  onMount(() => {
    lut = buildLut();
    /* Read back rather than spelled: no colour may be written in here, and the
     * token guard enforces it -- a runtime-built rgb() string would be caught
     * too. stopCss's own trick, on a different custom property. */
    clashRgb = readRgb('--amber');
    if (!clashRgb) console.error('Spectrogram: --amber is not a hex colour; the clash overlay is off');
    setupHistory(props.batch?.bands || 256);
    setupView();
    window.addEventListener('resize', setupView);
  });
  onCleanup(() => window.removeEventListener('resize', setupView));

  /* The page's zoom is a prop, so a host resize repaints at the new resolution
   * rather than at the one the editor opened with. */
  createEffect(() => {
    void zoom(); void props.width; void props.height;
    setupView();
  });

  /*
   * The columns are written WHATEVER the pause state -- that is what "just the
   * view" means, and what makes unpausing instant rather than a restart.
   *
   * A NEW BATCH IS THE ONLY THING THIS WATCHES, and the untrack below is what
   * says so. `props.paused` is read here to decide whether to repaint, and a
   * plain read subscribed the effect to it -- so a pause re-ran this with the
   * SAME batch object still in `props.batch` and wrote its columns a second
   * time, and the resume wrote them a third. Every pause/resume cycle stamped
   * two duplicate columns into the ring: a stutter in the picture at exactly
   * the moment the user was told nothing would happen.
   */
  createEffect(() => {
    const batch = props.batch;
    if (!batch || !histCtx) return;
    untrack(() => {
      if (batch.bands !== bands) setupHistory(batch.bands);
      for (let c = 0; c < batch.count; c++) {
        const off = c * batch.bands;
        /*
         * BOTH PICTURES, EVERY COLUMN, WHICHEVER ONE IS ON SCREEN.
         *
         * This is what makes the view switch instant instead of a restart. The
         * bar view is being drawn the whole time the scrolling one is showing,
         * so it is already complete the moment it is asked for -- the same
         * bargain pause makes, and for the same reason: the analysis never
         * stopped, so nothing has to be caught up.
         */
        writeColumn(batch.data, off);
        if (batch.slots) writeSweep(batch.data, off, batch.slots[c]);
        /* The clash arrives on the same batch, already measured against the
         * shown channel by the engine -- the editor never computes it. */
        if (batch.clash) writeClash(batch.clash, off, batch.slots ? batch.slots[c] : -1);
      }
      /*
       * THE PLAYHEAD STOPS WITH THE PICTURE, and it has to.
       *
       * The sweep keeps being written while paused -- that is what makes
       * resuming show a current picture rather than a restart -- but the mark
       * saying WHERE it is writing belongs to the frozen frame. Left ungated it
       * went on sliding across a still image, which reads as the picture being
       * live and the analysis being stuck: exactly backwards.
       */
      if (batch.slots && batch.count > 0 && !props.paused) {
        setPlayhead(batch.slots[batch.count - 1]);
      }
      if (!props.paused) paint();
      /* The pointer has not moved, but the picture under it has: a stationary
       * crosshair over a scrolling spectrogram is reading a NEW column every
       * ~21 ms, and a readout that only updated on mousemove would name the
       * level of a column that has since left the screen. */
      if (at && !props.paused) report();
    });
  });

  /* Pausing snapshots; unpausing repaints at once rather than waiting for the
   * next column, which at ~47 a second would be a visible hesitation. */
  let wasPaused = false;
  createEffect(() => {
    const now = !!props.paused;
    if (now === wasPaused) return;
    wasPaused = now;
    if (now) freeze();
    else {
      paint();
      /* Catch the mark up to where the sweep actually got to while the picture
       * was held, so resuming does not show it lagging by the pause. */
      if (head >= 0) setPlayhead(head);
    }
  });

  /*
   * A VIEW CHANGE REPAINTS, AND RE-FREEZES IF IT IS PAUSED.
   *
   * Without the second half, switching views while paused would keep showing
   * the snapshot of the view you just left -- the picture disagreeing with the
   * control that says which picture it is, which is the worst way for a freeze
   * to fail. `frozen` is dropped first so `paint` takes the live buffer, and
   * the snapshot is then retaken from what is actually on screen.
   */
  let wasBars = null;
  createEffect(() => {
    const now = isBars();
    if (now === wasBars) return;
    wasBars = now;
    untrack(() => {
      if (props.paused) frozen = null;
      paint();
      if (props.paused) freeze();
      if (at) report();
    });
  });

  /* A new range means every column in the history is an answer about other
   * frequencies. Clearing is the honest thing to do with them. */
  createEffect(() => {
    void props.generation;
    if (!histCtx) return;
    histCtx.fillStyle = stopCss(0);
    histCtx.fillRect(0, 0, hist.width, hist.height);
    sweepCtx.fillStyle = stopCss(0);
    sweepCtx.fillRect(0, 0, sweep.width, sweep.height);
    levels.fill(0);
    sweepLevels.fill(0);
    clashHistCtx.clearRect(0, 0, clashHist.width, clashHist.height);
    clashSweepCtx.clearRect(0, 0, clashSweep.width, clashSweep.height);
    clashPrev = null;
    cursor = 0;
    head = -1;
    setPlayhead(-1);
    paint();
    /* The crosshair is pointing at a level measured against the old range. */
    if (at) report();
  });

  /*
   * THE CROSSHAIR IS DOM, NOT CANVAS, AND THAT IS THE CHEAP WAY ROUND.
   *
   * Drawn into the picture it would have to be erased and redrawn on every
   * column -- 47 full repaints a second that exist only to move two lines --
   * and a paused picture could not show one at all without disturbing the
   * frozen snapshot. Two absolutely positioned hairlines over the canvas cost
   * nothing, survive the freeze, and are the same hairline the rest of the
   * system draws with.
   */
  return (
    <div
      class="spectro"
      ref={host}
      style={{ width: `${props.width}px`, height: `${props.height}px` }}
      onPointerMove={onPointerMove}
      onPointerLeave={onPointerLeave}
    >
      <canvas
        ref={view}
        class="spectro-canvas"
        style={{ width: `${props.width}px`, height: `${props.height}px` }}
      />
      {/*
        * THE PLAYHEAD: where the sweep is writing, so the newest edge of the
        * picture is findable. Only in the bar view -- the scrolling view's
        * newest column is the right-hand edge and needs no mark.
        */}
      <Show when={isBars() && playhead() >= 0}>
        <i
          class="spectro-playhead"
          style={{ left: `${(playhead() / COLS()) * props.width}px` }}
        />
      </Show>
      <Show when={hover()}>
        {(h) => (
          <>
            <i class="spectro-cross-h" style={{ top: `${h().y}px` }} />
            <i class="spectro-cross-v" style={{ left: `${h().x}px` }} />
          </>
        )}
      </Show>
    </div>
  );
}

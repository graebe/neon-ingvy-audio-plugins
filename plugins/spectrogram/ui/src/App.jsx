/*
 * Spectrogram — the editor.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * Three jobs, and nothing else: decode the columns the plugin pushes, hand them
 * to the canvas, and draw the frequency scale beside it.
 *
 * WHAT THIS FILE IS NOT ALLOWED TO DO IS DECIDE ANYTHING ABOUT THE ANALYSIS. The
 * band count, the log mapping and the dB floor all live in the Rust analyzer, and
 * the scale is drawn from the centre frequencies IT sends.
 */
import { createSignal, createMemo, onMount, onCleanup } from 'solid-js';
import { sendMessage, EditorFrame, useEditorBridge } from '@ultraviolet/ui';
import { MSG } from './lib/msg.js';
import {
  decodeColumns, decodeAxis, marksFor, RANGES,
  timeMarksFor, secondsAgo, dbForLevel, DB_FLOOR,
  decodeSync, slotForPpq, posForSlot, barMarksFor,
  decodeSources, sourceName,
} from './lib/columns.js';
import { decodeState, rangeIndex, createPushGate } from './lib/session.js';
import { Toolbar, BARS } from './lib/Toolbar.jsx';
import { SourceStrip } from './lib/SourceStrip.jsx';
import { Display, PICTURE_W, PICTURE_H } from './lib/Display.jsx';

/* Mirrored by PLUG_WIDTH / PLUG_HEIGHT in config.h. */
const DESIGN_W = 720;
const DESIGN_H = 502;
/* ~47 columns a second, held constant across sample rates by the engine's hop
 * rule -- so this one number turns the width into a span of time. */
const COLUMNS_PER_S = 47;
/* Buses this window will read at once. Mirrors SRECV_MAX_SOURCES - 1. */
const MAX_LISTEN = 3;
/*
 * WHAT COUNTS AS A CLASH: both sources above -60 dB, and within 12 dB of each
 * other. The floor alone is not enough -- a product of two spectra is a SUM in
 * dB, so 0 against -60 scores what -30 against -30 scores.
 */
const CLASH_FLOOR_DB = -60;
const CLASH_BALANCE_DB = 12;

/* "200 Hz", not "0.2 kHz": the unit follows the number. */
const asHz = (v) => (v < 1000 ? `${Math.round(v)} Hz` : `${(v / 1000).toFixed(1)} kHz`);

export default function App() {
  const [batch, setBatch] = createSignal(null);
  const [axis, setAxis] = createSignal(null);
  const [paused, setPaused] = createSignal(false);
  const [range, setRange] = createSignal(0);
  /* Bumped on every change of what the picture is OF: the canvas clears. */
  const [generation, setGeneration] = createSignal(0);
  /* Columns stop arriving when the host stops calling OnIdle -- saying so beats
   * a frozen picture that looks like a crash. */
  const [live, setLive] = createSignal(false);
  /* What the crosshair is over, in the canvas's terms. */
  const [cursor, setCursor] = createSignal(null);
  /* The bar view: whether the x-axis is the host's bars, and how many. */
  const [bars, setBars] = createSignal(false);
  const [barCount, setBarCount] = createSignal(2);   /* index into BARS */
  const [sync, setSync] = createSignal(null);
  /*
   * LISTEN-IN, AND TWO SEPARATE QUESTIONS ABOUT IT. `view` is what the picture
   * is OF (channels ADDED by the plugin into one stream); `cmpA`/`cmpB` are what
   * the clash measures, which has nothing to do with what is on screen.
   * Channel 0 is always this track; `sources` is what EXISTS.
   */
  const [sources, setSources] = createSignal([]);
  const [view, setView] = createSignal([0]);
  const [cmpA, setCmpA] = createSignal(0);
  const [cmpB, setCmpB] = createSignal(1);
  const [clashOn, setClashOn] = createSignal(false);
  let lastSeen = 0;
  /* The clash mask waiting for the column batch it belongs to. */
  let pendingClash = null;
  /* Closed until the plugin's saved session has been applied: a push before
   * that would replace the session with this editor's defaults. */
  const session = createPushGate();

  const applyState = (text) => {
    const st = decodeState(text);
    if (!st) return;
    setRange(rangeIndex(st.lo, st.hi, RANGES));
    setView(st.view);
    setCmpA(st.cmpA);
    setCmpB(st.cmpB);
    setClashOn(st.clashOn);
    session.open();
  };

  /*
   * THE TRANSPORT, AS THE FEW VALUES THE WINDOW DRAWS FROM. sync arrives every
   * idle tick; memoised, the axis and the hint are rebuilt only when the metre,
   * the tempo or the run state actually changes rather than fifty times a second.
   */
  const bpm = createMemo(() => Math.round(sync()?.bpm ?? 120));
  const num = createMemo(() => sync()?.num ?? 4);
  const denom = createMemo(() => sync()?.denom ?? 4);
  const running = createMemo(() => sync()?.running ?? true);
  const nBars = () => BARS[barCount()] ?? 4;

  const onColumns = (bytes) => {
    /* NOT GATED ON `paused`: the freeze is a repaint gate in the canvas. */
    const decoded = decodeColumns(bytes);
    if (!decoded) return;
    lastSeen = performance.now();
    setLive(true);
    /*
     * WHERE EACH COLUMN BELONGS, decided here: the canvas is handed a slot per
     * column. BACK-DATED FROM THE NEWEST, so a catch-up batch covers the musical
     * ground it really spans instead of stacking on one pixel.
     */
    const t = sync();
    if (t) {
      const n = nBars();
      const slots = new Int32Array(decoded.count);
      for (let c = 0; c < decoded.count; c++) {
        const ppq = t.ppq - (decoded.count - 1 - c) * t.ppqPerCol;
        slots[c] = slotForPpq(ppq, n, t.num, t.denom, PICTURE_W);
      }
      decoded.slots = slots;
    }
    /* Only if it is the same shape: a mask over a different number of columns
     * belongs to a different tick. */
    if (pendingClash && pendingClash.count === decoded.count
        && pendingClash.bands === decoded.bands) {
      decoded.clash = pendingClash.data;
    }
    pendingClash = null;
    setBatch(decoded);
  };

  const bridge = useEditorBridge({
    onMessage: (tag, text) => {
      if (tag === MSG.state) applyState(text);
      else if (tag === MSG.axis) setAxis(decodeAxis(text));
      else if (tag === MSG.sync) {
        const t = decodeSync(text);
        if (t) setSync(t);
      } else if (tag === MSG.sources) setSources(decodeSources(text));
    },
    /* The columns and the clash mask are BYTES, decoded from base64 once. */
    bytes: {
      [MSG.cols]: onColumns,
      /* The mask rides the next column batch, so the orange and the picture
       * under it can never be a frame apart. */
      [MSG.clashCols]: (bytes) => {
        const m = decodeColumns(bytes);
        if (m) pendingClash = m;
      },
    },
  });

  /* Half a second of nothing is a stall: a column arrives every ~21 ms. */
  onMount(() => {
    const watchdog = setInterval(() => {
      if (live() && performance.now() - lastSeen > 500) setLive(false);
    }, 250);
    onCleanup(() => clearInterval(watchdog));
  });

  /* PAUSE NEVER LEAVES THE EDITOR: the analysis runs on, only the repaint stops. */
  const togglePause = () => setPaused((p) => !p);

  /* This track, then every LIVE bus in slot order: index 1 here is index 1 in
   * the plugin, which opens the buses in the same order. */
  const busList = createMemo(() => sources().filter((s) => s.live));
  /* "input", not "this track": it has to fit a 112px dropdown. */
  const channelNames = createMemo(() => ['input', ...busList().map(sourceName)]);
  /* `||`, NOT `??`: a rate that has not arrived decodes to 0, which would refuse
   * every bus as a mismatch. */
  const rate = () => sync()?.rate || busList()[0]?.rate || 0;

  /* The names while they fit, a count when they do not. */
  const viewSummary = () => {
    const names = view().map((ch) => channelNames()[ch]).filter(Boolean);
    if (!names.length) return 'nothing';
    const joined = names.join(', ');
    return joined.length <= 17 ? joined : `${names[0]} +${names.length - 1}`;
  };

  /* A bus at another rate is shown and refused rather than hidden. */
  const viewOptions = () => channelNames().map((name, i) => {
    const bus = i > 0 ? busList()[i - 1] : null;
    const off = !!bus && bus.rate !== rate();
    return { id: i, name, hint: off ? `${Math.round(bus.rate / 1000)}k` : '', disabled: off };
  });

  /* Which buses the plugin must open: everything the view shows, plus both ends
   * of the comparison. DERIVED -- there is nothing to keep in step. */
  const neededSlots = () => {
    const buses = busList();
    const want = new Set();
    for (const ch of view()) if (ch > 0) want.add(ch);
    if (clashOn()) { want.add(cmpA()); want.add(cmpB()); }
    want.delete(0);
    return [...want]
      .sort((x, y) => x - y)
      .slice(0, MAX_LISTEN)
      .map((ch) => buses[ch - 1]?.slot)
      .filter((slot) => slot !== undefined);
  };

  /* One place, because all three settings change the same derived list. */
  const push = () => session.send(() => {
    sendMessage(MSG.select, neededSlots().join(','));
    sendMessage(MSG.view, view().join(','));
    sendMessage(MSG.compare, `${cmpA()}:${cmpB()}:${clashOn() ? 1 : 0}`);
  });

  const chooseView = (chans) => {
    /* Never nothing: a spectrogram showing no channel is a broken plugin. */
    const next = chans.length ? chans.slice().sort((a, b) => a - b) : [0];
    setView(next);
    /* The ring holds thirteen seconds of the PREVIOUS mix. */
    setGeneration((g) => g + 1);
    push();
  };

  const chooseCmp = (which, ch) => {
    if (which === 'a') setCmpA(ch); else setCmpB(ch);
    push();
  };

  const toggleClash = () => {
    setClashOn(!clashOn());
    session.send(() => sendMessage(MSG.clash, `${CLASH_FLOOR_DB}:${CLASH_BALANCE_DB}`));
    push();
  };

  /* The zoom. The plugin re-bands and sends the scale back (f_max is clamped
   * to Nyquist). Unpauses: a frozen picture of a range just left is a lie. */
  const chooseRange = (i) => {
    const r = RANGES[i];
    if (!r) return;
    setRange(i);
    setPaused(false);
    setGeneration((g) => g + 1);
    session.send(() => sendMessage(MSG.range, `${r.lo}:${r.hi}`));
  };

  /* THE THREE READOUTS say "—" when the pointer is away rather than a stale number. */
  const readFreq = () => {
    const c = cursor(); const hz = axis();
    if (!c || !hz || c.band >= hz.length) return '—';
    return asHz(hz[c.band]);
  };
  const readTime = () => {
    const c = cursor();
    if (!c) return '—';
    if (bars()) {
      /* "8:4.7": a colon says bar and beat are different kinds of number. */
      const p = posForSlot(c.slot, nBars(), num(), denom(), PICTURE_W);
      return `${p.bar}:${p.beat.toFixed(1)}`;
    }
    const t = secondsAgo(c.age, COLUMNS_PER_S);
    return t < 0.005 ? 'now' : `−${t.toFixed(2)} s`;
  };
  const readLevel = () => {
    const c = cursor();
    if (!c) return '—';
    const db = dbForLevel(c.level);
    /* Byte 0 is "at or below the floor", not "exactly the floor". */
    return db === -Infinity ? `< −${Math.abs(DB_FLOOR)} dB` : `−${Math.abs(db).toFixed(1)} dB`;
  };

  /* Static: one column is one pixel at a constant column rate. */
  const secondMarks = timeMarksFor(PICTURE_W, COLUMNS_PER_S, PICTURE_W);
  /* The bar grid, recomputed only when the window or the metre changes. */
  const timeMarks = createMemo(() => (bars()
    ? barMarksFor(nBars(), num(), denom(), PICTURE_W)
    : secondMarks));
  const freqMarks = createMemo(() => marksFor(axis(), PICTURE_H));

  const hint = createMemo(() => {
    const hz = axis();
    const span = hz && hz.length > 1
      ? `${asHz(hz[0])} – ${asHz(hz[hz.length - 1])}`
      : 'waiting for the plugin';
    /* In the bar view the width is bars and a tempo; `free` says the transport
     * is stopped and the sweep runs at the last tempo seen. */
    const n = nBars();
    const width = bars()
      ? `${n} ${n === 1 ? 'bar' : 'bars'} · ${bpm()} BPM${running() ? '' : ' free'}`
      : `${Math.round(PICTURE_W / COLUMNS_PER_S)} s`;
    return [
      ['log', span],
      ['floor', `−${Math.abs(DB_FLOOR)} dB`],
      [bars() ? 'window' : 'history', width],
    ];
  });

  return (
    /* The picture's well is the one box in this window: framed and opaque, so
     * rings break around it rather than crossing it. */
    <EditorFrame width={DESIGN_W} height={DESIGN_H} motionKey="spectrogram"
                 sources=".well, [data-wave-source]" bridge={bridge} hint={hint()}>
      <Toolbar live={live()} range={range()} onRange={chooseRange}
               bars={bars()} onBars={setBars}
               barCount={barCount()} onBarCount={setBarCount}
               paused={paused()} onPause={togglePause} />

      <SourceStrip summary={viewSummary()} view={view()} onView={chooseView}
                   options={viewOptions()} names={channelNames()}
                   cmpA={cmpA()} cmpB={cmpB()} onCompare={chooseCmp}
                   clash={clashOn()} onClash={toggleClash} />

      <Display freqMarks={freqMarks()} timeMarks={timeMarks()}
               batch={batch()} paused={paused()} generation={generation()}
               bars={bars()} clash={clashOn()} onHover={setCursor}
               freq={readFreq()} time={readTime()} level={readLevel()} />
    </EditorFrame>
  );
}

/*
 * Spectrogram — the editor.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * Three jobs, and nothing else: decode the columns the plugin pushes, hand them
 * to the canvas, and draw the frequency scale beside it.
 *
 * WHAT THIS FILE IS NOT ALLOWED TO DO IS DECIDE ANYTHING ABOUT THE ANALYSIS. The
 * band count, the log mapping and the dB floor all live in the Rust analyzer, and
 * the scale below is drawn from the centre frequencies IT sends. A second copy of
 * the mapping here would look right for as long as nobody changed the first one.
 */
import { createSignal, onMount, onCleanup, Show } from 'solid-js';
import { onMessage, sendMessage } from '@ultraviolet/ui';
import { MSG } from './lib/msg.js';
import {
  decodeColumns, decodeAxis, marksFor, RANGES,
  timeMarksFor, secondsAgo, dbForLevel, DB_FLOOR,
  decodeSync, slotForPpq, posForSlot, barMarksFor,
  decodeSources, sourceName,
} from './lib/columns.js';

import { Hint, Button, Select, Toggle, CheckList, Spectrogram } from '@ultraviolet/ui';

/* Mirrored by PLUG_WIDTH in config.h and by `main` in app.css. */
const DESIGN_W = 720;
/*
 * One column per CSS pixel and one band per CSS pixel: see the arithmetic at the
 * top of app.css. The well around this is two pixels larger in each direction,
 * which is the fix for the frame eating the bottom bands.
 */
const PICTURE_W = 606;
const PICTURE_H = 256;
/* ~47 columns a second, held constant across sample rates by the engine's hop
 * rule -- so this one number turns the width into a span of time. */
const COLUMNS_PER_S = 47;
/*
 * The windows the bar view offers. A loop, a phrase, a section -- and 16, which
 * at 606 pixels gives a bar 38 of them: coarse, but it is the view you reach for
 * to see where a section's energy sits rather than where a hat lands.
 */
const BARS = [1, 2, 4, 8, 16];
/*
 * Buses this window will read at once. Each is a whole analysis chain -- an
 * 8192-point transform about 47 times a second -- and it is also about where a
 * picture stops being worth switching between, so the cost and the legibility
 * run out together. Mirrors SRECV_MAX_SOURCES - 1.
 */
const MAX_LISTEN = 3;
/*
 * WHAT COUNTS AS A CLASH: both sources above -60 dB, and within 12 dB of each
 * other. The floor alone is not enough -- a product of two spectra is a SUM in
 * dB, so 0 against -60 scores what -30 against -30 scores, and only the second
 * is a clash. The window is what says "and neither of them is simply winning".
 */
const CLASH_FLOOR_DB = -60;
const CLASH_BALANCE_DB = 12;

export default function App() {
  const [batch, setBatch] = createSignal(null);
  const [axis, setAxis] = createSignal(null);
  const [scale, setScale] = createSignal(1);
  const [paused, setPaused] = createSignal(false);
  const [range, setRange] = createSignal(0);
  /* Bumped on every range change: the picture's history is about other
   * frequencies now, and the canvas clears on it. */
  const [generation, setGeneration] = createSignal(0);
  /* Columns stop arriving when the host stops calling OnIdle -- or when nothing
   * is loaded on the track at all. Saying so beats a frozen picture that looks
   * like a crash. */
  const [live, setLive] = createSignal(false);
  /* What the crosshair is over, in the canvas's terms: a band index, a column
   * age and a level byte. Turning those into Hz, seconds and dB is this file's
   * job, because the axis the plugin sent lives here. */
  const [cursor, setCursor] = createSignal(null);
  /*
   * THE BAR VIEW. `bars` is off/on rather than a value in the list, because the
   * two are different questions -- whether the x-axis is the clock or the host,
   * and how much of the host it shows -- and folding them into one dropdown
   * made "Off" sit in a list of lengths where it is not one.
   */
  const [bars, setBars] = createSignal(false);
  const [barCount, setBarCount] = createSignal(2);   /* index into BARS */
  const [sync, setSync] = createSignal(null);
  /*
   * LISTEN-IN, AND TWO SEPARATE QUESTIONS ABOUT IT.
   *
   * `view` is what the picture is OF: one or more channels, ADDED together --
   * the plugin sums them in power and sends one stream, so this file never
   * routes by channel. That is not tidiness. Routing by channel is what made a
   * batch for an unselected source silently dropped, and a saved session come
   * back drawing the wrong track with nothing to say so.
   *
   * `cmpA` / `cmpB` are what the ORANGE is measuring, and they have nothing to
   * do with what is on screen: comparing two things you are not looking at is a
   * legitimate thing to ask for, and tying the two together was the confusion
   * being undone here.
   *
   * Channel 0 is always this track. `sources` is the plugin's probe of all
   * sixteen slots -- what EXISTS, never what is open.
   */
  const [sources, setSources] = createSignal([]);
  const [view, setView] = createSignal([0]);
  const [cmpA, setCmpA] = createSignal(0);
  const [cmpB, setCmpB] = createSignal(1);
  const [clashOn, setClashOn] = createSignal(false);
  let lastSeen = 0;
  /* The clash mask waiting for the column batch it belongs to. */
  let pendingClash = null;

  /*
   * THE PAGE IS SCALED, NOT LAID OUT FLUIDLY, and it is the Trance Gate's
   * approach for the Trance Gate's reason: the numbers in app.css ARE the design,
   * and a fluid layout that happens to look close is a different drawing. A host
   * that gives the WebView a viewport narrower than 720 -- Live's own scaling
   * does -- gets the whole window scaled to fit.
   */
  const fit = () => {
    const el = document.querySelector('main');
    if (!el) return 1;
    const k = Math.max(0.1, (window.innerWidth || DESIGN_W) / DESIGN_W);
    el.style.transformOrigin = 'top left';
    el.style.transform = `scale(${k})`;
    setScale(k);
    return k;
  };

  onMount(() => {
    fit();
    window.addEventListener('resize', fit);

    const off = onMessage((tag, text) => {
      if (tag === MSG.cols) {
        /*
         * NOT GATED ON `paused`. The freeze is a repaint gate in the canvas;
         * the columns still arrive and are still written, which is what makes
         * unpausing show a current picture instead of resuming from a seam.
         */
        const decoded = decodeColumns(text);
        if (decoded) {
          lastSeen = performance.now();
          setLive(true);
          /*
           * WHERE EACH COLUMN BELONGS, DECIDED HERE AND NOWHERE ELSE.
           *
           * The canvas is handed a slot per column and draws there; it never
           * learns what a bar is. The slots are computed even while the
           * scrolling view is showing, because the bar picture is kept up to
           * date the whole time -- that is what makes switching instant.
           *
           * BACK-DATED FROM THE NEWEST. A tick normally carries one column, but
           * a host that stalled hands over up to 32 at once, and those cover
           * real musical ground: stacking them on the current position would
           * put a third of a second of audio on one pixel.
           */
          /*
           * NO CHANNEL GATE. There is exactly one picture stream now -- the
           * plugin has already added the viewed channels together -- so there
           * is nothing here to route, and nothing to drop.
           */
          const t = sync();
          if (t) {
            const n = BARS[barCount()] ?? 4;
            const slots = new Int32Array(decoded.count);
            for (let c = 0; c < decoded.count; c++) {
              const ppq = t.ppq - (decoded.count - 1 - c) * t.ppqPerCol;
              slots[c] = slotForPpq(ppq, n, t.num, t.denom, PICTURE_W);
            }
            decoded.slots = slots;
          }
          /* Only if it is the same shape: a mask measured over a different
           * number of columns belongs to a different tick. */
          if (pendingClash && pendingClash.count === decoded.count
              && pendingClash.bands === decoded.bands) {
            decoded.clash = pendingClash.data;
          }
          pendingClash = null;
          setBatch(decoded);
        }
      } else if (tag === MSG.axis) {
        setAxis(decodeAxis(text));
      } else if (tag === MSG.sync) {
        const t = decodeSync(text);
        if (t) setSync(t);
      } else if (tag === MSG.sources) {
        setSources(decodeSources(text));
      } else if (tag === MSG.clashCols) {
        /*
         * The mask, measured by the engine between the two channels this editor
         * NAMED -- not against whatever is on screen. It rides the next column
         * batch rather than being drawn on its own, so the orange and the
         * picture under it can never be a frame apart.
         */
        const m = decodeColumns(text);
        if (m) pendingClash = m;
      }
    });

    /*
     * Half a second of nothing is a stall, not a gap: at the analyzer's defaults
     * a column arrives every 21 ms.
     *
     * It needs no exemption for pause any more. Pause stops the drawing, not the
     * sending, so columns keep arriving and this keeps meaning what it says.
     */
    const watchdog = setInterval(() => {
      if (live() && performance.now() - lastSeen > 500) setLive(false);
    }, 250);

    /* LAST, AND IT HAS TO BE FROM HERE. The plugin sends the axis from OnUIOpen
     * as well, but that fires before this deferred module has evaluated and
     * lands on an undefined global -- see MSG.ready. */
    sendMessage(MSG.ready);

    onCleanup(() => {
      off();
      clearInterval(watchdog);
      window.removeEventListener('resize', fit);
    });
  });

  /*
   * PAUSE NEVER LEAVES THE EDITOR. The plugin is not told, because there is
   * nothing for it to do: the analysis runs, the columns arrive, the history
   * fills. Only the repaint stops.
   */
  const togglePause = () => setPaused((p) => !p);

  /*
   * THE CHANNELS THIS WINDOW CAN TALK ABOUT: this track, then every LIVE bus,
   * in slot order. Channel indices are positions in this list, and the plugin
   * opens the buses in the same order, so index 1 here is index 1 there.
   *
   * Built from what EXISTS rather than from a separate "captured" list. That
   * list was the third setting nobody asked for, and keeping it in step with
   * the other two is what went wrong: it was written on every pick and read
   * back from nothing, so a reopened session listed one channel while the
   * plugin was sending three.
   */
  const busList = () => sources().filter((s) => s.live);

  /*
   * "input", NOT "this track". The name has to fit a 112px dropdown beside
   * another one, and "this track" does not -- it was being cut to "this tr…",
   * which is a worse answer than a shorter word. In a plugin the signal coming
   * into it is the input; every other entry is a bus by name, so there is
   * nothing for it to be confused with.
   */
  const channelNames = () => ['input', ...busList().map(sourceName)];

  /* The sample rate every source has to agree on; a bus at another rate picks a
   * different window and so a different group delay, and the two pictures would
   * sit quietly offset. */
  /*
   * `||`, NOT `??`. A build older than the rate field, or a message that has
   * not arrived, decodes to 0 -- and `??` treats 0 as a perfectly good answer,
   * which would mark every bus as a rate mismatch and quietly refuse the lot.
   */
  const rate = () => sync()?.rate || busList()[0]?.rate || 0;

  /*
   * The names while they fit, a count when they do not. "this track, Bass" says
   * more than "2 in" and costs nothing until it is too long to read.
   */
  const viewSummary = () => {
    const names = view().map((ch) => channelNames()[ch]).filter(Boolean);
    if (!names.length) return 'nothing';
    const joined = names.join(', ');
    return joined.length <= 17 ? joined : `${names[0]} +${names.length - 1}`;
  };

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
  const push = () => {
    sendMessage(MSG.select, neededSlots().join(','));
    sendMessage(MSG.view, view().join(','));
    sendMessage(MSG.compare, `${cmpA()}:${cmpB()}:${clashOn() ? 1 : 0}`);
  };

  const chooseView = (chans) => {
    /* Never nothing: a spectrogram showing no channel at all is a broken
     * plugin rather than a view, and there would be no way to say so. */
    const next = chans.length ? chans.slice().sort((a, b) => a - b) : [0];
    setView(next);
    /* The ring holds thirteen seconds of the PREVIOUS mix, and splicing two
     * different sums with no seam is exactly the confusion being fixed. */
    setGeneration((g) => g + 1);
    push();
  };

  const chooseCmp = (which, ch) => {
    which === 'a' ? setCmpA(ch) : setCmpB(ch);
    push();
  };

  const toggleClash = () => {
    setClashOn(!clashOn());
    sendMessage(MSG.clash, `${CLASH_FLOOR_DB}:${CLASH_BALANCE_DB}`);
    push();
  };

  /*
   * The zoom. The plugin re-bands the analysis and sends the new scale back
   * rather than the editor assuming its request was honoured -- f_max is
   * clamped to Nyquist, so in a 32 kHz session "High" really is 2 k to 16 k.
   *
   * Unpauses: a frozen picture of a range you have just left is a lie, and
   * clicking a dropdown is not how anyone expects to be shown stale data.
   */
  const chooseRange = (i) => {
    const r = RANGES[i];
    if (!r) return;
    setRange(i);
    setPaused(false);
    setGeneration((g) => g + 1);
    sendMessage(MSG.range, `${r.lo}:${r.hi}`);
  };

  /* "200 Hz", not "0.2 kHz": the unit follows the number rather than the other
   * end of the range. The system's rule is that a value carries its unit, and
   * a zoomed view whose top is 200 Hz should say so in hertz. */
  const asHz = (v) => (v < 1000 ? `${Math.round(v)} Hz` : `${(v / 1000).toFixed(1)} kHz`);

  /*
   * THE THREE READOUTS. Each says "--" rather than a stale number when the
   * pointer is away: a spectrogram's whole claim is that what you read is what
   * was measured, and the last value the mouse happened to pass over is not.
   */
  const readFreq = () => {
    const c = cursor(); const hz = axis();
    if (!c || !hz || c.band >= hz.length) return '—';
    return asHz(hz[c.band]);
  };
  const readTime = () => {
    const c = cursor();
    if (!c) return '—';
    /* In the bar view "how long ago" is the wrong question -- the column under
     * the pointer may be from this pass or the one before it. WHERE in the bar
     * is the question that view exists to answer. */
    if (bars()) {
      const t = sync();
      const p = posForSlot(c.slot, BARS[barCount()] ?? 4, t?.num ?? 4, t?.denom ?? 4, PICTURE_W);
      /*
       * "8:4.7", NOT "8.4.68". A dot between the bar and the beat reads as a
       * host's bar.beat.tick and invites the last group to be counted as ticks
       * -- which these are not. A colon says the two numbers are different
       * kinds of thing, and one decimal is as fine as a 38px bar can resolve.
       */
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

  /* Static: one column is one pixel and the column rate is held constant by the
   * engine, so the ticks are arithmetic rather than a measurement. */
  const timeMarks = timeMarksFor(PICTURE_W, COLUMNS_PER_S, PICTURE_W);

  /* The bar grid, recomputed only when the window or the metre changes. */
  const marks = () => {
    if (!bars()) return timeMarks;
    const t = sync();
    return barMarksFor(BARS[barCount()] ?? 4, t?.num ?? 4, t?.denom ?? 4, PICTURE_W);
  };

  const hint = () => {
    const hz = axis();
    const range = hz && hz.length > 1
      ? `${asHz(hz[0])} – ${asHz(hz[hz.length - 1])}`
      : 'waiting for the plugin';
    /*
     * The third clause answers "how wide is this picture", and in the bar view
     * that is bars and a tempo rather than seconds. `free` is not decoration:
     * with the transport stopped the sweep keeps filling at the last tempo
     * seen, and a picture that is aligned to a guess should say so.
     */
    const t = sync();
    const n = BARS[barCount()] ?? 4;
    const span = bars()
      ? `${n} ${n === 1 ? 'bar' : 'bars'} · ${Math.round(t?.bpm ?? 120)} BPM${
          t && !t.running ? ' free' : ''}`
      : `${Math.round(PICTURE_W / COLUMNS_PER_S)} s`;
    return [
      ['log', range],
      /* The minus is U+2212, and the number is the constant the decode uses --
       * not a second spelling of it next to the first. */
      ['floor', `−${Math.abs(DB_FLOOR)} dB`],
      [bars() ? 'window' : 'history', span],
    ];
  };

  return (
    <main>
      <div class="title-row">
        <span class="t-title">Spectrogram</span>
        <div class="title-actions">
          {/* The window's single amber mark, and only when it means something. */}
          <span class="t-label stale">{live() ? '' : 'no signal'}</span>
          {/* No label beside it: the option names the band and the hint bar
              below already prints the numbers. */}
          <Select options={RANGES.map((r) => r.name)} value={range()} onChange={chooseRange} />
          {/* The switch says WHICH axis; the dropdown says how much of it. It
              stays visible while off rather than disappearing, so the window
              does not change shape when the switch is thrown. */}
          <Toggle label="bars" value={bars()} onChange={setBars} />
          <Select options={BARS.map((n) => String(n))} value={barCount()}
                  onChange={setBarCount} width={64} />
          <Button on={paused()} onClick={togglePause}>Pause</Button>
        </div>
      </div>

      {/*
        * THE SOURCES GET THEIR OWN ROW. Eight controls do not fit 656px, and
        * the split is the honest one anyway: the title row says HOW the picture
        * is drawn -- range, bars, paused -- and this one says WHAT it is of.
        */}
      {/*
        * TWO GROUPS, ONE STRIP, AND A RULE BETWEEN THEM.
        *
        * "What is the picture of" and "what is the orange measuring" are
        * different questions, and folding them into one row of controls is what
        * made them feel like one tangled setting. They are now captioned
        * groups, divided -- which costs no height, so the window's vertical
        * arithmetic in app.css is untouched.
        */}
      <div class="control-strip">
        <div class="control-group">
          <span class="group-label t-label">view</span>
          {/*
            * Several channels, ADDED. A CheckList rather than a Select because
            * a native <select multiple> stops being an OS popup, which is the
            * one thing that behaves inside a plugin WebView -- see the
            * component's header.
            */}
          <CheckList
            summary={viewSummary()}
            width={150}
            emptyText="no Listen-In found"
            selected={view()}
            onChange={chooseView}
            options={channelNames().map((name, i) => ({
              id: i,
              name,
              /* A bus at another rate cannot be compared with this one, so it
               * is shown and refused rather than hidden -- hiding it would read
               * as the Listen-In not being there at all. */
              hint: i > 0 && busList()[i - 1] && busList()[i - 1].rate !== rate()
                ? `${Math.round(busList()[i - 1].rate / 1000)}k` : '',
              disabled: i > 0 && busList()[i - 1] && busList()[i - 1].rate !== rate(),
            }))}
          />
        </div>

        <i class="control-divider" />

        <div class="control-group">
          <span class="group-label t-label">compare</span>
          <Select options={channelNames()} value={cmpA()}
                  onChange={(i) => chooseCmp('a', i)} width={112} />
          <span class="vs t-hint">vs</span>
          <Select options={channelNames()} value={cmpB()}
                  onChange={(i) => chooseCmp('b', i)} width={112} />
          <Toggle label="clash" value={clashOn()} onChange={toggleClash} />
        </div>
      </div>

      <div class="display">
        <div class="scale">
          {marksFor(axis(), PICTURE_H).map((m) => (
            <div class="scale-mark t-label" style={{ top: `${m.y}px` }}>
              <span>{m.label}</span>
              <i class="scale-tick" />
            </div>
          ))}
        </div>
        <div class="well">
          <Spectrogram
            width={PICTURE_W}
            height={PICTURE_H}
            cols={PICTURE_W}
            batch={batch()}
            scale={scale()}
            paused={paused()}
            generation={generation()}
            view={bars() ? 'bars' : 'time'}
            clash={clashOn()}
            onHover={setCursor}
          />
        </div>
      </div>

      {/*
        * UNDER THE PICTURE, AND INSET TO LINE UP WITH IT. The scale gutter and
        * the well's 1px frame are what separate the canvas's left edge from the
        * window's, so the same offset is spelled once here rather than twice.
        */}
      <div class="under">
        <div class="time-axis" classList={{ bars: bars() }}>
          {marks().map((m) => (
            <div class="time-mark t-hint" data-anchor={m.anchor ?? 'mid'}
                 classList={{ beat: !!m.beat }} style={{ left: `${m.x}px` }}>
              <i class="time-tick" />
              <span>{m.label}</span>
            </div>
          ))}
        </div>

        {/*
          * The keys are t-label (uppercase, the system's label voice) and the
          * VALUES are t-value, which does not transform: "kHz" and "dB" are
          * spelled the way the unit is spelled, and t-label would print them
          * KHZ and DB. A value carries its unit -- see asHz above.
          */}
        <div class="crosshair-read">
          <span class="xh-key t-label">freq</span><span class="xh-val t-value">{readFreq()}</span>
          <span class="xh-key t-label">{bars() ? 'pos' : 'time'}</span>
          <span class="xh-val t-value">{readTime()}</span>
          <span class="xh-key t-label">level</span><span class="xh-val t-value">{readLevel()}</span>
        </div>
      </div>

      <Hint clauses={hint()} />
    </main>
  );
}

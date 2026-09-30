/*
 * Trance Gate — the editor.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * The JUCE editor's layout, in its own numbers (PluginEditor.cpp:23-61):
 *
 *   window  824 wide      pad 32     ring 240    plot 104
 *   ring     (32,  32, 240, 240)     envelope (32, 296, 240, 104)
 *   gate     (304, 32, 488, 172)     env      (304, 228, 488, 172)
 *   selects  y 424, h 28              pattern  y 468, h 92
 *   grid     y 568                    hint bar last
 *
 * Absolutely positioned rather than flexed, because those numbers ARE the
 * design -- a flex layout that happens to look close is a different drawing.
 */
import { createSignal, createEffect, createMemo, onMount, onCleanup } from 'solid-js';
import { onMessage, sendMessage } from '@ultraviolet/ui';
import { createParams, ParamKnob, ParamSelect, ParamToggle } from '@ultraviolet/ui/params';
import { MSG, P, NUM_PARAMS as NPARAMS } from './lib/msg.js';
import Ring from './lib/Ring.jsx';
import StepGrid from './lib/StepGrid.jsx';
import { EnvelopePlot, PatternPlot, Scope } from './lib/Plots.jsx';
import { Button, Tabs, Hint, Ground, createMotion } from '@ultraviolet/ui';
import { fadeWeights } from './lib/fade.js';
import { randomize, setOrder } from './lib/steps.js';

const RATES = ['1/1T','1/2','1/2T','1/4','1/4T','1/8','1/8T','1/16','1/16T','1/32','1/32T','1/64','1/128'];
const SLOTS = ['1','2','3','4','5','6','7','8'];
/* "%", not "% Step": the long form did not fit the readout and said in every
 * value what the control's own name says once. */
const TIME_MODES = ['ms', '%'];
const CURVES = ['Linear', 'Exponential', 'S-Curve'];
/* Which end the pattern is built up from. The knob means the same thing either
 * way -- how much of the drawn pattern is present -- so 100% is the pattern in
 * both, and this only chooses what the missing part looks like. */
const FADE_DIRS = ['In', 'Out'];

/*
 * ONE CONTENT WIDTH, AND EVERYTHING IN THE WINDOW IS IT.
 *
 * Sixteen 40px pads with 8px between them is 760, and --step is a
 * design-system token on a 4px grid -- so the pads decide this number and the
 * rest of the layout follows them. The plot is 760, the band is 760, the
 * settings row is 760, and the window is 32 + 760 + 32.
 *
 * THE TAB STRIP USED TO BE A COLUMN AND IS NOW AN OVERLAY. It took 24 off the
 * band's right edge with 8 of gap, so the plot drew 32 narrower than the pads
 * under it; widening the window to 856 bought that alignment at the cost of a
 * second right-hand edge -- the band ended at 824 and everything else at 792.
 * Over the plot it costs nothing, so the window is 824 again and there is one
 * padding all round.
 */
const PLOT_W = 760;
const BAND_W = PLOT_W;

const hexToBits = (hex, n) => {
  const bits = new Array(n).fill(false);
  if (!hex) return bits;
  let bit = 0;
  for (let i = hex.length - 1; i >= 0 && bit < n; i--) {
    const v = parseInt(hex[i], 16);
    if (Number.isNaN(v)) continue;
    for (let k = 0; k < 4 && bit < n; k++, bit++) bits[bit] = ((v >> k) & 1) === 1;
  }
  return bits;
};

/*
 * COPY, THE WAY A PLUGIN'S WEBVIEW ALLOWS IT.
 *
 * navigator.clipboard.writeText needs a secure context and a user-gesture the
 * WKWebView inside a plugin does not reliably grant -- it resolves, or it
 * rejects, or it silently does nothing, depending on the host. The textarea
 * and execCommand('copy') route is deprecated everywhere and works here, so it
 * is the fallback rather than the other way round.
 *
 * PASTE HAS NO EQUIVALENT: readText() is gated behind a permission prompt the
 * WebView cannot show, and there is no legacy escape hatch -- a page simply
 * cannot read the clipboard unaided. So paste is a field the user pastes INTO;
 * see the Paste button.
 */
async function copyToClipboard(text) {
  try {
    await navigator.clipboard.writeText(text);
    return true;
  } catch { /* fall through */ }
  try {
    const ta = document.createElement('textarea');
    ta.value = text;
    /* Off-screen but NOT display:none or visibility:hidden -- the selection
     * has to be real for execCommand to have anything to copy. */
    ta.setAttribute('readonly', '');
    ta.style.cssText = 'position:fixed;top:0;left:-9999px;opacity:0';
    document.body.appendChild(ta);
    ta.select();
    ta.setSelectionRange(0, ta.value.length);
    const ok = document.execCommand('copy');
    ta.remove();
    return ok;
  } catch { return false; }
}

export default function App() {
  /* Every parameter's value, display string and default -- listening from here,
   * before `ready` is sent. */
  const host = createParams(NPARAMS);
  /* The Motion switch, remembered between openings. Not a host parameter -- see
   * the kit's lib/motion.js for why a view is not something to automate. */
  const [motion, setMotion] = createMotion('trance-gate');
  /* The ground's handle, set by <Ground ref>. A kick arrives as a message and is
   * handed straight to it. */
  let ground = null;
  const [ui, setUi] = createSignal({ steps: [], ties: [], depths: [], orders: [],
                                     length: 16,
                                     phase: 0, msStep: 0, moving: false, cursor: 0 });
  const [params, setParams] = createSignal(null);
  const [scope, setScope] = createSignal([]);
  const [scopeWindow, setScopeWindow] = createSignal(1000);
  /*
   * WHERE THE SWEEP IS WRITING. Not an origin -- the columns are drawn in place,
   * because the x-axis IS the pattern and standing still is the point -- so this
   * is only the mark that says the picture is filling left to right.
   */
  const [scopeHead, setScopeHead] = createSignal(0);
  /* The rendered gate curve, from the plugin. Null until the first push. */
  const [gate, setGate] = createSignal(null);
  const [tab, setTab] = createSignal(0);
  /*
   * THE CLOCK. `at` is performance.now() when this phase was received, which
   * is what turns a sampled position into a running one -- see playPhase().
   */
  const [anchor, setAnchor] = createSignal({ phase: 0, msStep: 0, length: 16,
                                             moving: false, at: 0 });
  const [frame, setFrame] = createSignal(0);
  const [pasting, setPasting] = createSignal(false);
  /*
   * ORDER MODE, and how far into the sequence you are.
   *
   * `named` is the steps clicked since the mode was entered, so the next click
   * is rank count+1. UI-only state: the engine holds the order and normalises it
   * after every rank, so this is nothing but "where am I in what I am typing".
   */
  const [orderMode, setOrderMode] = createSignal(false);
  const [named, setNamed] = createSignal({});
  const orderNext = () => Object.keys(named()).length;
  let pasteEl;

  /* ⌘ on a Mac, Ctrl elsewhere -- the hint has to name the key the user will
   * actually press, and this plugin runs on both. */
  const modKey = () => (/Mac|iP(hone|ad)/.test(navigator.platform ?? '') ? '\u2318' : 'Ctrl-');

  const startPaste = async () => {
    try {
      const t = await navigator.clipboard.readText();
      if (t) { sendMessage(MSG.patch, t); return; }
    } catch { /* no permission -- the field below is the answer */ }
    setPasting(true);
    /* Focused on the next frame: the element does not exist until the signal
     * above has been rendered. */
    requestAnimationFrame(() => pasteEl?.focus());
  };

  onMount(() => {
    onMessage((tag, msg) => {

      if (tag === MSG.ground) {
        /* One message, one ring. A malformed payload is dropped rather than
         * turned into a full-strength kick. */
        const gs = Number.parseFloat(msg);
        if (Number.isFinite(gs)) ground?.trigger(gs);
        return;
      }

      if (tag === MSG.uiState) {
        const f = msg.split(':');
        if (f.length < 9) return;
        const length = Math.max(1, parseInt(f[2], 10) || 16);
        const depths = [], orders = [];
        for (let i = 0; i < length; i++) {
          depths.push((parseInt((f[7] || '').substr(i * 2, 2), 16) || 0) / 255);
          /* The arrival rank, 1..N, and 0 for a step that is off. Two hex digits
           * each, exactly like the depths beside them. */
          orders.push(parseInt((f[8] || '').substr(i * 2, 2), 16) || 0);
        }
        const phase = parseFloat(f[3]) || 0;
        const msStep = parseFloat(f[4]) || 0;
        const moving = f[5] === '1';
        /* RE-ANCHORED ON EVERY PUSH, so the interpolation below can never
         * drift further than one idle tick from the engine. */
        setAnchor({ phase, msStep, length, moving, at: performance.now() });
        return setUi({
          steps: hexToBits(f[0], length), ties: hexToBits(f[1], length),
          length, phase, msStep, moving,
          cursor: parseInt(f[6], 10) || 0, depths, orders,
        });
      }
      if (tag === MSG.params) {
        const f = msg.split(':');
        if (f.length < 16) return;
        /* LEGATO IS FIELD 1 AND WAS NEVER READ, which is why the Pattern plot
         * drew three attacks for three joined neighbours: it had no way to know
         * Join Neighbors was on. */
        return setParams({ legato: +f[1] >= 0.5, curve: +f[3], rate: f[4],
                           amount: +f[6], width: +f[7],
                           attack: +f[8], decay: +f[9], sustain: +f[10],
                           release: +f[11], widthMs: +f[12],
                           fade: +f[13], fadeSoft: +f[14] >= 0.5,
                           fadeOut: +f[15] >= 0.5 });
      }
      if (tag === MSG.scope) {
        /*
         * "<cols>:<windowMs>:<4 hex pairs per column>" -- a byte per bound,
         * which is finer than the plot can draw and seven times smaller than
         * the "%.3f" text it replaced. That text was ~9.6 KB base64 against an
         * 8192-byte transport that truncates rather than fails, so the sweep
         * lost its tail every frame.
         *
         * NOT ROTATED, AND THAT IS THE CHANGE. Column k is pattern phase
         * k/cols and is drawn at that x, so the axis stands still and the trace
         * fills left to right. `head` is where the sweep is writing -- a mark,
         * not an origin. The window is one CYCLE of the gate now rather than a
         * second of wall time, which is what lets the envelope be drawn over it.
         */
        const f = msg.split(':');
        if (f.length < 4) return;
        const cols = Math.max(0, Math.min(1024, parseInt(f[0], 10) || 0));
        const windowMs = parseFloat(f[1]) || 0;
        const head = Math.max(0, Math.min(cols - 1, parseInt(f[2], 10) || 0));
        const hex = f[3];
        const out = new Array(cols);
        for (let i = 0; i < cols; i++) {
          const o = i * 8;
          const v = (k) => (parseInt(hex.substr(o + k * 2, 2), 16) || 0) / 127.5 - 1;
          out[i] = [v(0), v(1), v(2), v(3)];
        }
        setScopeWindow(windowMs);
        setScopeHead(head);
        return setScope(out);
      }
      /*
        * THE GATE, AS THE ENGINE APPLIES IT. "<length>:<perStep>:<hex>", a byte
        * per sample of one cycle. This side does not model it any more -- see
        * the note on PatternPlot.
        */
      if (tag === MSG.gate) {
        const f = msg.split(':');
        if (f.length < 3) return;
        const len = Math.max(1, parseInt(f[0], 10) || 1);
        const per = Math.max(1, parseInt(f[1], 10) || 1);
        const hex = f[2];
        const n = Math.min(len * per, hex.length >> 1);
        const out = new Array(n);
        for (let i = 0; i < n; i++)
          out[i] = (parseInt(hex.substr(i * 2, 2), 16) || 0) / 255;
        return setGate({ length: len, perStep: per, values: out });
      }

      if (tag === MSG.patch) copyToClipboard(msg);
    });

    /*
     * LAST, AND ONLY AFTER THE LISTENERS ARE REGISTERED.
     *
     * The plugin pushes all twelve values from OnUIOpen, which fires on
     * didFinishNavigation -- but this editor is a <script type="module"> and
     * module scripts are DEFERRED, so they evaluate after the document is
     * done. Every one of those pushes landed before globalThis.SPVFD existed
     * and was dropped, and the UI sat on twelve zeroes until something was
     * touched. That single fact produced four separate reported faults: a knob
     * whose first drag jumped to zero and only behaved on the second, a switch
     * drawn off whatever the engine held, and two dropdowns stuck on their
     * first entry.
     *
     * Asking is the fix. A push that races page load cannot be made to win;
     * a request sent from onMount cannot lose.
     */
    sendMessage(MSG.ready);
  });

  /* The stage percentages come from `params` and the step duration from
   * `ui`; the plots need both, so they are merged once here. */
  const plotParams = () => {
    const p = params();
    return p ? { ...p, msStep: ui().msStep } : null;
  };

  /*
   * THE FADE'S WEIGHT PER STEP, DERIVED HERE AND PASSED DOWN.
   *
   * Three views need it -- the pads, the ring and the Pattern plot -- so it is
   * computed once from the arrival order the `ui` readout already carries rather
   * than asked for as a fourth field. lib/fade.js is the engine's own formula and
   * is pinned to a table the engine generates, which is the same arrangement
   * curves.js has: the rule this codebase holds is not "never mirror the DSP", it
   * is "never mirror it unpinned".
   */
  const weights = createMemo(() => {
    const p = params();
    if (!p) return null;
    const u = ui();
    return fadeWeights(u.orders, u.steps, u.length,
                       p.fade ?? 1, !!p.fadeSoft, !!p.fadeOut);
  });
  /* Which kind the fade is introducing -- the one whose numbers are worth
   * drawing, and the one ORDER mode and SHUFFLE act on. */
  const arriving = (i) => !!params()?.fadeOut !== !!ui().steps?.[i];
  /* Below 100% there is something to explain, so the pads show their numbers. */
  const fading = () => (params()?.fade ?? 1) < 0.999;

  /* How many steps the fade has to introduce -- the denominator of the order and
   * of the ORDER button's count. Whichever kind is arriving, not always the
   * hits: Fade Out sequences the holes. */
  const hits = () => {
    const u = ui();
    let n = 0;
    for (let i = 0; i < u.length; i++) if (arriving(i)) n++;
    return n;
  };

  /*
   * SHUFFLE, AS A RUN OF RANKS RATHER THAN A NEW ENGINE KEY.
   *
   * Assigning rank 1, then 2, then 3 down a shuffled list of the on steps lands
   * exactly on that permutation: each `step_order` inserts at its rank and
   * shifts the rest, and a rank above everything already placed leaves those
   * alone. So the engine needs nothing it does not already have for ORDER mode,
   * and this is the same path a person clicking the pads takes.
   */
  const shuffleOrder = () => {
    const u = ui();
    const idx = [];
    for (let i = 0; i < u.length; i++) if (arriving(i)) idx.push(i);
    for (let i = idx.length - 1; i > 0; i--) {
      const j = Math.floor(Math.random() * (i + 1));
      [idx[i], idx[j]] = [idx[j], idx[i]];
    }
    idx.forEach((step, k) => setOrder(step, k + 1));
  };

  /* What the pads need to know while a sequence is being typed: the mode, how
   * many have been named, and which. Passed as one object because steps.js takes
   * the model it reads live rather than a captured copy. */
  const orderModel = () => ({
    orderMode: orderMode(),
    orderNext: orderNext(),
    /* Only the arriving kind can be sequenced: under Fade Out you are putting
     * the HOLES in order, and a hit has no place in that sequence. */
    orderTakes: arriving,
    onOrdered: (i) => setNamed((m) => ({ ...m, [i]: true })),
  });

  /*
   * THE GATE OVERLAY IS GONE, and removing it is the better half of the
   * change rather than a casualty of it.
   *
   * It drew the pattern's gate across the scope, which only meant anything
   * while the scope's x-axis WAS the pattern. The axis is wall time now, so
   * the overlay would have been the right shape against the wrong axis.
   *
   * It was also the UI re-deriving the gate from the parameters -- a second
   * implementation of DSP, of exactly the kind that has already cost this
   * editor an S-curve and a stage machine. The wet trace is the gating, drawn
   * by the engine that actually applies it.
   */

  /*
   * THE VISUALISATION'S CLOCK IS THE ENGINE'S, NOT THE TIMER'S.
   *
   * It used to be Math.floor(ui().phase) -- the phase as of whenever OnIdle
   * last fired. OnIdle runs on a main-thread timer at IDLE_TIMER_RATE 20, so
   * 50 Hz at best: at 125 ms a step, a step boundary could be drawn up to
   * 20 ms late even with a punctual timer, and under Live's UI load the timer
   * is not punctual. The result stuttered and drifted against the sound.
   *
   * So the push is treated as an ANCHOR rather than as the answer -- the
   * engine's own step position and step duration -- and the phase is carried
   * forward from it:
   *
   *     phase = phase0 + (now - t0) / msStep      wrapped by length
   *
   * One timing source, read at the display's rate instead of resampled at the
   * timer's. `moving` gates it, so a stopped transport holds still.
   *
   * A RESIDUAL LEAD REMAINS, and it is worth naming rather than chasing: the
   * phase at idle is where the audio thread has RENDERED to, which is one
   * output buffer plus device latency ahead of what you hear. That is a
   * constant offset rather than drift, and correcting it needs a latency
   * figure hosts report inconsistently.
   */
  const playPhase = () => {
    const a = anchor();
    const n = Math.max(1, a.length);
    if (!a.moving || !(a.msStep > 0)) return a.phase % n;
    frame();                             /* the dependency that makes this tick */
    const p = a.phase + (performance.now() - a.at) / a.msStep;
    return ((p % n) + n) % n;
  };

  /*
   * Two readings of the one clock, because the marks are different shapes.
   * The pattern plot's playhead is a LINE and glides; a lit wedge and a lit pad
   * are discrete and must snap at the boundary and not before. createMemo so
   * the discrete half only notifies when the integer actually changes, rather
   * than sixty times a second.
   */
  const playStep = createMemo(() => Math.floor(playPhase()) % Math.max(1, ui().length));

  /* rAF rather than setInterval: it is the display's own cadence, and it stops
   * when the window is hidden, which is the whole plugin window in a host tab
   * that is not showing. */
  onMount(() => {
    let live = true;
    const tick = () => {
      if (!live) return;
      if (anchor().moving) setFrame((n) => n + 1);
      requestAnimationFrame(tick);
    };
    requestAnimationFrame(tick);
    onCleanup(() => { live = false; });
  });

  /*
   * THE DESIGN IS 824 WIDE AND IS SCALED TO WHATEVER VIEWPORT IT GETS.
   *
   * It is a fixed layout -- the window is 824 and grows only in height
   * -- and the WebView does not necessarily hand us 824 CSS pixels. In Live
   * it hands us fewer, and the page simply overflowed: the GATE panel cut off
   * after Length, the settings row after Time, and six of the sixteen pads
   * were past the right edge. Reproduced at a 560px viewport in a browser,
   * which is how the cause was found rather than guessed.
   *
   * Scaling keeps every proportion and every one of the original's numbers
   * intact, which laying the design out fluidly would not.
   */
  const DESIGN_W = 824;
  const fit = () => {
    const el = document.querySelector('main');
    if (!el) return;
    const k = Math.max(0.1, (window.innerWidth || DESIGN_W) / DESIGN_W);
    el.style.transformOrigin = 'top left';
    el.style.transform = `scale(${k})`;
    return k;
  };

  /* The grid wraps at 16, so 128 steps is eight rows. The UI reports the
   * height it needs -- in the viewport's own pixels, so the scale above is
   * already accounted for -- and the plugin resizes the window to it. */
  let lastSent = '';
  createEffect(() => {
    const rows = Math.max(1, Math.ceil(ui().length / 16));
    const k = fit() ?? 1;
    /* 644 is main's padding-top -- the absolutely positioned block above the
     * grid, which gained a third panel. app.css must agree. */
    const designH = 644 + rows * 40 + (rows - 1) * 8 + 24 + 28;
    const msg = String(Math.ceil(designH * k));
    if (msg !== lastSent) { lastSent = msg; sendMessage(MSG.height, msg); }
  });

  onMount(() => {
    fit();
    window.addEventListener('resize', fit);
  });

  /* The window's one readout-size number. The Ring card has the COUNT in the
   * middle and the knob that changes it elsewhere -- which is exactly this
   * arrangement, not a duplication to avoid. */
  const centre = () => String(ui().length);

  /*
   * THE STAGE READOUTS, IN WHICHEVER UNIT Env Time ASKS FOR.
   *
   * THE TEST WAS INVERTED. TIME_MODES is ['ms', '%'] and the engine's TimeMode
   * is Ms = 0, Pct = 1 -- so `< 0.5` is MS, and returning the plugin's own text
   * there returned the PERCENTAGE, because a stage is stored as a percentage of
   * the gate's width and that is what the parameter formats. Selecting ms showed
   * percent and selecting percent showed ms, exactly swapped, for every one of
   * the three stages.
   *
   * It is display-only in both directions: the engine states that it never
   * consults time_mode -- ms and % are two readings of one number -- so nothing
   * about the sound was ever involved.
   */
  const stageText = (i) => {
    const p = params();
    if (!p || host.value(P.timeMode) >= 0.5) return host.text(i);   /* % -- as stored */
    const pct = { [P.attack]: p.attack, [P.decay]: p.decay, [P.release]: p.release }[i];
    return pct === undefined ? host.text(i) : `${(pct / 100 * p.widthMs).toFixed(1)} ms`;
  };

  return (
    <main>
      {/* FIRST CHILD OF THE WINDOW, which is the design system's contract for a
        * Ground. The three panels, the ring, the step grid and the plot band all
        * emit and reflect. These are this editor's own class spellings of the
        * design system's .ph-panel, .ph-ring and .ph-grid. */}
      <Ground enabled={motion()} sources=".panel, .ring-slot, .grid-slot, .band, [data-wave-source]" ref={(h) => { ground = h; }} />

      {/* The corner mark. Hint style -- the smallest thing the system has, so
        * it sits in the window without competing with anything in it. */}
      {/* LEFT COLUMN: the ring, and the envelope plot ALWAYS under it. The
        * envelope is not tabbed and never was -- the tabs choose between
        * Pattern and Signal in the band further down. */}
      <div class="ring-slot">
        {/* THE RING EDITS NOW, so it needs everything the grid has: a tie must
          * read as a tie and an amount as an amount, or the two views of the
          * same sixteen steps would contradict each other. */}
        <Ring size={240} length={ui().length} steps={ui().steps}
              ties={ui().ties} depths={ui().depths} cursor={ui().cursor}
              playhead={playStep()} moving={ui().moving}
              weights={weights()} {...orderModel()}
              centre={centre()} label="STEPS" />
      </div>
      <div class="env-plot-slot">
        <EnvelopePlot params={plotParams()} w={240} h={104} />
      </div>

      {/* Rate, Length, Amount, Width -- the panel's own order. */}
      <section class="panel gate-panel">
        <h2 class="t-title">GATE</h2>
        <div class="knob-row">
          <ParamKnob params={host} idx={P.rate} label="Rate" />
          <ParamKnob params={host} idx={P.length} label="Length" />
          <ParamKnob params={host} idx={P.amount} label="Amount" />
          <ParamKnob params={host} idx={P.width} label="Width" />
        </div>
      </section>

      <section class="panel env-panel">
        <h2 class="t-title">ENVELOPE</h2>
        <div class="knob-row">
          <ParamKnob params={host} idx={P.attack} label="Attack" display={stageText(P.attack)} />
          <ParamKnob params={host} idx={P.decay} label="Decay" display={stageText(P.decay)} />
          <ParamKnob params={host} idx={P.sustain} label="Sustain" />
          <ParamKnob params={host} idx={P.release} label="Release" display={stageText(P.release)} />
        </div>
      </section>

      {/*
        * FADE IN: the knob, its shape, and the order it introduces them in.
        *
        * The order controls live HERE and not beside the pads, because the order
        * is only ever about this knob -- it is what the Fade sweeps through, and
        * it means nothing without it.
        */}
      <section class="panel fade-panel">
        <h2 class="t-title">FADE</h2>
        <div class="knob-row">
          <ParamKnob params={host} idx={P.fade} label="Fade" />
          <div class="fade-actions">
            {/* THE DIRECTION IS A SETTING, NOT A SECOND KNOB. In introduces the
              * steps you drew on and leaves silence behind; Out introduces the
              * holes and leaves the gate open. 100% is the pattern either way. */}
            <ParamSelect params={host} idx={P.fadeDir} options={FADE_DIRS} label="Dir"
                         labelWidth={28} width={76} />
            <ParamToggle params={host} idx={P.fadeSoft} label="Soft" />
            {/*
              * ORDER MODE. Worded, not a glyph: "the system uses no icon set --
              * state is shown by light and by words". The count is the whole
              * affordance -- it says a sequence is being typed and how far in you
              * are, which no label on a button could.
              */}
            <Button on={orderMode()}
                    title="Tap the steps in the order the fade should introduce them"
                    onClick={() => { setNamed({}); setOrderMode((v) => !v); }}>
              {orderMode() ? `ORDER ${orderNext()}/${hits()}` : 'ORDER'}
            </Button>
            <Button title="Shuffle the arrival order" onClick={shuffleOrder}>
              SHUFFLE
            </Button>
          </div>
        </div>
      </section>

      {/* ONE ROW, left to right. It fits because the two config actions are
        * glyphs rather than the words that needed a second row. */}
      <div class="settings-row">
        {/* `value` was missing entirely, so this always read "1" however the
          * engine's slot moved -- and since Length and the whole pattern are
          * PER SLOT, it was the one control whose reading mattered most.
          *
          * No label, as the original had none: `slot.setBounds (kLeftX, ...)`
          * with no slotL beside it. The StepGrid card puts this above-left of
          * the grid, where its position says what it is. */}
        <ParamSelect params={host} idx={P.slot} options={SLOTS} width={96} />
        <ParamToggle params={host} idx={P.legato} label="Join Neighbors" />
        <ParamSelect params={host} idx={P.curve} options={CURVES} label="Curve" labelWidth={44} width={124} />
        <ParamSelect params={host} idx={P.timeMode} options={TIME_MODES} label="Time" labelWidth={36} width={88} />
        <span class="spacer" />
        {/* A whole-pattern action, which is why it sits with Copy and Paste
          * rather than in the Fade panel: those three are the only controls here
          * that replace the pattern instead of adjusting it. Worded, per the
          * design system; the glyph pair beside it is the stated exception. */}
        <Button title="Fill this slot with a new pattern and arrival order"
                onClick={randomize}>RANDOM</Button>
        {/* THE KIT'S BUTTON TAKES CHILDREN, so the two marks live here rather
          * than as a glyph set inside it -- the design system says "the system
          * uses no icon set ... write the word", and these two are the
          * exception that has to own itself. `icon` is the 40px square form.
          *
          * Hairlines, like the chevron: two offset rectangles for copy, a sheet
          * under a clipboard's tab for paste. */}
        <Button icon title="Copy gate config"
                onClick={() => sendMessage(MSG.requestPatch)}>
          <svg width="14" height="14" viewBox="0 0 14 14">
            <rect x="1.5" y="1.5" width="8" height="8" fill="none" stroke="currentColor" stroke-width="1" />
            <rect x="4.5" y="4.5" width="8" height="8" fill="none" stroke="currentColor" stroke-width="1" />
          </svg>
        </Button>
        <Button icon title="Paste gate config" onClick={startPaste}>
          <svg width="14" height="14" viewBox="0 0 14 14">
            <rect x="2" y="3" width="10" height="9" fill="none" stroke="currentColor" stroke-width="1" />
            <rect x="5" y="0.5" width="4" height="3" fill="none" stroke="currentColor" stroke-width="1" />
          </svg>
        </Button>
        {/*
          * THE PASTE FIELD, and it exists because a page cannot read the
          * clipboard.
          *
          * readText() is behind a permission prompt a plugin's WKWebView
          * cannot show, and unlike copy there is no legacy fallback. But the
          * PASTE EVENT carries the data with no permission at all -- that is
          * the whole trick. So Paste focuses a field, the user presses the
          * shortcut they were already going to press, and onPaste reads
          * clipboardData directly.
          *
          * Tried programmatically first, so on a host that does grant it the
          * field never appears.
          */}
        {pasting() && (
          <input ref={pasteEl} class="paste-field t-hint"
                 placeholder={`${modKey()}V to paste`}
                 onPaste={(e) => {
                   const t = e.clipboardData?.getData('text');
                   if (t) sendMessage(MSG.patch, t);
                   setPasting(false);
                   e.preventDefault();
                 }}
                 onBlur={() => setPasting(false)}
                 onKeyDown={(e) => { if (e.key === 'Escape') setPasting(false); }} />
        )}
      </div>

      {/* THE TWO PLOTS OCCUPY THE SAME BAND -- only one is visible at a time
        * -- and the tabs that choose between them take a 24px strip off its
        * right edge. */}
      <div class="band" style={{ width: `${BAND_W}px` }}>
        <div class="band-plot">
          {/* The FRACTIONAL phase here and the integer step everywhere else:
            * this playhead is a line and glides, a lit pad is discrete and
            * snaps. Both read the one clock, so they cannot disagree. */}
          {tab() === 0 && <PatternPlot length={ui().length} steps={ui().steps}
                                       ties={ui().ties} depths={ui().depths}
                                       weights={weights()} gate={gate()}
                                       params={plotParams()} w={PLOT_W} h={92}
                                       phase={playPhase()} moving={ui().moving} />}
          {/* The Scope takes no playhead and no pattern: it is a rolling
            * window of wall time, so "now" is always its right-hand edge and
            * there is no step for a mark to sit on. */}
          {/* THE SCOPE TAKES THE PATTERN NOW. Its x-axis is one cycle of the
            * gate rather than a second of wall time, so the step rules line up
            * with it and the envelope can be drawn over it -- which is the whole
            * reason to want a static axis. `head` is where the sweep is writing.
            */}
          {tab() === 1 && <Scope scope={scope()} w={PLOT_W} h={92}
                                 windowMs={scopeWindow()} head={scopeHead()}
                                 length={ui().length} steps={ui().steps}
                                 ties={ui().ties} depths={ui().depths}
                                 weights={weights()} params={plotParams()}
                                 gate={gate()}
                                 phase={playPhase()} moving={ui().moving} />}
        </div>
        <Tabs tabs={['Pattern', 'Signal']} active={tab()} onSelect={setTab} />
      </div>

      <div class="grid-slot">
        <StepGrid length={ui().length} steps={ui().steps} ties={ui().ties}
                  depths={ui().depths} cursor={ui().cursor}
                  playhead={playStep()} moving={ui().moving}
                  orders={ui().orders} weights={weights()}
                  fading={fading()} named={named()} {...orderModel()}
                  onOrder={setOrder} />
      </div>

      {/* THREE CLAUSES IS THE CAP, and a fourth means the window needs
        * simplifying rather than a smaller font -- so ORDER mode SWAPS them.
        * While a sequence is being typed, the three rules that apply are its
        * own. */}
      <Hint clauses={orderMode() ? [
        ['click', `the ${params()?.fadeOut ? 'gaps' : 'steps'} in the order they should arrive`],
        ['a number', 'to type one — they swap'],
        ['ORDER', 'again to finish'],
      ] : [
        ['click', 'a step to toggle'],
        ['shift-click', 'for a tie'],
        ['drag', 'up or down for its amount'],
      ]} motion={motion()} onMotion={setMotion} />
    </main>
  );
}

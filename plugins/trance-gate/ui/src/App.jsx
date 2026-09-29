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
import { onParam, onMessage, sendMessage, MSG } from './lib/iplug.js';
import Knob from './lib/Knob.jsx';
import Ring from './lib/Ring.jsx';
import StepGrid from './lib/StepGrid.jsx';
import { EnvelopePlot, PatternPlot, Scope } from './lib/Plots.jsx';
import { Switch, Select, GlyphButton, Tabs, HintBar } from './lib/Controls.jsx';

const P = { slot: 0, length: 1, rate: 2, legato: 3, timeMode: 4, curve: 5,
            amount: 6, width: 7, attack: 8, decay: 9, sustain: 10, release: 11 };

const RATES = ['1/1T','1/2','1/2T','1/4','1/4T','1/8','1/8T','1/16','1/16T','1/32','1/32T','1/64','1/128'];
const SLOTS = ['1','2','3','4','5','6','7','8'];
const TIME_MODES = ['ms', '% Step'];
const CURVES = ['Linear', 'Exponential', 'S-Curve'];

/*
 * THE PLOT AND THE PADS ARE THE SAME WIDTH, and that is why the window is 856
 * rather than the original's 824.
 *
 * Sixteen 40px pads with 8px between them is 760, and the band used to be 760
 * too -- but the tab strip takes 24 off its right with 8 of gap, so the plot
 * itself drew at 728 against a 760 grid. The JUCE editor accepted that ("a
 * rhyme that thin is worth less than a view switch you can find"); aligning it
 * costs 32px of window and nothing else, and every other number in the layout
 * is untouched.
 */
const PLOT_W = 760;
const BAND_W = PLOT_W + 8 + 24;          /* plot, gap, tab strip */

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
  const [vals, setVals] = createSignal(new Array(12).fill(0));
  const [text, setText] = createSignal(new Array(12).fill(''));
  const [ui, setUi] = createSignal({ steps: [], ties: [], depths: [], length: 16,
                                     phase: 0, msStep: 0, moving: false, cursor: 0 });
  const [params, setParams] = createSignal(null);
  const [scope, setScope] = createSignal([]);
  const [scopeWindow, setScopeWindow] = createSignal(1000);
  const [tab, setTab] = createSignal(0);
  /*
   * THE CLOCK. `at` is performance.now() when this phase was received, which
   * is what turns a sampled position into a running one -- see playPhase().
   */
  const [anchor, setAnchor] = createSignal({ phase: 0, msStep: 0, length: 16,
                                             moving: false, at: 0 });
  const [frame, setFrame] = createSignal(0);
  const [pasting, setPasting] = createSignal(false);
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
    onParam((i, v) => i >= 0 && i < 12 &&
      setVals((p) => { const n = p.slice(); n[i] = v; return n; }));

    onMessage((tag, msg) => {
      if (tag >= 0 && tag < 12)
        return setText((p) => { const n = p.slice(); n[tag] = msg; return n; });

      if (tag === MSG.uiState) {
        const f = msg.split(':');
        if (f.length < 8) return;
        const length = Math.max(1, parseInt(f[2], 10) || 16);
        const depths = [];
        for (let i = 0; i < length; i++)
          depths.push((parseInt((f[7] || '').substr(i * 2, 2), 16) || 0) / 255);
        const phase = parseFloat(f[3]) || 0;
        const msStep = parseFloat(f[4]) || 0;
        const moving = f[5] === '1';
        /* RE-ANCHORED ON EVERY PUSH, so the interpolation below can never
         * drift further than one idle tick from the engine. */
        setAnchor({ phase, msStep, length, moving, at: performance.now() });
        return setUi({
          steps: hexToBits(f[0], length), ties: hexToBits(f[1], length),
          length, phase, msStep, moving,
          cursor: parseInt(f[6], 10) || 0, depths,
        });
      }
      if (tag === MSG.params) {
        const f = msg.split(':');
        if (f.length < 13) return;
        return setParams({ curve: +f[3], rate: f[4], amount: +f[6], width: +f[7],
                           attack: +f[8], decay: +f[9], sustain: +f[10],
                           release: +f[11], widthMs: +f[12] });
      }
      if (tag === MSG.scope) {
        /*
         * "<cols>:<windowMs>:<4 hex pairs per column>" -- a byte per bound,
         * which is finer than the plot can draw and seven times smaller than
         * the "%.3f" text it replaced. That text was ~9.6 KB base64 against an
         * 8192-byte transport that truncates rather than fails, so the sweep
         * lost its tail every frame.
         *
         * ALREADY ROTATED: the plugin walks its ring from `head`, so column 0
         * here is the oldest sample in the window and the last is the newest.
         * The UI does not need to know where the write cursor is.
         */
        const f = msg.split(':');
        if (f.length < 3) return;
        const cols = Math.max(0, Math.min(1024, parseInt(f[0], 10) || 0));
        const windowMs = parseFloat(f[1]) || 0;
        const hex = f[2];
        const out = new Array(cols);
        for (let i = 0; i < cols; i++) {
          const o = i * 8;
          const v = (k) => (parseInt(hex.substr(o + k * 2, 2), 16) || 0) / 127.5 - 1;
          out[i] = [v(0), v(1), v(2), v(3)];
        }
        setScopeWindow(windowMs);
        return setScope(out);
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
   * It is a fixed layout -- the JUCE window was 824 and grew only in height
   * -- and the WebView does not necessarily hand us 824 CSS pixels. In Live
   * it hands us fewer, and the page simply overflowed: the GATE panel cut off
   * after Length, the settings row after Time, and six of the sixteen pads
   * were past the right edge. Reproduced at a 560px viewport in a browser,
   * which is how the cause was found rather than guessed.
   *
   * Scaling keeps every proportion and every one of the original's numbers
   * intact, which laying the design out fluidly would not.
   */
  const DESIGN_W = 856;
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
    const designH = 568 + rows * 40 + (rows - 1) * 8 + 24 + 28;
    const msg = String(Math.ceil(designH * k));
    if (msg !== lastSent) { lastSent = msg; sendMessage(MSG.rows, msg); }
  });

  onMount(() => {
    fit();
    window.addEventListener('resize', fit);
  });

  /* The window's one readout-size number. The Ring card has the COUNT in the
   * middle and the knob that changes it elsewhere -- which is exactly this
   * arrangement, not a duplication to avoid. */
  const centre = () => String(ui().length);

  const stageText = (i) => {
    const p = params();
    if (!p || vals()[P.timeMode] < 0.5) return text()[i];
    const pct = { [P.attack]: p.attack, [P.decay]: p.decay, [P.release]: p.release }[i];
    return pct === undefined ? text()[i] : `${(pct / 100 * p.widthMs).toFixed(1)} ms`;
  };

  return (
    <main>
      {/* The corner mark. Hint style -- the smallest thing the system has, so
        * it sits in the window without competing with anything in it. */}
      <div class="tag t-hint">neon inga</div>
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
              centre={centre()} label="STEPS" />
      </div>
      <div class="env-plot-slot">
        <EnvelopePlot params={plotParams()} w={240} h={104} />
      </div>

      {/* Rate, Length, Amount, Width -- the panel's own order. */}
      <section class="panel gate-panel">
        <h2 class="t-title">GATE</h2>
        <div class="knob-row">
          <Knob idx={P.rate}   label="Rate"   value={vals()[P.rate]}   display={text()[P.rate]} />
          <Knob idx={P.length} label="Length" value={vals()[P.length]} display={text()[P.length]} />
          <Knob idx={P.amount} label="Amount" value={vals()[P.amount]} display={text()[P.amount]} />
          <Knob idx={P.width}  label="Width"  value={vals()[P.width]}  display={text()[P.width]} />
        </div>
      </section>

      <section class="panel env-panel">
        <h2 class="t-title">ENVELOPE</h2>
        <div class="knob-row">
          <Knob idx={P.attack}  label="Attack"  value={vals()[P.attack]}  display={stageText(P.attack)} />
          <Knob idx={P.decay}   label="Decay"   value={vals()[P.decay]}   display={stageText(P.decay)} />
          <Knob idx={P.sustain} label="Sustain" value={vals()[P.sustain]} display={text()[P.sustain]} />
          <Knob idx={P.release} label="Release" value={vals()[P.release]} display={stageText(P.release)} />
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
        <Select idx={P.slot} options={SLOTS} width={96} value={vals()[P.slot]} />
        <Switch idx={P.legato} label="Join Neighbors" value={vals()[P.legato]} />
        <Select idx={P.curve} options={CURVES} label="Curve" labelWidth={44} width={124}
                value={vals()[P.curve]} />
        <Select idx={P.timeMode} options={TIME_MODES} label="Time" labelWidth={36} width={88}
                value={vals()[P.timeMode]} />
        <span class="spacer" />
        <GlyphButton glyph="copy" title="Copy gate config"
                     onClick={() => sendMessage(MSG.requestPatch)} />
        <GlyphButton glyph="paste" title="Paste gate config" onClick={startPaste} />
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
                                       params={plotParams()} w={PLOT_W} h={92}
                                       phase={playPhase()} moving={ui().moving} />}
          {/* The Scope takes no playhead and no pattern: it is a rolling
            * window of wall time, so "now" is always its right-hand edge and
            * there is no step for a mark to sit on. */}
          {tab() === 1 && <Scope scope={scope()} w={PLOT_W} h={92}
                                 windowMs={scopeWindow()} />}
        </div>
        <Tabs tabs={['Pattern', 'Signal']} active={tab()} onSelect={setTab} />
      </div>

      <div class="grid-slot">
        <StepGrid length={ui().length} steps={ui().steps} ties={ui().ties}
                  depths={ui().depths} cursor={ui().cursor}
                  playhead={playStep()} moving={ui().moving} />
      </div>

      <HintBar clauses={[
        ['click', 'a step to toggle'],
        ['shift-click', 'for a tie'],
        ['drag', 'up or down for its amount'],
      ]} />
    </main>
  );
}

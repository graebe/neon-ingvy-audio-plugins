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
import { createSignal, onMount } from 'solid-js';
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

export default function App() {
  const [vals, setVals] = createSignal(new Array(12).fill(0));
  const [text, setText] = createSignal(new Array(12).fill(''));
  const [ui, setUi] = createSignal({ steps: [], ties: [], depths: [], length: 16,
                                     phase: 0, msStep: 0, moving: false, cursor: 0 });
  const [params, setParams] = createSignal(null);
  const [scope, setScope] = createSignal([]);
  const [tab, setTab] = createSignal(0);

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
        return setUi({
          steps: hexToBits(f[0], length), ties: hexToBits(f[1], length),
          length, phase: parseFloat(f[3]) || 0, msStep: parseFloat(f[4]) || 0,
          moving: f[5] === '1', cursor: parseInt(f[6], 10) || 0, depths,
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
        const f = msg.split(':');
        return setScope(f.slice(1).map((c) => c.split(',').map(Number)));
      }
      if (tag === MSG.patch) navigator.clipboard?.writeText(msg);
    });
  });

  const playStep = () => Math.floor(ui().phase) % Math.max(1, ui().length);

  /* The ring's centre is the window's one readout-size number: the step
   * duration, which is what the whole pattern is measured in. */
  const centre = () => ui().msStep ? ui().msStep.toFixed(0) : '—';

  const stageText = (i) => {
    const p = params();
    if (!p || vals()[P.timeMode] < 0.5) return text()[i];
    const pct = { [P.attack]: p.attack, [P.decay]: p.decay, [P.release]: p.release }[i];
    return pct === undefined ? text()[i] : `${(pct / 100 * p.widthMs).toFixed(1)} ms`;
  };

  return (
    <main>
      <div class="ring-col">
        <Ring size={240} length={ui().length} steps={ui().steps}
              playhead={playStep()} moving={ui().moving}
              centre={centre()} label="MS / STEP" />
        <Tabs tabs={['ENVELOPE', 'PATTERN', 'SIGNAL']} active={tab()} onSelect={setTab} />
      </div>

      <div class="plot-slot">
        {tab() === 0 && <EnvelopePlot params={params()} steps={ui().length} />}
        {tab() === 1 && <PatternPlot length={ui().length} steps={ui().steps}
                                     ties={ui().ties} depths={ui().depths}
                                     playhead={playStep()} moving={ui().moving} />}
        {tab() === 2 && <Scope scope={scope()} length={ui().length} />}
      </div>

      <section class="panel gate-panel">
        <h2 class="t-title">GATE</h2>
        <div class="knob-row">
          <Knob idx={P.amount} label="Amount" value={vals()[P.amount]} display={text()[P.amount]} />
          <Knob idx={P.width}  label="Width"  value={vals()[P.width]}  display={text()[P.width]} />
          <Knob idx={P.length} label="Length" value={vals()[P.length]} display={text()[P.length]} />
          <Knob idx={P.rate}   label="Rate"   value={vals()[P.rate]}   display={text()[P.rate]} />
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

      {/* ALL OF IT IN ONE ROW: these were spread over the window and wasted
        * the space; together they read as what they are, the settings that
        * are not knobs. */}
      <div class="settings-row">
        <Select idx={P.slot} options={SLOTS} width={96} label="Slot" />
        <Switch idx={P.legato} label="Join Neighbors" value={vals()[P.legato]} />
        <Select idx={P.curve} options={CURVES} width={124} label="Curve" />
        <Select idx={P.timeMode} options={TIME_MODES} width={88} label="Env" />
        <span class="spacer" />
        <GlyphButton glyph="copy" title="Copy gate config"
                     onClick={() => sendMessage(MSG.requestPatch)} />
        <GlyphButton glyph="paste" title="Paste gate config"
                     onClick={async () => {
                       const t = await navigator.clipboard?.readText();
                       if (t) sendMessage(MSG.patch, t);
                     }} />
      </div>

      <div class="grid-slot">
        <StepGrid length={ui().length} steps={ui().steps} ties={ui().ties}
                  depths={ui().depths} cursor={ui().cursor}
                  playhead={playStep()} moving={ui().moving} />
      </div>

      <HintBar clauses={[
        ['click', 'toggles a step'],
        ['shift-click', 'ties it'],
        ['drag', 'its amount — all the way down turns it off'],
      ]} />
    </main>
  );
}

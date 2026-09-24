/*
 * Trance Gate — the editor.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * A port of the JUCE editor rather than a redesign: the ring, the pads, the
 * three plots, the knob row and the settings row are the ones that were
 * there, drawn to the same Uv tokens.
 */
import { createSignal, onMount, For } from 'solid-js';
import { onParam, onMessage, sendMessage, MSG } from './lib/iplug.js';
import Knob from './lib/Knob.jsx';
import Ring from './lib/Ring.jsx';
import StepGrid from './lib/StepGrid.jsx';
import { EnvelopePlot, PatternPlot, Scope } from './lib/Plots.jsx';
import { Select, Toggle, GlyphButton, Tabs, HintBar } from './lib/Controls.jsx';

/* EParams order, which is the ENGINE's wire order. */
const P = { slot: 0, length: 1, rate: 2, legato: 3, timeMode: 4, curve: 5,
            amount: 6, width: 7, attack: 8, decay: 9, sustain: 10, release: 11 };

const RATES = ['1/1T','1/2','1/2T','1/4','1/4T','1/8','1/8T','1/16','1/16T','1/32','1/32T','1/64','1/128'];
const SLOTS = ['1','2','3','4','5','6','7','8'];
const TIME_MODES = ['ms', '% Step'];
const CURVES = ['Linear', 'Exponential', 'S-Curve'];

/* A 128-bit mask arrives as hex, least significant nibble last. */
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
        /* steps : ties : length : phase : ms_step : advancing : cursor : depths */
        const f = msg.split(':');
        if (f.length < 8) return;
        const length = Math.max(1, parseInt(f[2], 10) || 16);
        const depths = [];
        for (let i = 0; i < length; i++)
          depths.push((parseInt(f[7].substr(i * 2, 2), 16) || 0) / 255);
        return setUi({
          steps: hexToBits(f[0], length), ties: hexToBits(f[1], length),
          length, phase: parseFloat(f[3]) || 0, msStep: parseFloat(f[4]) || 0,
          moving: f[5] === '1', cursor: parseInt(f[6], 10) || 0, depths,
        });
      }

      if (tag === MSG.params) {
        /* slot:legato:time_mode:curve:rate:length:amount:hold:attack:decay:
         * sustain:release:width_ms */
        const f = msg.split(':');
        if (f.length < 13) return;
        return setParams({
          curve: +f[3], rate: f[4],
          amount: +f[6], width: +f[7], attack: +f[8], decay: +f[9],
          sustain: +f[10], release: +f[11], widthMs: +f[12],
        });
      }

      if (tag === MSG.scope) {
        const f = msg.split(':');
        return setScope(f.slice(1).map((c) => c.split(',').map(Number)));
      }

      if (tag === MSG.patch) navigator.clipboard?.writeText(msg);
    });
  });

  const playStep = () => Math.floor(ui().phase) % Math.max(1, ui().length);

  /* The stage knobs read as ms or % depending on Env Time, and ms needs the
   * width -- which needs the rate and the tempo. The plugin serves width_ms
   * for exactly this, rather than the UI recomputing the rate table. */
  const stageText = (i) => {
    const p = params();
    if (!p || vals()[P.timeMode] < 0.5) return text()[i];
    const pct = [p.attack, p.decay, p.release][[P.attack, P.decay, P.release].indexOf(i)];
    if (pct === undefined) return text()[i];
    return `${(pct / 100 * p.widthMs).toFixed(1)} ms`;
  };

  return (
    <main>
      <header>
        <span class="title">TRANCE GATE</span>
        <span class="rate-badge">{params()?.rate ?? ''}</span>
        <span class="ms-badge">{ui().msStep ? `${ui().msStep.toFixed(1)} ms/step` : ''}</span>
      </header>

      <div class="columns">
        <section class="panel left">
          <Ring size={240} length={ui().length} steps={ui().steps} ties={ui().ties}
                depths={ui().depths} playhead={playStep()} moving={ui().moving} />
          <Tabs tabs={['ENVELOPE', 'PATTERN', 'SIGNAL']} active={tab()} onSelect={setTab} />
          {tab() === 0 && <EnvelopePlot params={params()} steps={ui().length} />}
          {tab() === 1 && <PatternPlot length={ui().length} steps={ui().steps}
                                       ties={ui().ties} depths={ui().depths}
                                       playhead={playStep()} moving={ui().moving} />}
          {tab() === 2 && <Scope scope={scope()} length={ui().length} />}
        </section>

        <section class="right">
          <div class="panel">
            <h2>GATE</h2>
            <div class="row">
              <Knob idx={P.amount} label="Amount" value={vals()[P.amount]} display={text()[P.amount]} />
              <Knob idx={P.width}  label="Width"  value={vals()[P.width]}  display={text()[P.width]} />
              <Knob idx={P.length} label="Length" value={vals()[P.length]} display={text()[P.length]} />
            </div>
          </div>

          <div class="panel">
            <h2>ENVELOPE</h2>
            <div class="row">
              <Knob idx={P.attack}  label="Attack"  value={vals()[P.attack]}  display={stageText(P.attack)} />
              <Knob idx={P.decay}   label="Decay"   value={vals()[P.decay]}   display={stageText(P.decay)} />
              <Knob idx={P.sustain} label="Sustain" value={vals()[P.sustain]} display={text()[P.sustain]} />
              <Knob idx={P.release} label="Release" value={vals()[P.release]} display={stageText(P.release)} />
            </div>
          </div>

          {/* ALL OF IT IN ONE ROW. These were spread over the window and
            * wasted the space; together they read as what they are, the
            * settings that are not knobs. */}
          <div class="panel settings">
            <Select idx={P.slot}     label="Slot"  options={SLOTS}      value={vals()[P.slot]} />
            <Select idx={P.rate}     label="Rate"  options={RATES}      value={vals()[P.rate]} />
            <Toggle idx={P.legato}   label="Join Neighbors"             value={vals()[P.legato]} />
            <Select idx={P.curve}    label="Curve" options={CURVES}     value={vals()[P.curve]} />
            <Select idx={P.timeMode} label="Env"   options={TIME_MODES} value={vals()[P.timeMode]} />
            <span class="spacer" />
            <GlyphButton glyph="copy"  title="Copy gate config"
                         onClick={() => sendMessage(MSG.requestPatch)} />
            <GlyphButton glyph="paste" title="Paste gate config"
                         onClick={async () => {
                           const t = await navigator.clipboard?.readText();
                           if (t) sendMessage(MSG.patch, t);
                         }} />
          </div>
        </section>
      </div>

      <section class="panel">
        <h2>PATTERN</h2>
        <StepGrid length={ui().length} steps={ui().steps} ties={ui().ties}
                  depths={ui().depths} cursor={ui().cursor}
                  playhead={playStep()} moving={ui().moving} />
      </section>

      <HintBar clauses={[
        ['click', 'toggle a step'],
        ['shift-click', 'tie'],
        ['drag a pad', 'its amount — all the way down turns it off'],
      ]} />
    </main>
  );
}

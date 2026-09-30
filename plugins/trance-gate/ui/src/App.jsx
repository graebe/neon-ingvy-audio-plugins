/*
 * Trance Gate — the editor.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * The window, top to bottom: the ring and the envelope plot on the left with
 * the three panels beside them, the settings row, the Pattern/Signal band, and
 * the pads, which grow rows rather than shrinking. The window itself -- padding,
 * ground, hint bar, scale and height -- is the kit's EditorFrame; the pieces are
 * in lib/, and this file holds the state they share.
 */
import { createSignal, createMemo } from 'solid-js';
import { EditorFrame, useEditorBridge, createClock } from '@ultraviolet/ui';
import { createParams } from '@ultraviolet/ui/params';
import { MSG, P, NUM_PARAMS } from './lib/msg.js';
import Ring from './lib/Ring.jsx';
import StepGrid from './lib/StepGrid.jsx';
import { EnvelopePlot } from './lib/EnvelopePlot.jsx';
import { Panels } from './lib/Panels.jsx';
import { SettingsRow } from './lib/SettingsRow.jsx';
import { Band } from './lib/Band.jsx';
import { fadeWeights } from './lib/fade.js';
import { setOrder } from './lib/steps.js';
import { decodeUi, decodeEngineParams, decodeScope, decodeGate } from './lib/readouts.js';
import { copyToClipboard } from './lib/clipboard.js';

/* Mirrored by PLUG_WIDTH in config.h: 32 + 760 + 32, the pads decide the 760. */
const DESIGN_W = 824;
/* The block above the pads, from the window's top edge -- app.css lays it out
 * and this has to agree: 32 padding + 452 (three 140px panels, 16 apart) + 16
 * + 28 (settings) + 8 + 92 (band) + 16. */
const ABOVE_GRID = 644;
/* Under the pads: space-6, then the hint bar (28) and the window's bottom
 * padding (16). */
const BELOW_GRID = 24 + 28 + 16;

export default function App() {
  /* Every parameter's value, display string and default -- listening from
   * here, before `ready` is sent. */
  const host = createParams(NUM_PARAMS);
  const [ui, setUi] = createSignal({ steps: [], ties: [], depths: [], orders: [],
                                     length: 16, phase: 0, msStep: 0, moving: false,
                                     cursor: 0 });
  /* The engine's `params` readout, in its own units. */
  const [params, setParams] = createSignal(null);
  const [scope, setScope] = createSignal([]);
  const [scopeWindow, setScopeWindow] = createSignal(1000);
  /* Where the Signal sweep is writing: a mark, not an origin. */
  const [scopeHead, setScopeHead] = createSignal(0);
  /* The rendered gate curve, from the plugin. Null until the first push. */
  const [gate, setGate] = createSignal(null);
  /*
   * ORDER MODE, and how far into the sequence you are. UI-only: the engine
   * holds the order and normalises it after every rank.
   */
  const [orderMode, setOrderMode] = createSignal(false);
  const [named, setNamed] = createSignal({});
  const orderNext = () => Object.keys(named()).length;

  /* THE VISUALISATION'S CLOCK IS THE ENGINE'S: each `ui` push re-anchors it, so
   * the interpolation never drifts further than one idle tick. */
  const clock = createClock();

  const bridge = useEditorBridge({
    onMessage: (tag, msg) => {
      if (tag === MSG.uiState) {
        const u = decodeUi(msg);
        if (!u) return;
        clock.set(u.phase, u.msStep > 0 ? 1 / u.msStep : 0, u.moving);
        setUi(u);
      } else if (tag === MSG.params) {
        const p = decodeEngineParams(msg);
        if (p) setParams(p);
      } else if (tag === MSG.scope) {
        const s = decodeScope(msg);
        if (!s) return;
        setScopeWindow(s.windowMs);
        setScopeHead(s.head);
        setScope(s.cols);
      } else if (tag === MSG.gate) {
        const g = decodeGate(msg);
        if (g) setGate(g);
      } else if (tag === MSG.patch) {
        copyToClipboard(msg);
      }
    },
  });

  /* The playhead, wrapped by the pattern. The pattern plot's line glides on
   * the fraction; a lit wedge and a lit pad snap on the integer, memoised so
   * they notify only when it changes. */
  const playPhase = () => {
    const n = Math.max(1, ui().length);
    const p = clock.position();
    return ((p % n) + n) % n;
  };
  const playStep = createMemo(() => Math.floor(playPhase()) % Math.max(1, ui().length));

  /* The stage percentages come from `params` and the step duration from `ui`. */
  const plotParams = () => {
    const p = params();
    return p ? { ...p, msStep: ui().msStep } : null;
  };

  /*
   * THE FADE'S WEIGHT PER STEP, for the pads, the ring and the Pattern plot.
   * lib/fade.js is the engine's own formula, pinned to a table the engine
   * generates.
   */
  const weights = createMemo(() => {
    const p = params();
    if (!p) return null;
    const u = ui();
    return fadeWeights(u.orders, u.steps, u.length, p.fade ?? 1, !!p.fadeSoft, !!p.fadeOut);
  });
  /* Which kind the fade is introducing: the hits, or under Fade Out the holes. */
  const arriving = (i) => !!params()?.fadeOut !== !!ui().steps?.[i];
  const fading = () => (params()?.fade ?? 1) < 0.999;
  const hits = () => {
    const u = ui();
    let n = 0;
    for (let i = 0; i < u.length; i++) if (arriving(i)) n++;
    return n;
  };

  /* SHUFFLE, as a run of ranks down a shuffled list: each rank inserts and
   * shifts, so the engine needs nothing ORDER mode does not already use. */
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

  const orderModel = () => ({
    orderMode: orderMode(),
    orderNext: orderNext(),
    orderTakes: arriving,
    onOrdered: (i) => setNamed((m) => ({ ...m, [i]: true })),
  });

  /* The grid wraps at 16, so 128 steps is eight rows and the window grows. */
  const designH = createMemo(() => {
    const rows = Math.max(1, Math.ceil(ui().length / 16));
    return ABOVE_GRID + rows * 40 + (rows - 1) * 8 + BELOW_GRID;
  });

  /* THREE CLAUSES IS THE CAP, so ORDER mode SWAPS them for its own. */
  const hint = () => (orderMode() ? [
    ['click', `the ${params()?.fadeOut ? 'gaps' : 'steps'} in the order they should arrive`],
    ['a number', 'to type one — they swap'],
    ['ORDER', 'again to finish'],
  ] : [
    ['click', 'a step to toggle'],
    ['shift-click', 'for a tie'],
    ['drag', 'up or down for its amount'],
  ]);

  return (
    /* The ring, the three panels, the plot band and the pads all emit and
     * reflect the ground's rings. */
    <EditorFrame width={DESIGN_W} height={designH()} motionKey="trance-gate"
                 sources=".panel, .ring, .grid, .band, .plot, [data-wave-source]"
                 bridge={bridge} hint={hint()}>
      <div class="top">
        {/* LEFT: the ring, and the envelope plot ALWAYS under it. */}
        <div class="left-column">
          <Ring size={240} length={ui().length} steps={ui().steps}
                ties={ui().ties} depths={ui().depths} cursor={ui().cursor}
                playhead={playStep()} moving={ui().moving}
                weights={weights()} {...orderModel()}
                centre={String(ui().length)} label="STEPS"
                onLength={(steps) => host.commit(P.length, (steps - 1) / 127)} />
          <EnvelopePlot params={plotParams()} w={240} h={104} />
        </div>
        <Panels host={host}
                orderMode={orderMode()} orderNext={orderNext()} hits={hits()}
                onOrder={() => { setNamed({}); setOrderMode((v) => !v); }} onShuffle={shuffleOrder} />
      </div>

      <SettingsRow host={host} />

      <Band ui={ui()} weights={weights()} gate={gate()} params={plotParams()}
            phase={playPhase()} scope={scope()} scopeWindow={scopeWindow()}
            scopeHead={scopeHead()} />

      <StepGrid length={ui().length} steps={ui().steps} ties={ui().ties}
                depths={ui().depths} cursor={ui().cursor}
                playhead={playStep()} moving={ui().moving}
                orders={ui().orders} weights={weights()}
                fading={fading()} named={named()} {...orderModel()}
                onOrder={setOrder} />
    </EditorFrame>
  );
}

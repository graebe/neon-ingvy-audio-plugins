// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Trance Gate -- the editor, on a Move.
 *
 * The window, top to bottom: the ring and the envelope plot on the left with
 * the three panels beside them, the settings row, the Pattern plot, and the
 * pads, which grow rows rather than shrinking. The window itself -- padding,
 * ground, hint bar, scale -- is the kit's EditorFrame; the pieces are in lib/,
 * and this file holds the state they share.
 *
 * WHAT IT IS TOLD, AND HOW OFTEN. Three readouts, pushed by Schwung's manager
 * at most 150 ms after any write and twice a second besides (lib/move.js):
 * `ui` (the pattern and the playhead), `params` (the sound) and `state` (the
 * patch, from which the page renders its curves with the engine itself). So
 * the playhead is carried forward on the kit's clock between them, and a step
 * edit is drawn as it is sent rather than when the readout confirms it.
 *
 * `renderer` is the engine's curve renderer, (state) -> { gate, envelope },
 * once its wasm has loaded (boot.jsx); until then the plots are empty wells.
 */
import { createSignal, createMemo, createEffect, onCleanup } from 'solid-js';
import { EditorFrame, useEditorBridge, createClock, createFit, scaledHeight } from '@ultraviolet/ui';
import { createParams } from '@ultraviolet/ui/params';
import { MSG, P, NUM_PARAMS, MAX_LENGTH, lengthNorm } from './lib/msg.js';
import Ring from './lib/Ring.jsx';
import StepGrid from './lib/StepGrid.jsx';
import { EnvelopePlot } from './lib/EnvelopePlot.jsx';
import { PatternPlot } from './lib/Plots.jsx';
import { Panels } from './lib/Panels.jsx';
import { SettingsRow } from './lib/SettingsRow.jsx';
import { fadeWeights } from './lib/fade.js';
import { setOrder, onStepEdit } from './lib/steps.js';
import { decodeUi, decodeEngineParams, applyEdit } from './lib/readouts.js';
import { withStages } from './lib/stages.js';
import { createBeats } from '@ultraviolet/ui/beats';
import { INFO } from './lib/info.js';

/* 32 + 760 + 32: the pads decide the 760. */
const DESIGN_W = 824;
/* The block above the pads, from the window's top edge -- app.css lays it out
 * and this has to agree: 32 padding + 468 (three 140px panels, 24 apart) + 24
 * + 28 (settings) + 16 + 92 (plot) + 16. The native editor's 676. */
const ABOVE_GRID = 676;
/* Under the pads: space-6, then the hint bar (28) and the window's bottom
 * padding (16). */
const BELOW_GRID = 24 + 28 + 16;
const PLOT_W = 760;

const EMPTY_UI = { steps: [], ties: [], depths: [], orders: [], length: 16, phase: 0,
                   msStep: 0, moving: false, cursor: 0, slot: 0 };

export default function App(props) {
  const [ui, setUi] = createSignal(EMPTY_UI);
  /* The engine's `params` readout, in its own units. */
  const [params, setParams] = createSignal(null);
  /* The patch, as the engine last published it. */
  const [state, setState] = createSignal('');

  /* Every parameter's value, display string and default -- listening from
   * here, before the transport attaches. The three stages read and parse in
   * the unit Time asks for, against the engine's gate width. */
  const host = withStages(createParams(NUM_PARAMS), {
    stages: [P.attack, P.decay, P.release],
    timeMs: () => host.value(P.timeMode) < 0.5,
    widthMs: () => params()?.widthMs ?? 0,
  });

  /* Set order, and how far into the sequence you are. The editor's own: the
   * engine holds the order and normalises it after every rank. */
  const [orderMode, setOrderMode] = createSignal(false);
  const [named, setNamed] = createSignal({});
  const orderNext = () => Object.keys(named()).length;

  /* THE PLAYHEAD'S CLOCK IS THE ENGINE'S: each `ui` readout re-anchors it,
   * and between them -- half a second while playing -- it runs on. */
  const clock = createClock();

  let ground = null;
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
      } else if (tag === MSG.state) {
        setState(msg);
      }
    },
  });
  /* The frame hands the ground to the bridge; the beats below ring it. */
  const frameBridge = {
    setGround: (handle) => {
      ground = handle;
      bridge.setGround(handle);
    },
  };

  /* A step edit, drawn as it is sent (lib/steps.js). */
  onCleanup(onStepEdit((edit) => setUi((u) => applyEdit(u, edit))));

  /* The two curves, rendered from the patch whenever it moves. */
  const curves = createMemo(() => {
    const render = props.renderer?.();
    const s = state();
    return render && s ? render(s) : null;
  });

  /* The playhead, wrapped by the pattern. The ring, the pads and the plot all
   * snap on the integer, memoised so they notify only when it changes. */
  const playPhase = () => {
    const n = Math.max(1, ui().length);
    const p = clock.position();
    return ((p % n) + n) % n;
  };
  const playStep = createMemo(() => Math.floor(playPhase()) % Math.max(1, ui().length));

  /* THE GROUND RINGS ON THE BEAT, found in the playhead (the kit's lib/beats.js). */
  const beats = createBeats();
  createEffect(() => {
    if (!ui().moving) {
      beats.reset();
      return;
    }
    const ring = beats.next(playPhase(), ui().length, params()?.rate);
    if (ring !== null) ground?.trigger?.(ring);
  });

  /* The Length detents, in steps, as the engine counted them for this slot's
   * Rate. Compared by value, so an unchanged list does not redraw the ticks
   * at every readout. */
  const detents = createMemo(() => params()?.detents ?? [],
                             [], { equals: (a, b) => a.join() === b.join() });

  /* The stage percentages come from `params` and the step duration from `ui`. */
  const plotParams = () => {
    const p = params();
    return p ? { ...p, msStep: ui().msStep } : null;
  };

  /*
   * THE FADE'S WEIGHT PER STEP, for the pads and the ring. lib/fade.js is the
   * engine's own formula, pinned to a table the engine generates.
   */
  const weights = createMemo(() => {
    const p = params();
    if (!p) return null;
    const u = ui();
    return fadeWeights(u.orders, u.steps, u.length, p.fade ?? 1, !!p.fadeSoft, !!p.fadeOut);
  });
  /* Which kind the fade is introducing: the hits, or under Out the holes. */
  const arriving = (i) => !!params()?.fadeOut !== !!ui().steps?.[i];
  const fading = () => (params()?.fade ?? 1) < 0.999;
  const hits = () => {
    const u = ui();
    let n = 0;
    for (let i = 0; i < u.length; i++) if (arriving(i)) n++;
    return n;
  };

  /* SHUFFLE, as a run of ranks down a shuffled list: each rank inserts and
   * shifts, so the engine needs nothing Set order does not already use. */
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

  /*
   * THE PAGE IS AS TALL AS THE WINDOW LOOKS. The frame scales a fixed design
   * width to the iframe with a transform, which a layout box does not see: the
   * window's box stays 824 wide and its design height tall whatever it looks
   * like. Schwung's bridge sizes the iframe from the body's height, so the
   * window sits in a box of the height it is DRAWN at, which clips the
   * unscaled remainder -- or the iframe would be the design's height on a
   * phone, with half of it empty.
   */
  const { scale } = createFit(DESIGN_W);

  /* THREE CLAUSES IS THE CAP, so Set order SWAPS them for its own (the native
   * editor's, InfoLines.h). */
  const hint = () => (orderMode() ? [
    ['click', `the ${params()?.fadeOut ? 'gaps' : 'steps'} in arrival order`],
    ['a number', 'to type one'],
    ['Set order', 'again to finish'],
  ] : [
    ['click', 'a step to toggle'],
    ['shift-click', 'for a tie'],
    ['drag', 'up or down for its amount'],
  ]);

  return (
    <div class="page" style={{ height: `${scaledHeight(designH(), scale())}px` }}>
      {/* The ring, the panels, the plots and the pads all emit and reflect the
        * ground's rings. */}
      <EditorFrame width={DESIGN_W} height={designH()} motionKey="trance-gate"
                   sources=".panel, .ring, .grid, .plot, [data-wave-source]"
                   bridge={frameBridge} hint={hint()} status={null}
                   motionInfo={INFO.motion} signatureInfo={INFO.signature}>
        <div class="top">
          {/* LEFT: the ring, and the envelope plot ALWAYS under it. */}
          <div class="left-column">
            <Ring size={240} length={ui().length} steps={ui().steps}
                  ties={ui().ties} depths={ui().depths} cursor={ui().cursor}
                  playhead={playStep()} moving={ui().moving}
                  weights={weights()} {...orderModel()}
                  centre={String(ui().length)} label="STEPS"
                  maxLength={MAX_LENGTH} detents={detents()}
                  onLength={(steps) => host.commit(P.length, lengthNorm(steps))} />
            <EnvelopePlot params={plotParams()} envelope={curves()?.envelope} w={240} h={104} />
          </div>
          <Panels host={host} rateSteps={props.rateSteps} lengthDetents={detents().map(lengthNorm)}
                  orderMode={orderMode()} orderNext={orderNext()} hits={hits()}
                  onOrder={() => { setNamed({}); setOrderMode((v) => !v); }}
                  onShuffle={shuffleOrder} />
        </div>

        <SettingsRow host={host} />

        <div class="band">
          <PatternPlot length={ui().length} gate={curves()?.gate} amount={params()?.amount}
                       w={PLOT_W} h={92} info={INFO.patternPlot}
                       playStep={playStep()} moving={ui().moving} />
        </div>

        <StepGrid length={ui().length} steps={ui().steps} ties={ui().ties}
                  depths={ui().depths} cursor={ui().cursor}
                  playhead={playStep()} moving={ui().moving}
                  orders={ui().orders} weights={weights()}
                  fading={fading()} named={named()} {...orderModel()}
                  onOrder={setOrder} />
      </EditorFrame>
    </div>
  );
}

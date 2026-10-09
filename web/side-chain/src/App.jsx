// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Side-Chain's editor, on the Move.
 *
 * THE NATIVE EDITOR'S WINDOW (plugins/side-chain/editor/SideChainEditor.h),
 * 760 x 604, laid out as the layout canvas's "NI Side-Chain -- as built"
 * artboard, and reached through Schwung's web manager instead of a VST3 host:
 * the same rows in the same places, the same controls and words, less what the
 * Move cannot have. Each of those is named where it was taken out:
 *
 *   - the Sidechain source, Threshold and Lockout: the Move has no key input
 *     (sc-move/src/params.rs), so its declaration has none of them;
 *   - the audio in the well, and the "no input" warning read off it: the
 *     manager carries values, not audio (lib/Shaper.jsx);
 *   - "no key routed": there is no key to route.
 *
 * ONE ALIGNMENT IS LOAD-BEARING: the shape you drag spans exactly one cycle,
 * and the playhead crosses it where the engine is. The knobs and selects below
 * it are the same parameters as numbers.
 *
 * WHERE THE VALUES COME FROM. Every control is a parameter by index
 * (lib/msg.js), normalised, through the kit's host vocabulary; the transport
 * (web/kit/src/lib/schwung.js) turns that into the module's chain keys. The
 * drawing reads the engine's own values from the module's feeds -- `params`,
 * `ui`, `stage_ms` -- which the manager pushes when they move.
 */
import { For, Show, createEffect, createMemo, createSignal, onCleanup, onMount } from 'solid-js';
import { EditorFrame, useEditorBridge, createClock, createFit, scaledHeight } from '@ultraviolet/ui';
import { createBeats } from '@ultraviolet/ui/beats';
import { createParams, ParamKnob, ParamSelect, ParamToggle } from '@ultraviolet/ui/params';
import { MSG, P, NUM_PARAMS } from './lib/msg.js';
import { shapeFromNorm } from './lib/declaration.js';
import { parseUi, parseParams, parseStageMs } from './lib/feeds.js';
import { Shaper } from './lib/Shaper.jsx';
import { StageKnob, ChoiceKnob } from './lib/Knobs.jsx';
import { bounds } from './lib/shape.js';
import { KNOBS, TRIGGER_ROW, SHAPE_ROW, showsFor } from './lib/layout.js';
import { INFO, CONVENTIONS } from './lib/text.js';
import {
  CYCLE, TriggerWatch, warningFor, stateLine, captionFor, readsInMs, stageText, stageEdit,
  playheadOf, landmarkMs,
} from './lib/status.js';

/* The native editor's designWidth / designHeight. */
const DESIGN_W = 760;
const DESIGN_H = 604;
const PLOT_W = DESIGN_W - 64;
const PLOT_H = 260;

/* Where each stage sits in the `stage_ms` feed. */
const STAGE_AT = { [P.delay]: 0, [P.attack]: 1, [P.hold]: 2, [P.release]: 3 };

/* How often the trigger's silence is looked at: the warning's resolution. */
const WATCH_MS = 250;

export default function App(props) {
  const decl = props.declaration;

  /* Every parameter's value, display string and default -- listening from
   * here, before the transport is connected (boot.jsx). */
  const host = createParams(NUM_PARAMS);

  /* The ENGINE's values, in their own units: what the drawing uses. */
  const [engine, setEngine] = createSignal(null);
  const [ui, setUi] = createSignal({
    source: CYCLE, rate: 4, msCycle: 0, sweep: 0, advancing: 0,
    fires: 0, duck: 0, key: 0, connected: 0, stage: 0, phase: 0,
  });
  const [stageMs, setStageMs] = createSignal([0, 0, 0, 0]);

  const bridge = useEditorBridge({
    onMessage: (tag, text) => {
      if (tag === MSG.uiState) {
        const u = parseUi(text);
        if (u) setUi(u);
      } else if (tag === MSG.params) {
        const f = parseParams(text);
        if (f) setEngine(f);
      } else if (tag === MSG.stageMs) {
        const f = parseStageMs(text);
        if (f) setStageMs(f);
      }
    },
  });

  /* An index from a choice's normalised value, over the module's options. */
  const choice = (idx) => Math.round(host.value(idx) * Math.max(0, decl.options(idx).length - 1));

  /*
   * WHICH SOURCE'S CONTROLS SHOW follows the Source PARAMETER -- the control
   * you just changed -- so the window answers the choice at once rather than
   * when the engine's next feed says so.
   */
  const source = () => choice(P.source);

  /* ------------------------------------------------ the playhead's clock -- */

  /* The playhead moves when the SOURCE is moving, which is not the transport
   * running: a MIDI duck advances with the transport parked. The manager
   * pushes `ui` twice a second at most; the clock carries the position
   * between pushes at the cycle's own rate. */
  const clock = createClock();
  createEffect(() => {
    const u = ui();
    const moving = u.source === CYCLE ? u.advancing > 0 : u.sweep < 1;
    clock.set(u.sweep, u.msCycle > 0 ? 1 / u.msCycle : 0, moving);
  });
  const playhead = () => playheadOf(clock.position(), ui().source);

  /*
   * THE GROUND RINGS ON THE BEAT, found in the playhead (the kit's
   * lib/beats.js), as the Trance Gate's does: on Cycle the sweep is one step
   * of the Rate, so a 1/4 cycle rings once a cycle and a 1/1 four times, full
   * at the cycle's start. A MIDI duck has no beat to ring on.
   */
  let ground = null;
  const frameBridge = {
    setGround: (handle) => {
      ground = handle;
      bridge.setGround(handle);
    },
  };
  const beats = createBeats();
  createEffect(() => {
    const u = ui();
    if (u.source !== CYCLE || !u.advancing) {
      beats.reset();
      return;
    }
    const ring = beats.next(playhead(), 1, decl.options(P.rate)[u.rate]);
    if (ring !== null) ground?.trigger?.(ring);
  });

  /* ---------------------------------------------------------- the warning -- */

  const watch = new TriggerWatch();
  const [quiet, setQuiet] = createSignal(false);
  onMount(() => {
    const t = setInterval(() => {
      const now = performance.now();
      watch.observe(ui().fires, now);
      setQuiet(watch.isQuiet(now));
    }, WATCH_MS);
    onCleanup(() => clearInterval(t));
  });
  const warning = () => warningFor(ui(), quiet());

  /* ------------------------------------------------------------ the shape -- */

  /*
   * DRAWN FROM THE ENGINE'S OWN VALUES, not the normalised ones. Until the
   * first `params` feed the parameters stand in, through the declared ranges:
   * the manager pushes the declared values on subscribing and the feeds on its
   * next look, so this is a moment's difference, not a different picture.
   */
  const shape = createMemo(() => {
    const e = engine();
    if (!e) {
      return shapeFromNorm(host.value,
                           { curve: choice(P.curve), cycle: source() === CYCLE }, decl);
    }
    return {
      curve: e[P.curve] | 0,
      delay: e[P.delay],
      attack: e[P.attack],
      hold: e[P.hold],
      release: e[P.release],
      depth: e[P.depth],
      /* On Cycle, Delay is a phase and wraps; on MIDI it is a wait. */
      cycle: e[P.source] === CYCLE,
    };
  });

  /* The landmark is the instant the duck reaches its floor, at its WRAPPED
   * position. */
  const markMs = () => landmarkMs(bounds(shape()).bottom, ui().msCycle);

  /* ------------------------------------------------------------ the stages -- */

  const inMs = () => readsInMs(host.value(P.timeMode), ui().msCycle);
  const stageReadout = (idx) => stageText(stageMs()[STAGE_AT[idx]], inMs(), host.text(idx));
  const typeStage = (idx, text) => {
    const edit = stageEdit(text, inMs(), ui().msCycle);
    if (!edit) return;
    if (edit.percent !== undefined) host.commit(idx, decl.toNorm(idx, edit.percent));
    else host.sendText(idx, edit.text);
  };
  /* The handles read as the knobs do: the stage in Time's unit, Depth's own. */
  const handleText = (idx) => (STAGE_AT[idx] !== undefined ? stageReadout(idx) : host.text(idx));

  const caption = () => captionFor(ui().msCycle,
                                   inMs() ? stageMs().reduce((a, b) => a + b, 0) : null);

  /* ---------------------------------------------------------- the controls -- */

  const knob = (k) => {
    if (k.kind === 'stage') {
      return <StageKnob params={host} idx={k.idx} label={k.label} info={k.info}
                        readoutInfo={k.readoutInfo} display={stageReadout(k.idx)} bipolar={k.bipolar}
                        onText={(text) => typeStage(k.idx, text)} />;
    }
    if (k.kind === 'choice') {
      return <ChoiceKnob params={host} idx={k.idx} label={k.label} info={k.info}
                         readoutInfo={k.readoutInfo} options={decl.options(k.idx).length} />;
    }
    return <ParamKnob params={host} idx={k.idx} label={k.label} info={k.info}
                      readoutInfo={k.readoutInfo} />;
  };

  const control = (c) => (c.kind === 'toggle'
    ? <ParamToggle params={host} idx={c.idx} label={c.label} info={c.info} />
    : <ParamSelect params={host} idx={c.idx} label={c.label} options={decl.options(c.idx)}
                   width={c.width} labelWidth={c.labelWidth} info={c.info} />);

  const row = (controls) => (
    <div class="selects-row">
      <For each={controls}>{(c) => <Show when={showsFor(c, source())}>{control(c)}</Show>}</For>
    </div>
  );

  /*
   * THE PAGE IS AS TALL AS THE WINDOW IT SHOWS. The manager sizes the iframe
   * from the height the page reports, and the bridge measures the body; the
   * window is a fixed design scaled by a transform, which a layout box does
   * not shrink with. So the window sits in a box of its SCALED size -- the same
   * fit the frame applies -- and the body measures that.
   */
  const { scale } = createFit(DESIGN_W);

  return (
    <div class="fit" style={{ height: `${scaledHeight(DESIGN_H, scale())}px` }}>
      <EditorFrame width={DESIGN_W} height={DESIGN_H} motionKey="side-chain"
                   sources=".plot" bridge={frameBridge} hint={CONVENTIONS}
                   motionInfo={INFO.motion} signatureInfo={INFO.signature}>
        {/* The trigger's state -- not the module's name, which the manager
          * shows -- and the window's single amber mark. */}
        <div class="state-line t-hint">
          <span class="state">
            {stateLine(decl.options(P.source)[ui().source], decl.options(P.rate)[ui().rate],
                       ui().source, ui().stage)}
          </span>
          <Show when={warning()}><span class="warn">{warning()}</span></Show>
        </div>

        <div class="plot-slot">
          <Shaper w={PLOT_W} h={PLOT_H}
                  shape={shape()}
                  heldShape={shapeFromNorm(host.value, shape(), decl)}
                  sweep={playhead()}
                  spanMs={ui().msCycle}
                  markMs={markMs()}
                  caption={caption()}
                  info={INFO.plot}
                  toNorm={decl.toNorm}
                  onReset={(idx) => host.reset(idx)}
                  value={host.value} text={handleText}
                  onCommit={(idx, v) => host.commit(idx, v)} />
        </div>

        <div class="knob-row">
          <For each={KNOBS}>{(k) => <Show when={showsFor(k, source())}>{knob(k)}</Show>}</For>
        </div>

        {row(TRIGGER_ROW)}
        {row(SHAPE_ROW)}
      </EditorFrame>
    </div>
  );
}

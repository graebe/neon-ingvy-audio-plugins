/*
 * NI Side-Chain's editor. Copyright (c) 2026 Torben Gräber. MIT.
 *
 * ONE ALIGNMENT IS LOAD-BEARING: the shape you drag sits directly above the
 * audio it shaped, in one well spanning exactly one cycle. That is the whole
 * editor; the knobs and selects below it are the same parameters as numbers.
 *
 * The window itself -- padding, ground, hint bar, scale, height -- is the kit's
 * EditorFrame; this file is content.
 */
import { createEffect, createMemo, createSignal, onCleanup, onMount, Show } from 'solid-js';
import { EditorFrame, useEditorBridge, createClock } from '@ultraviolet/ui';
import { createParams, ParamKnob, ParamSelect } from '@ultraviolet/ui/params';
import { MSG, P, NUM_PARAMS, shapeFromNorm } from './lib/msg.js';
import { Shaper, SPAN } from './lib/Shaper.jsx';
import { bounds as boundsOf } from './lib/shape.js';
import { decodeScope } from './lib/scope.js';
import { NOTE_NAMES, hintFor } from './lib/text.js';

/* Mirrored by PLUG_WIDTH / PLUG_HEIGHT in config.h. */
const DESIGN_W = 760;
const DESIGN_H = 604;
const PLOT_W = DESIGN_W - 64;

const SOURCES = ['Cycle', 'MIDI', 'Sidechain'];
const TIME_MODES = ['ms', '% of cycle'];
const CURVES = ['Linear', 'Exponential', 'S-Curve'];
const RATES = ['1/1', '1/1T', '1/2', '1/2T', '1/4', '1/4T',
  '1/8', '1/8T', '1/16', '1/16T', '1/32', '1/32T'];
const CHANNELS = ['Omni', '1', '2', '3', '4', '5', '6', '7', '8',
  '9', '10', '11', '12', '13', '14', '15', '16'];
const MIDI_MODES = ['Trigger', 'Gate'];

/* 0 Idle, 1 Delay, 2 Attack, 3 Hold, 4 Release -- params.rs's stage_index. */
const STAGES = ['idle', 'delay', 'attack', 'hold', 'release'];

/*
 * IS ANYTHING COMING IN AT ALL? The floor is one encoding step: a sample
 * crosses the wire as one byte over -1..1, so below 0.0118 (~-38 dBFS) the
 * scope genuinely cannot tell a signal from silence.
 */
const INPUT_FLOOR = 0.012;

export default function App() {
  /* Every parameter's value, display string and default -- listening from here,
   * before `ready` is sent. */
  const host = createParams(NUM_PARAMS);
  /* The ENGINE's values, in their own units: what the drawing uses, so no
   * range arithmetic stands between the picture and the DSP. */
  const [engine, setEngine] = createSignal(null);
  const [ui, setUi] = createSignal({
    source: 0, rate: 4, msCycle: 0, sweep: 0, advancing: 0,
    fires: 0, duck: 0, key: 0, connected: 0, stage: 0, phase: 0,
  });
  const [scope, setScope] = createSignal(null);
  const [seenBits, setSeenBits] = createSignal('');
  const [stageMs, setStageMs] = createSignal([0, 0, 0, 0]);
  const [buses, setBuses] = createSignal({ key: 0, isMain: 0 });

  const bridge = useEditorBridge({
    onMessage: (tag, text) => {
      switch (tag) {
        case MSG.uiState: {
          const f = text.split(':');
          if (f.length < 11) return;
          setUi({
            source: +f[0], rate: +f[1], msCycle: +f[2], sweep: +f[3],
            advancing: +f[4], fires: +f[5], duck: +f[6], key: +f[7],
            connected: +f[8], stage: +f[9], phase: +f[10],
          });
          return;
        }
        case MSG.params: {
          const f = text.split(':').map(Number);
          if (f.length >= NUM_PARAMS) setEngine(f);
          return;
        }
        case MSG.stageMs: {
          const f = text.split(':').map(Number);
          if (f.length >= 4) setStageMs(f);
          return;
        }
        case MSG.buses: {
          const f = text.split(':');
          setBuses({ key: +f[0] || 0, isMain: +f[1] || 0 });
          return;
        }
        case MSG.scope: {
          const s = decodeScope(text);
          if (!s) return;
          setSeenBits(s.seen);
          setScope(s.cols);
          return;
        }
        default:
      }
    },
  });

  /* ------------------------------------------------ the playhead's clock -- */

  /* The playhead moves when the SOURCE is moving, which is not the transport
   * running: a MIDI duck advances with the timeline parked. */
  const clock = createClock();
  createEffect(() => {
    const u = ui();
    const moving = u.source === 0 ? u.advancing > 0 : u.sweep < 1;
    clock.set(u.sweep, u.msCycle > 0 ? 1 / u.msCycle : 0, moving);
  });
  /* A cycle wraps; a one-shot duck stops at its end. */
  const playSweep = () => {
    const s = clock.position();
    return s >= 1 ? (ui().source === 0 ? s % 1 : 1) : s;
  };

  /* ---------------------------------------------------------- the warning -- */

  /*
   * ONE AMBER MARK. A ducker whose trigger is not arriving looks exactly like
   * one set to zero depth, and there are three ways for that to happen: the
   * transport stopped, MIDI Live will not route to an audio track, or a
   * sidechain with nothing patched.
   */
  const [stale, setStale] = createSignal(false);
  onMount(() => {
    let lastFires = -1;
    let lastSeen = performance.now();
    const t = setInterval(() => {
      const f = ui().fires;
      if (f !== lastFires) { lastFires = f; lastSeen = performance.now(); }
      setStale(performance.now() - lastSeen > 800);
    }, 250);
    onCleanup(() => clearInterval(t));
  });

  const seen = (i) => seenBits().charCodeAt(i) === 49; /* '1' */

  const hasInput = createMemo(() => {
    const cols = scope();
    if (!cols) return true; /* nothing pushed yet is not a verdict */
    for (let i = 0; i < cols.length; i++) {
      if (!seen(i)) continue;
      const r = cols[i];
      if (Math.abs(r[0]) > INPUT_FLOOR || Math.abs(r[1]) > INPUT_FLOOR) return true;
    }
    return false;
  });

  const warning = createMemo(() => {
    const u = ui();
    if (u.source === 2 && !buses().key) return 'no key routed';
    if (u.source === 2 && buses().isMain) return 'key is the input';
    /* BEFORE the trigger warnings: a silent track is the more basic fact. */
    if (!hasInput()) return 'no input';
    if (!stale()) return '';
    if (u.source === 0) return u.advancing ? '' : 'transport stopped';
    if (u.source === 1) return 'no midi';
    return 'no trigger';
  });

  /* ------------------------------------------------------------ the shape -- */

  /* DRAWN FROM THE ENGINE'S OWN VALUES, not the normalised ones: converting
   * back would be a second copy of every range. */
  const shape = createMemo(() => {
    const e = engine();
    if (!e) return { curve: 0, delay: 0, attack: 2, hold: 8, release: 35, depth: 100 };
    return {
      curve: e[P.curve] | 0,
      delay: e[P.delay],
      attack: e[P.attack],
      hold: e[P.hold],
      release: e[P.release],
      depth: e[P.depth] * 100, /* the engine holds 0..1, the display a percent */
      /* On Cycle, Delay is a phase and wraps; anywhere else it is a wait. */
      cycle: ui().source === 0,
    };
  });

  /* The cycle in ms is the well's span; the landmark is the instant the duck
   * reaches its floor, at its WRAPPED position. */
  const spanMs = () => ui().msCycle;
  const markMs = () => {
    const bottom = boundsOf(shape()).bottom;
    return bottom > 0 && bottom <= SPAN ? (bottom / SPAN) * spanMs() : 0;
  };

  const source = () => ui().source;

  return (
    /* The shaper's well is the one solid box; the knobs sit on the window. */
    <EditorFrame width={DESIGN_W} height={DESIGN_H} motionKey="side-chain"
                 sources=".plot, [data-wave-source]" bridge={bridge}
                 hint={hintFor(source(), stageMs(), host.value(P.timeMode))}>
      {/* The trigger's state -- not the plugin's name, which the host shows. */}
      <div class="state-line t-hint">
        <span class="state">
          {SOURCES[source()] ?? ''}
          {source() === 0 ? ` ${RATES[ui().rate] ?? ''}` : ''}
          {' · '}
          {STAGES[ui().stage] ?? ''}
        </span>
        {/* The window's single amber mark. */}
        <Show when={warning()}><span class="warn">{warning()}</span></Show>
      </div>

      <div class="plot-slot">
        <Shaper w={PLOT_W} h={260}
                shape={shape()}
                heldShape={shapeFromNorm(host.value, shape())}
                scope={scope()}
                seen={seen}
                quiet={!hasInput()}
                sweep={playSweep()}
                spanMs={spanMs()}
                markMs={markMs()}
                onReset={(idx) => host.reset(idx)}
                value={host.value} text={host.text}
                onCommit={(idx, v) => host.commit(idx, v)} />
      </div>

      <div class="knob-row">
        <ParamKnob params={host} idx={P.depth} label="Depth" />
        <ParamKnob params={host} idx={P.delay} label="Delay" />
        <ParamKnob params={host} idx={P.attack} label="Attack" />
        <ParamKnob params={host} idx={P.hold} label="Hold" />
        <ParamKnob params={host} idx={P.release} label="Release" />
        {/* THE SOURCE'S OWN KNOBS, AND ONLY THE ONES THAT APPLY: a Threshold
          * on a tempo-locked duck invites turning it and concluding the plugin
          * is broken. */}
        <Show when={source() === 1}>
          <ParamKnob params={host} idx={P.velSens} label="Vel" />
        </Show>
        <Show when={source() === 2}>
          <ParamKnob params={host} idx={P.threshold} label="Thresh" />
          <ParamKnob params={host} idx={P.lockout} label="Lockout" />
        </Show>
      </div>

      {/* TWO ROWS, SPLIT BY SUBJECT: the trigger, then the shape every source
        * has -- so the shape controls do not move when the source changes. */}
      <div class="selects-row">
        <ParamSelect params={host} idx={P.source} label="Src" options={SOURCES} width={104} labelWidth={30} />
        <Show when={source() === 0}>
          <ParamSelect params={host} idx={P.rate} label="Rate" options={RATES} width={84} labelWidth={38} />
        </Show>
        <Show when={source() === 1}>
          <ParamSelect params={host} idx={P.note} label="Note" options={NOTE_NAMES} width={78} labelWidth={38} />
          <ParamSelect params={host} idx={P.channel} label="Ch" options={CHANNELS} width={66} labelWidth={26} />
          <ParamSelect params={host} idx={P.midiMode} label="Mode" options={MIDI_MODES} width={88} labelWidth={42} />
        </Show>
      </div>

      <div class="selects-row">
        <ParamSelect params={host} idx={P.curve} label="Curve" options={CURVES} width={118} labelWidth={42} />
        <ParamSelect params={host} idx={P.timeMode} label="Time" options={TIME_MODES} width={92} labelWidth={38} />
      </div>
    </EditorFrame>
  );
}

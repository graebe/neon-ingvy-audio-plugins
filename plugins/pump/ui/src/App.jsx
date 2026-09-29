/*
 * NI Pump's editor.
 *
 * THE LAYOUT IS ABSOLUTE AND THE NUMBERS ARE THE DESIGN. A flex layout that
 * happens to look close is a different drawing, and this one has a load-bearing
 * alignment in it: the shape well and the signal well are the same width and the
 * same span, because the shape you dragged has to sit directly above the audio
 * it shaped. That is the whole editor.
 *
 *   window 760 wide, pad 32, plot 696
 *   tag      y 32   h 16
 *   shape    y 56   h 156
 *   signal   y 220  h 132
 *   axis            under BOTH, not inside either -- see below
 *   knobs    y 376  h 78
 *   selects  y 470  h 28
 *   hint     y 514  h 20
 *
 * THE AXIS IS UNDER THE WELLS AND NOT LAID OVER THEM. The Spectrogram's rule
 * (app.css:77-84): a label over the picture is legible against silence and
 * invisible against a loud passage, and fixing that would need a backing plate,
 * and nothing in this system floats.
 */
import { createEffect, createMemo, createSignal, onCleanup, onMount, Show } from 'solid-js';
import { Hint, onParam, onMessage, sendMessage, Well, Axis, band, INSET }
  from '@ultraviolet/ui';
import { MSG, P, NUM_PARAMS } from './lib/msg.js';
import { ParamKnob, ParamSelect } from './lib/params.jsx';
import { Shaper, SPAN } from './lib/Shaper.jsx';

const DESIGN_W = 760;
const DESIGN_H = 566;
const PLOT_W = DESIGN_W - 64;

const SOURCES = ['Cycle', 'MIDI', 'Sidechain'];
const TIME_MODES = ['ms', '% of cycle'];
const CURVES = ['Linear', 'Exponential', 'S-Curve', 'Pump'];
const RATES = ['1/1', '1/1T', '1/2', '1/2T', '1/4', '1/4T',
  '1/8', '1/8T', '1/16', '1/16T', '1/32', '1/32T'];
const CHANNELS = ['Omni', '1', '2', '3', '4', '5', '6', '7', '8',
  '9', '10', '11', '12', '13', '14', '15', '16'];
const MIDI_MODES = ['Trigger', 'Gate'];

/* 0 Idle, 1 Delay, 2 Attack, 3 Hold, 4 Release -- params.rs's stage_index, whose
 * numbering is a UI contract rather than the enum's order. */
const STAGES = ['idle', 'delay', 'attack', 'hold', 'release'];

/* A nibble table rather than parseInt: this runs over 256 columns every frame,
 * and the Spectrogram's columns.js makes the same trade for the same reason. */
const NIB = new Int8Array(128).fill(-1);
for (let i = 0; i < 16; i++) NIB['0123456789ABCDEF'.charCodeAt(i)] = i;

export default function App() {
  /* Normalised values, as the host reports them. The Knobs want these. */
  const [norm, setNorm] = createSignal(new Array(NUM_PARAMS).fill(0));
  /* The plugin's own display strings -- it owns every unit, precision and enum
   * label, because iPlug2's IParam already does. */
  const [display, setDisplay] = createSignal(new Array(NUM_PARAMS).fill(''));
  /* The ENGINE's values, in their own units. What the drawing uses, so no range
   * arithmetic stands between the picture and the DSP. */
  const [engine, setEngine] = createSignal(null);
  const [ui, setUi] = createSignal({
    source: 0, rate: 4, msCycle: 0, sweep: 0, advancing: 0,
    fires: 0, duck: 0, key: 0, connected: 0, stage: 0, phase: 0,
  });
  const [scope, setScope] = createSignal(null);
  const [seenBits, setSeenBits] = createSignal('');
  const [stageMs, setStageMs] = createSignal([0, 0, 0, 0]);
  const [buses, setBuses] = createSignal({ key: 0, isMain: 0 });
  const [defaults, setDefaults] = createSignal({});

  onMount(() => {
    const offParam = onParam((idx, value) => {
      setNorm((v) => {
        const next = [...v];
        next[idx] = value;
        return next;
      });
      /* The first value a parameter reports is its default, and the only place
       * this side can learn one -- which is what a double-click needs. */
      setDefaults((d) => (idx in d ? d : { ...d, [idx]: value }));
    });

    const offMsg = onMessage((tag, text) => {
      if (tag >= 0 && tag < NUM_PARAMS) {
        setDisplay((v) => {
          const next = [...v];
          next[tag] = text;
          return next;
        });
        return;
      }
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
          if (f.length < NUM_PARAMS) return;
          setEngine(f);
          return;
        }
        case MSG.stageMs: {
          const f = text.split(':').map(Number);
          if (f.length < 4) return;
          setStageMs(f);
          return;
        }
        case MSG.buses: {
          const f = text.split(':');
          setBuses({ key: +f[0] || 0, isMain: +f[1] || 0 });
          return;
        }
        case MSG.scope: {
          /*
           * "<cols>:<seen bits>:<5 hex pairs per column>"
           *
           * A SHORT PAYLOAD IS DROPPED WHOLE rather than drawn in part: the
           * transport truncates rather than fails, and half a picture drawn
           * anyway puts a column of garbage in the middle of it, which reads as
           * a real transient and cannot be told from one.
           */
          const c1 = text.indexOf(':');
          const c2 = text.indexOf(':', c1 + 1);
          if (c1 < 0 || c2 < 0) return;
          const cols = Math.max(0, Math.min(1024, parseInt(text.slice(0, c1), 10) || 0));
          const seen = text.slice(c1 + 1, c2);
          const hex = text.slice(c2 + 1);
          if (seen.length < cols || hex.length < cols * 10) return;

          const out = new Array(cols);
          for (let i = 0; i < cols; i++) {
            const o = (c2 + 1) + i * 10;
            const byteAt = (k) => {
              const hi = NIB[text.charCodeAt(o + k * 2)];
              const lo = NIB[text.charCodeAt(o + k * 2 + 1)];
              return hi < 0 || lo < 0 ? 128 : (hi << 4) | lo;
            };
            out[i] = [
              byteAt(0) / 127.5 - 1, /* dry low  */
              byteAt(1) / 127.5 - 1, /* dry high */
              byteAt(2) / 127.5 - 1, /* wet low  */
              byteAt(3) / 127.5 - 1, /* wet high */
              /* The gain is UNIPOLAR and was encoded as such -- reading it
               * through the bipolar mapping would put unity at 1 and silence at
               * -1, and the trace would draw upside down and twice as tall. */
              byteAt(4) / 255,
            ];
          }
          setSeenBits(seen);
          setScope(out);
          return;
        }
        default:
      }
    });

    /*
     * MANDATORY, AND THE LAST THING onMount DOES.
     *
     * OnUIOpen fires from didFinishNavigation, but a <script type="module"> is
     * DEFERRED and evaluates after the document is done -- so every value the
     * plugin pushed from OnUIOpen landed before globalThis.SPVFD existed and was
     * dropped on the floor. The Trance Gate sat on twelve zeroes until this line
     * existed. It goes last, after both listeners are registered.
     */
    sendMessage(MSG.ready);

    onCleanup(() => { offParam(); offMsg(); });
  });

  /* ------------------------------------------------ the animation clock ---- */

  const [frame, setFrame] = createSignal(0);
  const [anchor, setAnchor] = createSignal({ sweep: 0, at: 0, moving: false, msCycle: 0 });

  createEffect(() => {
    const u = ui();
    setAnchor({
      sweep: u.sweep,
      at: performance.now(),
      /* The playhead moves when the SOURCE is moving, which is not the same as
       * the transport running: a MIDI duck advances with the timeline parked. */
      moving: u.source === 0 ? u.advancing > 0 : u.sweep < 1,
      msCycle: u.msCycle,
    });
  });

  /*
   * THE PLAYHEAD'S CLOCK IS THE ENGINE'S, NOT THE TIMER'S.
   *
   * OnIdle runs on a main-thread timer at IDLE_TIMER_RATE -- 50 Hz at best, and
   * worse under Live's UI load -- so drawing the sweep it reports directly
   * stutters and drifts visibly against the sound. The pushed value is an
   * ANCHOR; the position is interpolated from it against wall time.
   */
  const playSweep = () => {
    const a = anchor();
    if (!a.moving || !(a.msCycle > 0)) return a.sweep;
    frame(); /* the dependency that makes this tick */
    const s = a.sweep + (performance.now() - a.at) / a.msCycle;
    return s >= 1 ? (ui().source === 0 ? s % 1 : 1) : s;
  };

  onMount(() => {
    let live = true;
    /* rAF rather than setInterval: it is the display's own cadence, and it stops
     * when the window is hidden -- which is the whole plugin window in a host
     * tab that is not showing. */
    const tick = () => {
      if (!live) return;
      if (anchor().moving) setFrame((n) => n + 1);
      requestAnimationFrame(tick);
    };
    requestAnimationFrame(tick);
    onCleanup(() => { live = false; });
  });

  /* ---------------------------------------------------------- the warning */

  /*
   * ONE AMBER MARK, AND IT IS THE MOST USEFUL THING IN THE WINDOW.
   *
   * A ducker whose trigger is not arriving looks exactly like one set to zero
   * depth, and Pump has three separate ways for that to happen: the transport
   * stopped, MIDI that Live will not route to an audio track, or a sidechain
   * with nothing patched. The Spectrogram's watchdog idiom, and its reasoning:
   * "a spectrogram's whole claim is that what you read is what was measured".
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

  const warning = createMemo(() => {
    const u = ui();
    if (u.source === 2 && !buses().key) return 'no key routed';
    if (u.source === 2 && buses().isMain) return 'key is the input';
    if (!stale()) return '';
    if (u.source === 0) return u.advancing ? '' : 'transport stopped';
    if (u.source === 1) return 'no midi';
    return 'no trigger';
  });

  /* ------------------------------------------------------------ the shape */

  /*
   * DRAWN FROM THE ENGINE'S OWN VALUES, not from the normalised ones.
   *
   * Converting 0..1 back into percentages here would mean a second copy of every
   * parameter's range, and a range that drifts puts the handle somewhere the
   * sound is not. The `params` readout carries the engine's numbers; this just
   * names them.
   */
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
    };
  });

  const seen = (i) => seenBits().charCodeAt(i) === 49; /* '1' */

  /* The cycle in ms is the span of BOTH wells, and the landmark is the point the
   * duck reaches its floor -- the one instant in the shape a listener can name. */
  const spanMs = () => ui().msCycle;
  const markMs = () => {
    const s = shape();
    const end = s.delay + s.attack;
    return end > 0 && end <= SPAN ? (end / SPAN) * spanMs() : 0;
  };

  /* -------------------------------------------------------------- layout */

  /*
   * THE DESIGN IS 760 WIDE AND IS SCALED TO WHATEVER VIEWPORT IT GETS.
   *
   * Live hands over fewer pixels than asked for, and a page that simply
   * overflowed would cut off the right-hand controls. Scaling keeps every
   * proportion and every one of these numbers intact, which laying the design
   * out fluidly would not.
   */
  const fit = () => {
    const el = document.querySelector('main');
    if (!el) return 1;
    const k = Math.max(0.1, (window.innerWidth || DESIGN_W) / DESIGN_W);
    el.style.transformOrigin = 'top left';
    el.style.transform = `scale(${k})`;
    return k;
  };

  let lastSent = '';
  createEffect(() => {
    const k = fit() ?? 1;
    const msg = String(Math.ceil(DESIGN_H * k));
    if (msg !== lastSent) { lastSent = msg; sendMessage(MSG.height, msg); }
  });
  onMount(() => {
    const onResize = () => fit();
    window.addEventListener('resize', onResize);
    onCleanup(() => window.removeEventListener('resize', onResize));
  });

  const v = (idx) => norm()[idx];
  const d = (idx) => display()[idx];

  return (
    <main>
      <div class="tag t-hint">
        NI PUMP
        <span class="tag-state">
          {SOURCES[ui().source] ?? ''}
          {ui().source === 0 ? ` ${RATES[ui().rate] ?? ''}` : ''}
          {' · '}
          {STAGES[ui().stage] ?? ''}
        </span>
        {/* The window's single amber mark. */}
        <Show when={warning()}><span class="tag-warn">{warning()}</span></Show>
      </div>

      <div class="shape-slot">
        <Shaper w={PLOT_W} h={156}
                shape={shape()}
                scope={scope()}
                seen={seen}
                sweep={playSweep()}
                spanMs={spanMs()}
                markMs={markMs()}
                defaults={defaults()} />
      </div>

      <div class="signal-slot">
        <SignalWell w={PLOT_W} h={132} scope={scope()} seen={seen}
                    spanMs={spanMs()} markMs={markMs()} />
      </div>

      <section class="panel knob-row">
        <ParamKnob idx={P.depth} label="Depth" value={v(P.depth)} display={d(P.depth)}
                   default={defaults()[P.depth]} />
        <ParamKnob idx={P.delay} label="Delay" value={v(P.delay)} display={d(P.delay)}
                   default={defaults()[P.delay]} />
        <ParamKnob idx={P.attack} label="Attack" value={v(P.attack)} display={d(P.attack)}
                   default={defaults()[P.attack]} />
        <ParamKnob idx={P.hold} label="Hold" value={v(P.hold)} display={d(P.hold)}
                   default={defaults()[P.hold]} />
        <ParamKnob idx={P.release} label="Release" value={v(P.release)}
                   display={d(P.release)} default={defaults()[P.release]} />
        {/* The source's own control, whichever source that is. Shown here rather
          * than in a separate panel because it is the one knob whose relevance
          * changes, and hiding it entirely would make the row jump. */}
        <Show when={ui().source === 2}
              fallback={
                <ParamKnob idx={P.velSens} label="Vel" value={v(P.velSens)}
                           display={d(P.velSens)} default={defaults()[P.velSens]} />
              }>
          <ParamKnob idx={P.threshold} label="Thresh" value={v(P.threshold)}
                     display={d(P.threshold)} default={defaults()[P.threshold]} />
        </Show>
      </section>

      <div class="selects-row">
        <ParamSelect idx={P.source} label="Src" options={SOURCES}
                     value={v(P.source)} width={104} labelWidth={30} />
        <Show when={ui().source === 0}>
          <ParamSelect idx={P.rate} label="Rate" options={RATES}
                       value={v(P.rate)} width={84} labelWidth={38} />
        </Show>
        <Show when={ui().source === 1}>
          <ParamSelect idx={P.note} label="Note" options={noteNames()}
                       value={v(P.note)} width={78} labelWidth={38} />
          <ParamSelect idx={P.channel} label="Ch" options={CHANNELS}
                       value={v(P.channel)} width={66} labelWidth={26} />
          <ParamSelect idx={P.midiMode} label="Mode" options={MIDI_MODES}
                       value={v(P.midiMode)} width={88} labelWidth={42} />
        </Show>
        <Show when={ui().source === 2}>
          <ParamKnob idx={P.lockout} label="Lockout" value={v(P.lockout)}
                     display={d(P.lockout)} default={defaults()[P.lockout]} />
        </Show>
        <div class="spacer" />
        <ParamSelect idx={P.curve} label="Curve" options={CURVES}
                     value={v(P.curve)} width={118} labelWidth={42} />
        <ParamSelect idx={P.timeMode} label="Time" options={TIME_MODES}
                     value={v(P.timeMode)} width={92} labelWidth={38} />
      </div>

      <Hint clauses={hintFor(ui().source, ui().rate, stageMs(), v(P.timeMode))} />
    </main>
  );
}

/*
 * THE SIGNAL WELL: the input in grey behind, the output in front.
 *
 * `band` is the kit's, and it is the same primitive twice -- which is the point
 * of it being in the kit. The dry is CONTEXT, NOT THE SUBJECT, so it goes behind
 * at partial alpha: a sustained input fills every column edge to edge, and at
 * full strength it is a solid slab with the ducked trace fighting to be seen
 * through it.
 */
function SignalWell(props) {
  const AXIS_H = 14;
  const geom = () => ({
    x0: INSET,
    w: Math.max(1, Math.round(props.w - 2 * INSET)),
    top: 18,
    bottom: props.h - INSET - AXIS_H,
  });
  const mid = () => (geom().top + geom().bottom) / 2;

  return (
    <Well w={props.w} h={props.h}
          caption="SIGNAL   ONE CYCLE, ALIGNED WITH THE SHAPE ABOVE   INPUT IN GREY">
      {/* The zero line, so a silent stretch reads as silence rather than as a
        * gap in the drawing. */}
      <line x1={INSET} x2={props.w - INSET} y1={mid()} y2={mid()}
            stroke="var(--line-100)" />
      <path d={band(props.scope, 0, 1, geom(), props.seen)}
            fill="var(--scope-dry)" opacity="0.5" />
      {/*
        * THE DUCKED TRACE IS THE SUBJECT, so it gets the arc halo -- the same
        * 3px falloff the knob's value arc uses, and for the same reason: `uv` is
        * a near-white and a near-white drawn alone has no cast at all. The 10px
        * LED halo is too wide here; on a trace that fills the well it blooms.
        */}
      <g class="glow-arc">
        <path d={band(props.scope, 2, 3, geom(), props.seen)} fill="var(--uv)" />
      </g>
      <Axis w={props.w} y={props.h - INSET - AXIS_H} spanMs={props.spanMs}
            markMs={props.markMs} />
    </Well>
  );
}

/* Live's octave numbering, where 36 is C1 -- the same table Pump.cpp generates
 * for the host's own menu. Built once. */
let NOTE_NAMES = null;
function noteNames() {
  if (NOTE_NAMES) return NOTE_NAMES;
  const n = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
  NOTE_NAMES = Array.from({ length: 128 }, (_, i) => `${n[i % 12]}${Math.floor(i / 12) - 2}`);
  return NOTE_NAMES;
}

/*
 * THE HINT BAR STATES THIS WINDOW'S CONVENTIONS ONCE, in the design system's
 * pattern: the verb in `ink`, the rest in `ink-muted`, at most three clauses.
 *
 * The third clause is the thing a reader cannot work out by looking: that the
 * times are percentages of the cycle, and what that is in milliseconds right
 * now. `Time` switches which of the two the clause names.
 */
function hintFor(source, rate, ms, timeModeNorm) {
  const showMs = (timeModeNorm ?? 0) < 0.5;
  const total = ms.reduce((a, b) => a + b, 0);
  const clauses = [['drag a handle', 'shift for fine, double-click to reset']];
  if (source === 1) {
    clauses.push(['midi', 'needs a MIDI track -- Live routes none to an audio track']);
  } else if (source === 2) {
    clauses.push(['sidechain', 'pick a source in the device header']);
  } else {
    clauses.push(['cycle', 'locked to the transport, no routing needed']);
  }
  clauses.push(showMs
    ? ['times', `percent of the cycle -- ${Math.round(total)} ms in total`]
    : ['times', 'percent of the cycle, so the shape follows the tempo']);
  return clauses;
}

/*
 * NI Listen-In — the editor.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * Two controls and a meter. The plugin decides everything; this file shows what
 * it decided and sends back the two things a person can change.
 *
 * THE ONE THING WORTH BEING CAREFUL ABOUT is that the slot is a HOST PARAMETER
 * and the name is not. The Select drives the parameter through the kit's bridge
 * -- so the host records it, saves it with the set, and can automate it --
 * while the name travels as a message and is serialised by the plugin itself.
 * Two different mechanisms, deliberately: a name is not a number.
 *
 * THE PLUGIN IS THE ONE SOURCE OF TRUTH for what the bus is doing, including
 * the slot. It pushes state sixty times a second and this file only ever
 * displays it -- so a slot changed by automation, by a preset recall or by
 * another editor needs no separate path to arrive here. It already does.
 */
import { createSignal, onMount, onCleanup } from 'solid-js';
import { onMessage, sendMessage, setParam, beginGesture, endGesture } from '@ultraviolet/ui';
import { Ground, Hint, Select, createMotion } from '@ultraviolet/ui';
import { MSG, STATUS } from './lib/msg.js';
import { decodeState, meterFraction, SLOTS, STATUS_TEXT } from './lib/state.js';
import Meter from './lib/Meter.jsx';
import NameField from './lib/NameField.jsx';

/* Mirrored by PLUG_WIDTH in config.h and by `main` in app.css. */
const DESIGN_W = 360;
/* kSlot in ListenIn.h. The bridge addresses parameters by index. */
const P_SLOT = 0;

/* The parameter wire is NORMALISED 0..1 in both directions -- see ParamSelect
 * in the Trance Gate's editor, which states the same conversion for the same
 * reason. Sixteen slots, so slot 1 is 0.0 and slot 16 is 1.0. */
const toNorm = (slot) => (SLOTS.length > 1 ? (slot - 1) / (SLOTS.length - 1) : 0);

export default function App() {
  const [state, setState] = createSignal({ slot: 1, status: STATUS.idle, peak: 0 });
  const [label, setLabel] = createSignal('');
  /* The Motion switch, remembered between openings. Not a host parameter -- see
   * the kit's lib/motion.js for why a view is not something to automate. */
  const [motion, setMotion] = createMotion('listen-in');
  /* The ground's handle, set by <Ground ref>. A kick arrives as a message and is
   * handed straight to it. */
  let ground = null;

  /*
   * THE PAGE IS SCALED, NOT LAID OUT FLUIDLY -- the Trance Gate's approach for
   * the Trance Gate's reason: the numbers in app.css ARE the design, and a
   * fluid layout that happens to look close is a different drawing.
   */
  const fit = () => {
    const el = document.querySelector('main');
    if (!el) return;
    const k = Math.max(0.1, (window.innerWidth || DESIGN_W) / DESIGN_W);
    el.style.transformOrigin = 'top left';
    el.style.transform = `scale(${k})`;
  };

  onMount(() => {
    const offMsg = onMessage((tag, text) => {
      if (tag === MSG.state) {
        const next = decodeState(text);
        /* A malformed payload keeps what we had. A dropped frame should look
         * like a dropped frame, not like a bus that went idle. */
        if (next) setState(next);
      } else if (tag === MSG.label) {
        setLabel(text ?? '');
      } else if (tag === MSG.ground) {
        /* One message, one ring. A malformed payload is dropped rather than
         * turned into a full-strength kick. */
        const strength = Number.parseFloat(text);
        if (Number.isFinite(strength)) ground?.trigger(strength);
      }
    });

    window.addEventListener('resize', fit);
    fit();

    /* LAST, and it must be: see MSG.ready. */
    sendMessage(MSG.ready);

    onCleanup(() => {
      offMsg();
      window.removeEventListener('resize', fit);
    });
  });

  /* A gesture around the change is what lets a host record it as one edit
   * rather than as a value that appeared from nowhere. */
  const chooseSlot = (i) => {
    const slot = SLOTS[i];
    beginGesture(P_SLOT);
    setParam(P_SLOT, toNorm(slot));
    endGesture(P_SLOT);
  };

  const commitLabel = (text) => {
    setLabel(text);
    sendMessage(MSG.label, text);
  };

  const live = () => state().status === STATUS.live;

  const hints = () => {
    const s = state();
    switch (s.status) {
      /*
       * TWO CLAUSES, NOT THREE, SINCE THE MOTION SWITCH ARRIVED. This window is
       * 360px wide: three clauses, a ~110px signature and the switch do not fit
       * in the 296 that leaves, and the hint bar's own note says a bar that
       * truncates is a bar whose hints are too long -- the fix is fewer clauses,
       * not a smaller font. The dropped ones said the least: that a bus can be
       * read from any plugin is in the documentation, and "pick a free bus" is
       * what the status line already says.
       */
      case STATUS.live:
        return [['bus', String(s.slot)], ['name', label() || 'unnamed']];
      case STATUS.taken:
        return [['bus', `${s.slot} is taken`], ['another', 'Listen-In holds it']];
      case STATUS.unavailable:
        return [['bus', 'unavailable'], ['why', 'the host may be sandboxed']];
      default:
        return [['bus', String(s.slot)], ['status', 'starting']];
    }
  };

  return (
    <main>
      {/* FIRST CHILD OF THE WINDOW, which is the design system's contract for a
        * Ground. There are no panels in this window, so the only thing that
        * emits and reflects is the window border itself. */}
      <Ground enabled={motion()} ref={(h) => { ground = h; }} />

      <div class="row title-row">
        <span class="title t-value">NI Listen-In</span>
        <span
          class="status t-label"
          classList={{ 'status-warn': state().status !== STATUS.live }}
        >
          {STATUS_TEXT[state().status]}
        </span>
      </div>

      <div class="row controls">
        <Select
          label="Bus"
          labelWidth={28}
          width={64}
          options={SLOTS.map(String)}
          value={SLOTS.indexOf(state().slot)}
          onChange={chooseSlot}
        />
        <NameField value={label()} onCommit={commitLabel} />
      </div>

      <div class="row">
        <Meter value={meterFraction(state().peak)} active={live()} />
      </div>

      <Hint clauses={hints()} motion={motion()} onMotion={setMotion} />
    </main>
  );
}

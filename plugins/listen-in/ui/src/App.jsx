/*
 * Listen-In — the editor.
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
import { Hint, Select } from '@ultraviolet/ui';
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
      case STATUS.live:
        return [['bus', String(s.slot)], ['name', label() || 'unnamed'],
                ['read it', 'from any plugin']];
      case STATUS.taken:
        return [['bus', `${s.slot} is taken`], ['another', 'Listen-In holds it'],
                ['fix', 'pick a free bus']];
      case STATUS.unavailable:
        return [['bus', 'unavailable'], ['why', 'the host may be sandboxed']];
      default:
        return [['bus', String(s.slot)], ['status', 'starting']];
    }
  };

  return (
    <main>
      <div class="row title-row">
        <span class="title t-value">Listen-In</span>
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

      <Hint clauses={hints()} />
    </main>
  );
}

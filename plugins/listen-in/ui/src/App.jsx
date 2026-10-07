// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Listen-In — the editor.
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
import { createSignal } from 'solid-js';
import { sendMessage, EditorFrame, useEditorBridge } from '@ultraviolet/ui';
import { createParams, ParamSelect } from '@ultraviolet/ui/params';
import { MSG, STATUS } from './lib/msg.js';
import { decodeState, meterFraction, SLOTS, STATUS_TEXT } from './lib/state.js';
import Meter from './lib/Meter.jsx';
import NameField from './lib/NameField.jsx';

/* Mirrored by PLUG_WIDTH and PLUG_HEIGHT in config.h. */
const DESIGN_W = 360;
const DESIGN_H = 232;
/* kSlot in ListenIn.h. The bridge addresses parameters by index. */
const P_SLOT = 0;
const SLOT_NAMES = SLOTS.map(String);

export default function App() {
  /* The Bus is the one host parameter; the store binds the Select to it. */
  const host = createParams(1);
  const [state, setState] = createSignal({ slot: 1, status: STATUS.idle, peak: 0 });
  const [label, setLabel] = createSignal('');

  const bridge = useEditorBridge({
    onMessage: (tag, text) => {
      if (tag === MSG.state) {
        const next = decodeState(text);
        /* A malformed payload keeps what we had. A dropped frame should look
         * like a dropped frame, not like a bus that went idle. */
        if (next) setState(next);
      } else if (tag === MSG.label) {
        setLabel(text ?? '');
      }
    },
  });

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
    /* No panels in this window, so the only thing the ground's rings reflect
     * off is the window border itself. */
    <EditorFrame width={DESIGN_W} height={DESIGN_H} motionKey="listen-in"
                 bridge={bridge} hint={hints()}>
      {/* The window does not repeat the plugin's name -- the host shows it. The
        * status word is the one fact a person opens this window to read. */}
      <div class="row status-row">
        <span class="status t-label"
              classList={{ 'status-warn': state().status !== STATUS.live }}>
          {STATUS_TEXT[state().status]}
        </span>
      </div>

      <div class="row controls">
        <ParamSelect params={host} idx={P_SLOT} label="Bus" labelWidth={28} width={64}
                     options={SLOT_NAMES} />
        <NameField value={label()} onCommit={commitLabel} />
      </div>

      <div class="row">
        <Meter value={meterFraction(state().peak)} active={live()} />
      </div>
    </EditorFrame>
  );
}

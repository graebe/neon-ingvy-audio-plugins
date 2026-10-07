// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Listen-In, faked, for reviewing the editor without a host.
 *
 * A classic script, so it runs BEFORE the editor's deferred module -- the
 * first push below lands on globals that do not exist yet and is dropped,
 * exactly as the plugin's push from OnUIOpen is. The reply to `ready` is where
 * the state actually arrives.
 *
 *   ?status=0..3   idle, listening, slot taken, unavailable (default 1)
 *   ?slot=1..16    the bus (default 3)
 *   ?label=text    its name (default "Bass")
 */
const b64 = (s) => btoa(String.fromCharCode(...new TextEncoder().encode(s)));
const Q = new URLSearchParams(location.search);
const MSG = { state: 64, label: 96, defaults: 113, ready: 120, setText: 121 };

let slot = Number(Q.get('slot') ?? 3);
const status = Number(Q.get('status') ?? 1);
let label = Q.get('label') ?? 'Bass';

/* A slow wobble, so the meter is seen to move. */
let t = 0;
const peak = () => (0.35 + 0.3 * Math.abs(Math.sin(t / 9))).toFixed(4);
const send = (tag, text) => globalThis.SAMFD?.(tag, 0, b64(text));

function pushAll() {
  send(MSG.defaults, '0');
  globalThis.SPVFD?.(0, (slot - 1) / 15);
  send(0, String(slot));
  send(MSG.state, `${slot}:${status}:${peak()}`);
  send(MSG.label, label);
}

pushAll();   /* dropped: the editor does not exist yet */
setInterval(() => { t++; send(MSG.state, `${slot}:${status}:${peak()}`); }, 50);

window.__sent = [];
window.IPlugSendMsg = (m) => {
  window.__sent.push(m);
  if (m?.msg === 'SAMFUI' && m.msgTag === MSG.ready) { pushAll(); return; }
  if (m?.msg === 'SAMFUI' && m.msgTag === MSG.label) { label = atob(m.data); return; }
  /* The plugin publishes the new slot on its next tick; no SPVFD echo, which is
   * what iPlug2 does for a write from the UI. */
  if (m?.msg === 'SPVFUI' && m.paramIdx === 0) slot = Math.round(m.value * 15) + 1;
};

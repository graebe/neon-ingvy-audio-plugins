// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The plots' curves, rendered by the engine in the page.
 *
 * In Live the plugin rendered both through a scratch engine and pushed the
 * bytes; on the Move no audio and no bytes reach the page, only the patch (the
 * `state` readout). So the page runs the same two functions itself -- the
 * real tg_core_render_gate and tg_core_render_envelope, compiled to wasm in
 * engines/trance-gate/crates/tg-web -- and hands their bytes to the decoders
 * the plugin's bytes went to (lib/capture.js). Nothing about the sound is
 * reasoned about here.
 *
 *   renderCurves(capi, state) -> { gate, envelope }   each decoded, or null
 */
import { decodeGate, decodeEnvelope } from './capture.js';

/* The buffers the C ABI asks for: TG_GATE_MAX and TG_ENVELOPE_MAX
 * (tg-capi/src/engine.rs). */
export const GATE_MAX = 4096;
export const ENVELOPE_MAX = 1024;

/** Both curves for a patch, from the engine; null for each it could not draw. */
export function renderCurves(capi, state) {
  if (!state) return { gate: null, envelope: null };
  const { fn } = capi;
  return capi.withString(state, (s) => {
    const gate = capi.withBuffer(GATE_MAX, (buf, n) => fn.tg_core_render_gate(s, buf, n));
    const envelope = capi.withBuffer(ENVELOPE_MAX, (buf, n) => fn.tg_core_render_envelope(s, buf, n));
    return {
      gate: gate ? decodeGate(gate) : null,
      envelope: envelope ? decodeEnvelope(envelope) : null,
    };
  });
}

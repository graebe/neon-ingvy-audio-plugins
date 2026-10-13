// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Side-Chain's engine as the Move module runs it, in the page: the real
 * sc_core_* C ABI compiled to wasm (engines/side-chain/crates/sc-web), behind
 * the three calls the kit's fake manager makes of a module (fake-schwung.js):
 *
 *   set(key, value)    the module's set_param: sc_core_set_param
 *   get(key)           its get_param: sc_core_get_param, as text, or null
 *   advance(ms)        its audio callback, run for `ms` of audio
 *
 * plus what the harness drives a module with from outside:
 *
 *   transport({ running, bpm, beats })   the host's transport
 *   note(note, velocity, channel)        a MIDI note on or off, at offset 0
 *
 * AS THE MOVE RUNS IT, NOT AS THE PLUGIN DOES: 44.1 kHz in blocks of 128
 * frames (ni_schwung's), with the host's transport on every block and MIDI at
 * offset 0 -- the v2 API has no offset to give. So the playhead the page draws
 * comes from the engine's own sweep, advancing as it does on the device.
 *
 * `web_ui` is answered here because on the Move the SHELL answers it, not the
 * engine: ni_schwung serves WEB_UI_VERSION for every module
 * (engines/shared/crates/ni-schwung/src/lib.rs).
 *
 * Plain JavaScript over capi.js, so `node --test` runs it on the same wasm.
 */

export const SAMPLE_RATE = 44100;
export const BLOCK = 128;
/* sc_core.h's SC_STATE_MAX: sized for the longest readout, `params`. */
const STATE_MAX = 4096;
const WEB_UI_VERSION = '1';

/*
 * sc_transport_t (ni_dsp::ffi::CTransport), repr(C) on wasm32:
 *   int running @0, double beats @8, float bpm @16 -- 24 bytes.
 */
const TRANSPORT_SIZE = 24;

export function createEngine(capi, { sampleRate = SAMPLE_RATE, bpm = 120, running = true } = {}) {
  const f = capi.fn;
  const core = f.sc_core_create(sampleRate);
  /* Memory the engine's calls use on every block, kept for the engine's life:
   * one interleaved stereo block of silence in, the transport, a MIDI message. */
  const audio = f.sc_web_alloc(BLOCK * 2 * 4);
  const transportAt = f.sc_web_alloc(TRANSPORT_SIZE);
  const midiAt = f.sc_web_alloc(4);

  const clock = { running, bpm, beats: 0 };
  let carry = 0;

  /* Views are made after any allocation could have grown the memory. */
  function block() {
    new Float32Array(f.memory.buffer, audio, BLOCK * 2).fill(0);
    const t = new DataView(f.memory.buffer, transportAt, TRANSPORT_SIZE);
    t.setInt32(0, clock.running ? 1 : 0, true);
    t.setFloat64(8, clock.beats, true);
    t.setFloat32(16, clock.bpm, true);
    f.sc_core_process_f32(core, audio, BLOCK, transportAt);
    if (clock.running) clock.beats += (BLOCK / sampleRate) * (clock.bpm / 60);
  }

  return {
    set(key, value) {
      capi.withString(key, (k) => capi.withString(String(value), (v) => f.sc_core_set_param(core, k, v)));
    },

    get(key) {
      if (key === 'web_ui') return WEB_UI_VERSION;
      const out = capi.withBuffer(STATE_MAX, (ptr, n) =>
        capi.withString(key, (k) => f.sc_core_get_param(core, k, ptr, n)));
      return out ? capi.text(out) : null;
    },

    /* Whole blocks only, as a host calls them; the remainder waits for the
     * next call rather than being dropped, so time is never lost. */
    advance(ms) {
      carry += (ms * sampleRate) / 1000;
      while (carry >= BLOCK) {
        block();
        carry -= BLOCK;
      }
    },

    transport({ running: r, bpm: b, beats } = {}) {
      if (r !== undefined) clock.running = !!r;
      if (b > 0) clock.bpm = b;
      if (Number.isFinite(beats)) clock.beats = beats;
      return { ...clock };
    },

    note(note, velocity = 100, channel = 1) {
      const status = (velocity > 0 ? 0x90 : 0x80) | ((channel - 1) & 0x0f);
      new Uint8Array(f.memory.buffer, midiAt, 3).set([status, note & 0x7f, velocity & 0x7f]);
      f.sc_core_on_midi(core, midiAt, 3, 0);
    },

    destroy() {
      f.sc_core_destroy(core);
      f.sc_web_free(audio, BLOCK * 2 * 4);
      f.sc_web_free(transportAt, TRANSPORT_SIZE);
      f.sc_web_free(midiAt, 4);
    },
  };
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The module's DSP, in the browser: the engine the fake Move runs (the
 * kit's lib/fake-schwung.js), for the harness and the tests. Never in the
 * built page -- on a Move the module itself is the engine.
 *
 *   wasmEngine(capi, { sampleRate, bpm }) -> {
 *     set(key, value)    tg_core_set_param, as the module's set_param
 *     get(key)           tg_core_get_param; null for a key it does not serve
 *     advance(ms)        that much audio through tg_core_process_f32, the
 *                        transport running, so the playhead moves
 *     play(), stop()     the transport, as the Move's Play button
 *     dispose()
 *   }
 *
 * THE TRANSPORT IS THE C STRUCT, laid out by hand: tg_transport_t is
 * ni_dsp::ffi::CTransport, #[repr(C)] { running: c_int, beats: f64, bpm: f32 },
 * which on wasm32 is running at 0, beats at 8 (f64's alignment), bpm at 16,
 * 24 bytes in all.
 */

const TRANSPORT_BYTES = 24;
const BLOCK = 128;
/* TG_STATE_MAX: the largest readout, the whole patch. */
const READ_MAX = 8192;

export function wasmEngine(capi, { sampleRate = 48000, bpm = 120 } = {}) {
  const { fn } = capi;
  const core = fn.tg_core_create(sampleRate);
  fn.tg_core_set_meter(core, 4, 4);
  /* One block of interleaved stereo, silent: the gate's input does not move
   * the playhead, and a silent buffer stays finite through any gain. */
  const audioBytes = BLOCK * 2 * 4;
  const audio = fn.tg_web_alloc(audioBytes);
  const transport = fn.tg_web_alloc(TRANSPORT_BYTES);
  new Uint8Array(fn.memory.buffer, audio, audioBytes).fill(0);
  let running = true;
  let beats = 0;
  let owed = 0;

  const writeTransport = () => {
    /* A view made now: memory may have grown since the last one. */
    const v = new DataView(fn.memory.buffer);
    v.setInt32(transport, running ? 1 : 0, true);
    v.setFloat64(transport + 8, beats, true);
    v.setFloat32(transport + 16, bpm, true);
  };

  return {
    set(key, value) {
      capi.withString(key, (k) => capi.withString(String(value), (v) => fn.tg_core_set_param(core, k, v)));
    },
    get(key) {
      let len = -1;
      const bytes = capi.withString(key, (k) =>
        capi.withBuffer(READ_MAX, (buf, n) => (len = fn.tg_core_get_param(core, k, buf, n))));
      if (len < 0) return null;
      return bytes ? capi.text(bytes) : '';
    },
    advance(ms) {
      owed += (ms * sampleRate) / 1000;
      while (owed >= 1) {
        const frames = Math.min(BLOCK, Math.floor(owed));
        writeTransport();
        fn.tg_core_process_f32(core, audio, frames, transport);
        if (running) beats += (frames / sampleRate) * (bpm / 60);
        owed -= frames;
      }
    },
    play() { running = true; },
    stop() { running = false; },
    dispose() {
      fn.tg_web_free(audio, audioBytes);
      fn.tg_web_free(transport, TRANSPORT_BYTES);
      fn.tg_core_destroy(core);
    },
  };
}

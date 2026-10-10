---
title: NI Vocalize
tagline: Any sound, more and more a voice — singing A, E, I, O or U.
order: 7
hosts: [live, move]
formats: [VST3]
engine: engines/vocalize
crates: [vz-core, vz-capi, vz-move]
tests: [vz_core_rs, vz_core_time_only, vz_move_rs, vz_core, vz_render_ab, vz_processor, vz_rt, vz_host]
---

# NI Vocalize

NI Vocalize turns any sound — a pad, a bass, a guitar, a drum loop — step by
step into a voice singing a vowel. One knob, **Vocal**, says how far: at 0 %
the sound is untouched, at 100 % it sings **A**, **E**, **I**, **O** or **U**,
and every setting between is the sound on its way there.

It is not a filter laid over the sound. A voice is a buzz shaped by a throat
and a mouth, and the shape is what makes an A an A. Vocalize takes your sound
as the buzz: it trades the sound's own colour for the vowel's, and keeps
everything else — its pitch, its rhythm, its noise. A VST3 for macOS (Linux
and Windows follow) with a native editor, plus a Schwung module for the
Ableton Move.

## What it does

| control | what it does |
|---|---|
| **Vocal** | how far the sound has become the vowel: 0 % untouched, 100 % all vowel |
| **Vowel** | A, E, I, O, U — and every place between, so a turn sweeps from one vowel to the next |
| **Size** | the voice: up is a bigger, lower voice, down a smaller, higher one |
| **Resonance** | the vowel's peaks broad and breathy, or sharp and whistling |
| **Excite** | adds harmonics, for sounds too pure to have anything to shape — a sine, a sub |
| **Capture…**, **Voice** | learn your own voice from a recording of you singing A E I O U — drag it onto the window — and morph from the textbook voice to yours |
| **Mode** | **Time**: no latency, resonant and punchy, like a talkbox. **Spectral**: smoother and more sung, one frame (about 21 ms) late |
| **Mix**, **Output** | dry against vowel, and the level after |

Every control glides, so automating any of them does not click — Vocal and
Vowel are made to be automated. Switching Mode dips the sound for 20 ms and
tells Live the new latency.

## The engine

`engines/vocalize` is Rust, shared by both shells:

| crate | what it is |
|---|---|
| `vz-core` | the DSP: the vowel model (five formants, Klatt resonators), linear prediction, the Spectral engine (`realfft`), Excite, the voice capture (linear prediction's roots, `num-complex`), the parameter table and its text |
| `vz-capi` | the C ABI the plugin links (`vz_core.h`, `vz_shell.h`, written by cbindgen) |
| `vz-move` | the Schwung `audio_fx` vtable, built without the Spectral engine |

Both engines put on the same vowel: the Spectral engine multiplies by the
magnitude of the very filter the Time engine runs, and a test holds the two to
each other.

## Tested

| test | what it holds |
|---|---|
| `vz_core_rs` | the engine's own claims: a synthetic singer's five vowels and size measured back from a recording (F2 within 6 %), a captured voice heard as far as Voice says and gliding in without a click, the identity at Vocal 0 (bit for bit in Time, within −120 dB in Spectral), each vowel's F1 and F2 on white noise within 5 % in both modes and at every Size, the envelope moving closer to the vowel at every step of Vocal, the level kept, the mode switch's dip and latency, the panic, Excite giving a sine a vowel, no allocation on any audio path |
| `vz_core_time_only`, `vz_move_rs` | the Move's build: the core without its Spectral engine, and the module's vtable and `chain_params` against the engine's table |
| `vz_core` | the generated C headers, from C |
| `vz_render_ab` | the plugin's float path and the Move's int16 path render a patch per engine within a sixteen-bit step, and each render's hash is pinned |
| `vz_processor`, `vz_rt`, `vz_host` | the processor on the real engine, its audio callback under an allocation guard, and the built VST3 as a DAW hosts it — the latency it reports, a voice captured from a recording, sung, saved with the set and restored (and a broken one refused), and the engine's golden renders bit for bit |

Licence: GPL-3.0-or-later, © 2026 Torben Gräber.

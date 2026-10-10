# NI Vocalize, on the Move

The Schwung build of the same Rust engine the Live plugin uses. AGENTS.md asks
for every DSP on both hosts, and `vz_render_ab` is what proves the two agree:
the plugin's float path and this int16 path render the same patch within a
sixteen-bit step, and the render's hash is pinned.

## What differs from the plugin, and why

**Seven knobs, one page.** Vocal, Vowel, Size, Resonance, Excite, Mix and
Output. The keys are the plugin's, so a value means the same on both.

**The Time engine only.** `vz-move` depends on `vz-core` without its
`spectral` feature: the module has no latency, which on a device played live
is the engine that fits, and it carries no FFT. Mode is not declared; a Mode
the engine is sent is read and changes nothing (`Mode::from_value`).

**Vowel is a float, 0 to 4.** A Schwung float knob turns linearly, which is
what the vowel path wants: a turn sweeps A, E, I, O, U and the way between.

**The panic is MIDI.** The v2 vtable has no reset hook, so a host panic
arrives as CC 120 (All Sound Off) or CC 123 (All Notes Off) through
`move_audio_fx_on_midi` -- which is why `midi_in` is on. Either resets the
engine: the history, the analysis and the resonators let go, so the next
block starts from silence.

## Tested alone

`MODULE_TEST_CRATES` names `vz-move` and nothing beside it: Cargo unites the
features of every crate one invocation builds, so `vz-core` named with it
would be built -- and tested -- with the plugin's Spectral engine. The core's
own tests run on the Mac in both of its builds (`vz_core_rs`,
`vz_core_time_only`).

## No `ui_chain.js`

The module serves `ui_hierarchy` as empty and supplies no custom page, so the
host draws its own knob grid from `chain_params`.

## Building

    ./modules/_shared/package.sh vocalize  # from anywhere, via Docker
    cmake --build build --target schwung-vocalize

    ./modules/_shared/test.sh vocalize     # build, then test on aarch64 in Docker
    ./modules/_shared/install.sh vocalize  # scp to move.local

The build, packaging and install scripts are shared by every module (see
`modules/_shared/`); what is particular to this one is in `module.env`.

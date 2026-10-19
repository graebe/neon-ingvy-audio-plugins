# NI High Pass, on the Move

The Schwung build of the same Rust engine the Live plugin uses
(engines/single-filter), as every single filter's module is: the shared
module crate `sf-move` with this product's knob grid, exported by
`sf-move-high-pass` as one `.so`. `sf_render_ab` proves the plugin's float path
and this int16 path render the same patch within a sixteen-bit step and a
half.

## The knobs

| page | knobs |
|---|---|
| 1 | Freq, Q, Slope, Family, Ripple, Stopband, Channel, Output |
| 2 | Matched |

The keys are the plugin's (`freq`, `q`, `order` ...), so a value means the same
on both. Everything the plugin has but its Analyzer, which is a view and the
Move has no screen for. A Matched switch, where there is one, sits on a second
page so Output stays on the first.

**Freq and Q are named steps.** A Schwung float turns linearly, which would
spend a knob's travel above a few kHz. Freq steps in third-octaves from 20 Hz
to 20 kHz; Q in twelve steps (`sf-move/src/steps.rs`). The knob sends its
label ("2.5k", "0.7071"), which the engine reads, and the module reads it back
in the same words.

**The panic is MIDI.** The v2 vtable has no reset hook, so a host panic
arrives as CC 120 (All Sound Off) or CC 123 (All Notes Off) through
`move_audio_fx_on_midi` -- which is why `midi_in` is on. Either resets the
engine: the filter empties, so the next block starts from silence.

The manual's Move page is [plugins/high-pass/docs/schwung.md](../../plugins/high-pass/docs/schwung.md).

## Building and installing

    cmake --build build --target schwung-high-pass   # the tarball, via Docker
    modules/_shared/install.sh high-pass             # onto the Move

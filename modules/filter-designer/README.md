# NI Filter Designer, on the Move

The Schwung build of the same Rust engine the Live plugin uses. AGENTS.md asks
for every DSP on both hosts, and `fd_render_ab` is what proves the two agree:
the plugin's float path and this int16 path render the same patch within a
sixteen-bit step and a half, and the render's hash is pinned.

## What differs from the plugin, and why

**The isolator and two cuts, on two pages of eight knobs.** The plugin's
sixteen filters are designed on a curve, and the Move has no curve to design
on. So the module is what a DJ reaches for on a device played live:

| page | knobs |
|---|---|
| 1 | Low, Mid, High, Lower, Upper, Slope, Isolator, Output |
| 2 | LC On, LC Freq, LC Order, LC Res, HC On, HC Freq, HC Order, HC Res |

The keys are the plugin's (`iso_low`, `n01_freq` ...), so a value means the
same on both. Node 1 is a Low Cut and node 2 a High Cut, both Butterworth
and off until switched on; the isolator starts on. A band turned to its
bottom (-60 dB) is gone: the Move's kill.

**The frequencies are named steps.** A Schwung float turns linearly, which
would spend a knob's travel above a few kHz. Each frequency knob offers twelve
steps and sends its label ("250", "2.5k"), which the engine reads in Hz; the
module reads it back in the same words.

**The panic is MIDI.** The v2 vtable has no reset hook, so a host panic
arrives as CC 120 (All Sound Off) or CC 123 (All Notes Off) through
`move_audio_fx_on_midi` -- which is why `midi_in` is on. Either resets the
engine: every filter empties, so the next block starts from silence.

## Building and installing

    cmake --build build --target schwung-filter-designer   # the tarball, via Docker
    modules/_shared/install.sh filter-designer             # onto the Move

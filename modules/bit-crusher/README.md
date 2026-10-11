# NI Bit-Crusher, on the Move

The Schwung build of the same Rust engine the Live plugin uses. AGENTS.md asks
for every DSP on both hosts, and `bc_render_ab` is what proves the two agree:
the plugin's float path and this int16 path render the same patch within half
a sixteen-bit step, and the render's hash is pinned.

## What differs from the plugin, and why

**Eight knobs, one page.** Bits, Rate, Drive, Shape, Jitter, Dither, Mix and
Output. The four switches -- Mid-Rise, Anti-Alias and the two filters -- are
not offered and stay off, which is the classic crusher: hard steps, hard
holds. The keys are the plugin's, so a value means the same on both.

**Rate is a list of twelve rates, 250 Hz to 48 kHz.** The plugin's Rate turns
in equal ratios over 50 Hz to 96 kHz; a Schwung float turns linearly, which
would spend most of a knob's travel above 10 kHz. So `chain_params` declares
Rate as an enum that sends its label (`wire_format: "name"`): the host writes
"12k", the engine reads 12000 Hz, and the module reads the value back in the
same words. 48k is the Move's own rate: no hold at all.

**Sixteen bits are the ceiling.** The Move's audio is int16, so Bits above 16
could change nothing; the range is the plugin's, 1 to 16.

**The panic is MIDI.** The v2 vtable has no reset hook, so a host panic
arrives as CC 120 (All Sound Off) or CC 123 (All Notes Off) through
`move_audio_fx_on_midi` -- which is why `midi_in` is on. Either resets the
engine: the held sample lets go and the filters empty, so the next block
starts from silence.

## No `ui_chain.js`

The module serves `ui_hierarchy` as empty and supplies no custom page, so the
host draws its own knob grid from `chain_params`.

## Building

    ./modules/_shared/package.sh bit-crusher  # from anywhere, via Docker
    cmake --build build --target schwung-bit-crusher

    ./modules/_shared/test.sh bit-crusher     # build, then test on aarch64 in Docker
    ./modules/_shared/install.sh bit-crusher  # scp to move.local

The build, packaging and install scripts are shared by every module (see
`modules/_shared/`); what is particular to this one is in `module.env`.

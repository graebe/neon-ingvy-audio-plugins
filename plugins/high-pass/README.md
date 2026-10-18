---
title: NI High Pass
tagline: One high-pass filter — six approximations, 6 to 48 dB/oct, resonance, and matched to the analog curve up to Nyquist.
order: 10
hosts: [live, move]
formats: [VST3]
engine: engines/single-filter
crates: [sf-core, sf-capi, sf-move, sf-move-high-pass]
tests: [sf_core_rs, sf_move_rs, sf_core, sf_render_ab, sf_processor, sf_rt, sf_host_high-pass]
---

# NI High Pass

A **high-pass** (low-cut) filter lets everything above its cutoff pass and
takes away what is below: rumble, handling noise, the low end a part does not
need in a mix. It is one NI Filter Designer filter as a plugin of its own, for
when one cut is all you want in the chain.

It is the same filter, sample for sample, as a high pass in
[NI Filter Designer](../filter-designer/README.md): the same design, the
same glides and crossfades, the same words for every value. A VST3 for macOS
with a native editor, and a Schwung module for the Ableton Move.

- [In Live](docs/live.md): the window, every control, and worked examples.
- [On the Move](docs/schwung.md): the knobs.
- [Filter reference](../filter-designer/docs/filters.md): the formulas behind
  every filter type and approximation.

## How it is built

NI High Pass is one of nine single filters (NI High Pass, NI Low Pass, NI Band
Pass, NI Notch, NI Bell, NI Low Shelf, NI High Shelf, NI Tilt, NI All Pass)
built on one Rust engine, `engines/single-filter`. Its filter is NI Filter
Designer's own node (`engines/shared/crates/ni-filter-node`): a cascade of
TPT state-variable sections designed in `ni-filter`, whose numbers glide,
whose design morphs when its family changes and crossfades when its slope or
channel does, so nothing clicks. The plugin is the shared JUCE shell in
`plugins/_shared/single-filter` with this product's shape in it; the Move
module is `sf-move` with this product's knobs.

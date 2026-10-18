---
title: NI Band Pass
tagline: One band-pass filter — five approximations, Q as the bandwidth, steep skirts on both sides.
order: 12
hosts: [live, move]
formats: [VST3]
engine: engines/single-filter
crates: [sf-core, sf-capi, sf-move, sf-move-band-pass]
tests: [sf_core_rs, sf_move_rs, sf_core, sf_render_ab, sf_processor, sf_rt, sf_host_band-pass]
---

# NI Band Pass

A **band-pass** filter keeps a band around its centre frequency and takes away
everything above and below it: the telephone voice, the radio in the next
room, a single drum isolated from the kit. One NI Filter Designer filter as a
plugin of its own.

It is the same filter, sample for sample, as a band pass in
[NI Filter Designer](../filter-designer/README.md): the same design, the
same glides and crossfades, the same words for every value. A VST3 for macOS
with a native editor, and a Schwung module for the Ableton Move.

- [In Live](docs/live.md): the window, every control, and worked examples.
- [On the Move](docs/schwung.md): the knobs.
- [Filter reference](../filter-designer/docs/filters.md): the formulas behind
  every filter type and approximation.

## How it is built

NI Band Pass is one of nine single filters (NI High Pass, NI Low Pass, NI Band
Pass, NI Notch, NI Bell, NI Low Shelf, NI High Shelf, NI Tilt, NI All Pass)
built on one Rust engine, `engines/single-filter`. Its filter is NI Filter
Designer's own node (`engines/shared/crates/ni-filter-node`): a cascade of
TPT state-variable sections designed in `ni-filter`, whose numbers glide,
whose design morphs when its family changes and crossfades when its slope or
channel does, so nothing clicks. The plugin is the shared JUCE shell in
`plugins/_shared/single-filter` with this product's shape in it; the Move
module is `sf-move` with this product's knobs.

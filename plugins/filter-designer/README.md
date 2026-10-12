---
title: NI Filter Designer
tagline: A filter design tool you play as an equaliser — sixteen filters, six families, one curve.
order: 8
hosts: [live, move]
formats: [VST3]
engine: engines/filter-designer
crates: [fd-core, fd-capi, fd-move]
tests: [fd_core_rs, fd_move_rs, fd_core, fd_render_ab, fd_processor, fd_rt, fd_host]
---

# NI Filter Designer

NI Filter Designer puts up to sixteen filters on one response curve. Each is
a bell, a shelf, a cut, a band pass, a notch, an all-pass or a tilt. The
cuts, bands and notches come in the classic families: Butterworth, Bessel,
Chebyshev I and II, Elliptic and Linkwitz-Riley, at 6 to 48 dB an octave,
with resonance, ripple and stopband. In front of them sits an isolator in the
manner of a DJ mixer: three bands, two crossovers, a kill on each.

So it does what an EQ Eight and an EQ Three do, in one window, and what
neither does: choose how a cut turns the corner. It is minimum phase, so it
adds no latency. A VST3 for macOS (Linux and Windows follow) with a native
editor, plus a Schwung module for the Ableton Move.

- [In Live](docs/live.md): the window, what each family is for, and recipes.
- [On the Move](docs/schwung.md): the isolator and two cuts, on two pages.

## How it is built

The engine is Rust (`engines/filter-designer`) on the shared filter crate
`ni-filter` (`engines/shared/crates/ni-filter`): every filter is a cascade
of TPT state-variable sections, designed from analog prototypes with one
prewarp per filter; the bells and shelves are matched to their analog shape
up to Nyquist. The editor draws the engine's own designs of the values the
host holds, so the curve is the filter that plays. The decisions are in
[ADR 0004](../../docs/adr/0004-iir-filter-design.md), the design in
[the concept](../../docs/concepts/filter-designer.md).

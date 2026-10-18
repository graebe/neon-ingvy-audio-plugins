---
title: NI Filter Designer
tagline: Sixteen minimum-phase IIR filters on one response curve — six approximations, orders 1 to 8, and a three-band isolator.
order: 8
hosts: [live, move]
formats: [VST3]
engine: engines/filter-designer
crates: [fd-core, fd-capi, fd-move]
tests: [fd_core_rs, fd_move_rs, fd_core, fd_render_ab, fd_processor, fd_rt, fd_host]
---

# NI Filter Designer

NI Filter Designer places up to sixteen filters on one magnitude response:
bell, low shelf, high shelf, high-pass, low-pass, band-pass, notch
(band-stop), all-pass and tilt. High-pass, low-pass, band-pass and notch are
designed from the classic analog approximations — Butterworth, Bessel,
Chebyshev I and II, elliptic and Linkwitz-Riley — at orders 1 to 8 (6 to
48 dB/oct per order), with Q, passband ripple and stopband attenuation. In
front of them sits a three-band isolator with Linkwitz-Riley crossovers and a
kill per band.

It covers what EQ Eight and EQ Three do, adds the choice of approximation and
order, and draws the engine's own transfer function over a pre/post spectrum
analyzer. All filters are minimum phase; the plugin adds no latency. A VST3
for macOS (Linux and Windows follow) with a native editor, plus a Schwung
module for the Ableton Move.

- [In Live](docs/live.md): the window, the filter types and approximations,
  and worked examples.
- [On the Move](docs/schwung.md): the isolator, a high-pass and a low-pass,
  on two pages.
- [Filter reference](docs/filters.md): every filter type and approximation,
  its parameters, its analog and digital formulas, and what they mean.

## How it is built

The engine is Rust (`engines/filter-designer`) on the shared filter crate
`ni-filter` (`engines/shared/crates/ni-filter`). Every filter is a cascade of
TPT (zero-delay feedback) state-variable sections designed from an analog
prototype with a single bilinear prewarp per filter; the bell, the shelves
and the tilt are magnitude-matched to their analog prototypes up to Nyquist.
The editor draws the engine's designs of the values the host holds, so the
curve is the transfer function that plays. The decisions are in
[ADR 0005](../../docs/adr/0005-iir-filter-design.md), the design in
[the concept](../../docs/concepts/filter-designer.md).

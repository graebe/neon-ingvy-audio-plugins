---
title: Trance Gate
tagline: A tempo-locked step gate, in Live and on the Move, from one Rust engine.
order: 1
hosts: [live, move]
formats: [VST3, AU, CLAP]
engine: engines/trance-gate
crates: [tg-core, tg-capi, tg-move]
tests: [tg_core, tg_render_ab, tg_curves, tg_envelope, tg_au]
still: media/trance-gate/live.png
harness: harness/trance-gate/
---

# Trance Gate

A tempo-locked step gate: rhythmic chopping locked to song position, per-step
ADSR, ties, per-step amount, 8 pattern slots. Universal macOS binary as
VST3 / AU / CLAP, and a Schwung module for the Ableton Move.

**It is the same engine in both**, and that is asserted rather than claimed.
`tg-capi` wraps the core in a C ABI for the plugin and `tg-move` wraps it in
Schwung's `audio_fx` vtable for the Move; both are members of one Cargo
workspace and both reach `tg-core` by relative path, so there is nothing to
keep in sync. `tests/render_plugin.c` then renders four seconds through the
plugin's own audio path — float, split channels, a DAW-shaped transport — and
the result is byte-for-byte identical to the module's reference render.

## Patch interchange

The plugin's saved state **is** the Move patch, verbatim. `Copy patch` puts it
on the clipboard; `Paste patch` reads one back. The same string moves a pattern
between the hardware and the DAW in either direction.

## What the host can automate, and what it cannot

The twelve continuous controls — Slot, Length, Rate, Amount, Width, the four
envelope stages, Join Neighbors, Env Time and Env Curve — are ordinary host
parameters. Automate them, and they behave the way a DAW expects.

**The pattern is not among them**, and that is a decision rather than an
omission. Which steps sound, which are tied, and how loud each one is would be
32 × 8 = 256 more parameters, or 32 that get silently rewritten every time the
Slot changes — and a parameter that changes value without the host asking is
worse than no parameter at all. So the pattern travels in the saved state, which
is the same model the Move module uses and the reason a patch can cross between
them at all.

## Timing

The gate follows song position, so it stays bar-aligned however long it runs
and survives a seek. Rate is one step's length: at 1/16 a 16-step pattern is one
bar.

Times are in milliseconds and are **not** synced. A 1/16 step is `15000 / BPM`
ms — 125 ms at 120 BPM — so keep Attack plus Decay under that, or a step never
reaches full.

With the transport stopped the gate holds open and audio passes.

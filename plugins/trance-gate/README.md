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
ADSR, ties, per-step amount, a fade-in that introduces the steps one at a time,
and 8 pattern slots. Universal macOS binary as
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

## The fade-in

**Every step that sounds carries an arrival number**, and Fade introduces them in
that order: at 0% none of them sound, at 100% all of them do, and the arrivals in
between are evenly spaced — the step ranked *r* of *n* arrives at exactly *r/n*.

**Soft** ramps a step in on its own level, the same quantity a vertical drag in a
pad sets. **Hard** jumps it on. They are one formula and a threshold:

```
w(r) = clamp(f·n − (r−1), 0, 1)     soft
w(r) = w_soft(r) ≥ 1 ? 1 : 0        hard
```

so the two agree at every arrival boundary — the switch changes a step's shape,
never *when* it arrives — and in soft mode exactly one step is ever part way in.

A step the fade has not reached is a **gap**, not a silent step that is on. The
difference is audible: ties and Join Neighbors ask the pattern whether a step
sounds in order to decide whether to hold a gate open through it, so a step that
has not arrived must not keep its neighbour's gate open.

**The order is the point.** In position order a fade can only be a left-to-right
wipe; shuffled, it is a build-up. `Random` shuffles it along with the pattern, and
`ORDER` in the Fade In panel lets you tap the steps into the sequence you want.

## Random

`Random` fills the current slot with a Euclidean gate — a random number of hits
spread as evenly as the length allows, always with one on the downbeat — and a
shuffled arrival order. Sixteen coin flips reads as noise rather than as a trance
gate, which is why it is not that. Ties are cleared and the levels return to
full: they describe a pattern that no longer exists.

It does not touch the playhead, so it is safe to press mid-bar.

## What the host can automate, and what it cannot

The fourteen continuous controls — Slot, Length, Rate, Amount, Width, the four
envelope stages, Join Neighbors, Env Time, Env Curve, and the fade's knob and its
shape — are ordinary host parameters. Automate them, and they behave the way a
DAW expects. **Fade is the one this matters most for**: a build-up is that knob
drawn across eight bars.

**The pattern is not among them**, and that is a decision rather than an
omission. Which steps sound, which are tied, how loud each one is and when it
arrives would be 32 × 8 = 256 more parameters, or 32 that get silently rewritten
every time the Slot changes — and a parameter that changes value without the host
asking is worse than no parameter at all. So the pattern travels in the saved state, which
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

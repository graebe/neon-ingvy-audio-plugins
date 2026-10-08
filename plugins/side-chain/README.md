---
title: NI Side-Chain
tagline: A ducker you can see — the shape you drew, drawn over the audio it shaped.
order: 3
hosts: [live, move]
formats: [VST3]
engine: engines/side-chain
crates: [sc-core, sc-capi, sc-move]
tests: [sc_core, sc_core_rs, sc_shape, sc_envelope, sc_render_ab, sc_processor, sc_host]
---

# NI Side-Chain

Sidechain ducking without the compressor. A trigger arrives, the signal is
pushed down along a shape you drew, and it comes back. A VST3 for macOS (Linux and Windows follow)
with a native editor, plus a Schwung module for the Ableton Move.

## Three ways to say "now"

The three trigger sources disagree about **when** and about nothing else — one
envelope answers all of them, so switching source changes the timing and not the
character.

**Cycle** is tempo-locked and needs no routing at all: pick a rate and the duck
fires on every division. The host's beat position is an *anchor* that a local
accumulator is pulled towards rather than a clock to divide, which is what
survives a loop jump, a seek and a tempo ramp.

**MIDI** is a note, with a channel, Trigger-or-Gate and velocity-to-depth.
Notes duck **only when Source is MIDI**: on Cycle or Sidechain a kick pad
played on the same track no longer fires an extra, unsynchronised duck of its
own. The note lands on **its own sample**, not at the top of the buffer it arrived in —
the host reports the offset and this honours it, because up to 5 ms of *jitter* on
the one event the effect is about cannot be compensated anywhere downstream.
**All Notes Off and All Sound Off** (CC 123 and CC 120) open the duck at once,
whatever note it listens to, on its channel: Live's panic, and the Move's.

> **In Live, MIDI mode needs a MIDI track.** Live does not route MIDI to a
> plugin sitting on an audio track, so MIDI mode fires when Side-Chain sits
> after an instrument. Cycle is the default source for that reason, and the
> editor says which source is actually firing rather than leaving a silent
> trigger to look like zero depth.

**Sidechain** is a real aux input — the first in this repository — with a
threshold and a retrigger lockout. Two time constants smooth the key into one
hump per hit; the lockout decides how close two triggers may be. That split is
deliberate: a longer detector fall would *merge* hits and quietly take the
decision away from the control that is supposed to own it.

## Smooth when you move it

Depth glides to a new value over about 5 ms while a duck is sounding, so
riding it during playback does not click. Change a stage length — or the tempo
— in the middle of a duck and the duck carries on from where it is, at the new
speed, instead of jumping. And stopping the transport on the Cycle source lets
the current duck release over your Release time rather than cutting back to
full level.

## The picture

One well, **one axis**, and that is the whole editor:

- the **shape** you dragged, as a line with four handles — drawn by the engine
  itself from the parameters, never by a second copy of its maths
- the **signal** behind it: the input dimmed, the output in front, and the gain
  the plugin actually applied as a thin line

They line up because the capture is phase-locked to the trigger rather than
rolling on wall time — so a dip in the waveform sits under the curve that made
it. The cost is stated rather than hidden: a column is rewritten once per cycle,
which is twice a second at 1/4 and 120 bpm and genuinely four seconds stale at
1/1 and 60. The alternative is a picture that looks fresher and lines up with
nothing.

The two curves agree until something interrupts a duck, and then they do not: a
retrigger part way through a recovery anchors on the level the envelope actually
reached. No drawing can predict that, which is why both are there.

## Everything is a parameter

There is no patch blob. All fifteen values are host parameters, so the shape
editor's **handles are parameters too** — dragging one is an ordinary edit that
lands in Live's undo history and its automation lane, and the whole state travels
where the host can see it.

The four stage lengths are percentages of the **cycle**, always; `Time` chooses
which unit you read them in. A shape proportional to the cycle keeps its
proportions when the tempo or the rate changes, which is what "in time with the
music" means — and a parameter whose *meaning* depended on another parameter
would be one whose automation lane changes what it does when something else
moves.

## One engine, two hosts

`sc-capi` wraps the core in a C ABI for the plugin; `sc-move` wraps it in
Schwung's `audio_fx` vtable for the Move. Both are members of one Cargo
workspace and both reach `sc-core` by relative path, so there is nothing to keep
in sync.

The Move build has no Sidechain source — the chain host hands over one buffer
and the API has no aux input anywhere — so that option is *absent* rather than
present and inert, along with the Threshold and Lockout that belong to it.

## How the curves are checked

The native editor draws the engine's own single shot (`sc_shape_render`), so
there is no second copy of the curve to drift. The engine's curves are pinned
to one table **generated by the engine** rather than to a second
transcription — two identically wrong transcriptions agree with each other
perfectly.

The envelope fixture goes further and is **measured**: a DC input at depth 1, so
the output sample *is* the gain. Nothing in producing it consults a model of the
envelope, which is what makes it able to disagree with one.

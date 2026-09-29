---
section: live
title: Ableton Live Interface
---

The window is two pictures stacked on one axis, and the axis is **one cycle**.
Above it, the shape you asked for. Below it, the audio you got. They line up on
purpose: a dip in the waveform sits directly under the curve that made it.

## The shape well

The bright line is what the four stage controls describe — an idealised single
duck, from open, down, held, and back.

The filled region behind it is **what actually happened**: the gain the plugin
applied, measured and captured. Most of the time the two agree and the line sits
on the edge of the fill. When they part, something interrupted a duck — a
trigger arriving part way through a recovery anchors on the level the envelope
had actually reached rather than restarting from the top. That is the whole
reason both are drawn.

Four handles, and each one is a parameter:

| handle | drag | changes |
|---|---|---|
| the first, on the ceiling | sideways | **Delay** — when it begins |
| the corner at the bottom | sideways | **Attack** — how fast it gets there |
| | up and down | **Depth** — how far down |
| the second corner | sideways | **Hold** |
| the last, back on the ceiling | sideways | **Release** |

Shift-drag for fine, double-click to reset. Because they are parameters,
dragging one is an ordinary edit: it lands in Live's undo history and it writes
to the automation lane, exactly as turning the knob below it would.

An **amber line at the right edge** means the shape is longer than one cycle and
cannot finish before the next trigger. That is allowed — the overlap is a real
sound — but it is worth knowing you are hearing it.

## The signal well

Input in grey, output in front. Both are drawn as the minimum and maximum of
every column rather than an average, because a transient is a fraction of a
column and averaging would show you a signal nobody is playing.

The columns are locked to the trigger rather than scrolling, which is what keeps
them under the shape. One consequence is worth stating: a column is rewritten
once per cycle. At 1/4 and 120 bpm that is twice a second and reads as live; at
1/1 and 60 bpm the picture really is four seconds old.

A part-drawn picture is a picture still filling, not a signal that stopped — the
trace simply ends where the sweep has got to.

## Choosing a trigger

**Cycle** needs nothing set up. Pick a rate and it fires on every division,
locked to the transport. It survives loop jumps and tempo ramps because the
host's position is treated as something to follow rather than something to
divide.

**MIDI** takes a note, a channel, and Trigger or Gate. In Trigger mode the Hold
times out on its own; in Gate mode the duck stays down until the note is
released. Velocity can scale the depth.

> **Live does not route MIDI to a plugin on an audio track.** MIDI mode fires
> when Side-Chain sits on a MIDI track after an instrument. If you are on an
> audio track, use Cycle or Sidechain. The header says `no midi` when nothing is
> arriving, so this is visible rather than mysterious.

**Sidechain** listens to a real key input — pick the source in the device
header, the way you would for Live's own Compressor. **Threshold** sets how loud
the key has to be; **Lockout** sets how close two triggers may be, which is what
stops a snare 30 ms behind the kick firing a second duck.

The header shows `no key routed` until something is patched, and `key is the
input` if the host has handed over the track's own audio — which Logic and
GarageBand do when the slot is empty.

## Times are percentages of the cycle

All four stages are proportional to the cycle, always. Change the tempo or the
rate and the shape keeps its proportions, which is what staying in time means.

The `Time` control chooses whether you read them as percentages or as
milliseconds; it changes the reading, not the sound. The hint bar shows the
total in milliseconds as you work.

## What the header tells you

Source, rate, and which stage the envelope is in right now. On the right, at
most one amber mark — `transport stopped`, `no midi`, `no key routed`. A ducker
whose trigger is not arriving looks exactly like one set to zero depth, and
there are three separate ways for that to happen, so it says which.

## The background, and the Motion switch

The window's background is not a static image: a kick drum makes it ring. Each
hit sends one slow wave out from every panel edge and from the window border; the
waves reflect, cross and fade out over about twenty seconds, and the dots and
grain swell and thin as they pass. With no bass playing it is completely still.

The **Motion** switch in the hint bar turns it off. That setting is remembered on
your machine and is not a plugin parameter — it will not be automated, saved into
a preset, or changed on somebody else who opens your project. If your system is
set to reduce motion, it is off regardless.

Only 20–80 Hz counts as a kick, so a snare or a hi-hat will not move it however
loud it is. There is more detail, including why two very close kicks read as one,
under **The Animated Ground**, on the Tech pages.

<!-- NOT A LINK. This file is rendered in two places -- on the docs site, which
     is served under a base path, and on GitHub, which is not -- so a path that
     works in one is broken in the other, and `ctest -R site_links` catches the
     site half. Naming the page works in both. -->

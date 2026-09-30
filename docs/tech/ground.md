---
title: The Animated Ground
order: 5
slug: ground
---

Every window in this repository has the same background: violet dot paper over a
fine grain. Since `v2026.09.29`, that background **moves when a kick lands**, and
is perfectly still the rest of the time.

This page is about how it works and how to turn it off. If you just want the
switch, it is called **Motion** and it sits in the hint bar at the bottom of
every window.

## What you see

Play a kick through any of the four plugins and watch the background rather than
the controls. Each hit makes every panel edge — and the window border itself —
emit one slow ring. The rings travel outward through the background, bounce off
the panels and off the border, cross each other, and fade out over about twenty
seconds. Where a ring peaks the dots grow a little and brighten and the grain
thickens; in the trough between two rings they shrink and dim and it thins.

Three things it deliberately does **not** do:

- **It never idles.** With no kick, the field is exactly zero, the picture is
  identical to the static background, and the render loop stops entirely. A
  background that drifts on its own would be competing with the meters.
- **The grain never travels.** Only its local density changes. Moving grain reads
  as television static.
- **Rings do not cross a panel.** Panels, wells and the step grid are solid to
  the field, which is what keeps the motion feeling like it is *behind* the
  interface rather than on top of it.

## What counts as a kick

Only low bass. The detector listens to the band from **20 Hz to 80 Hz** and looks
for an **attack** in it — a sudden rise above whatever the bass has been doing for
the last quarter second or so. A kick drum moves it; a snare, a hi-hat, a vocal or
a loud guitar do not, however loud they are, and neither does a sustained bass
note once it is holding.

Because the question is *relative*, there is no sensitivity to set. The same
plugin behaves the same way on a quiet dub mix and on a loud master — it is
comparing the music to itself.

> **A deviation from the design system, deliberately.** The Ultraviolet spec asks
> for a different test: the band's level exceeding 1.8× its own 300 ms average.
> That works beautifully on an isolated kick, which is what the design's preview
> demonstrates — and it very nearly never fires on a record. Over a loud sustained
> low end, a kick adds only about a third to the level of the 20–80 Hz band,
> because the bass is already filling that band; a third is 1.3×, and 1.3 is not
> 1.8. Measured on an eight-second loop with sixteen kicks in it, the specified
> rule produced **one** ring on a limited mix and **two** on an 808 pattern.
> Keying on the attack instead gives sixteen on both, with no false positives.
> The band, the envelope times, the refractory and the strength range are all
> still the design's. The table in
> `engines/ground/crates/ground-core/src/tests.rs` (`mod material`) is the real
> specification now, and `detect.rs` records the reasoning in full.

Two details worth knowing, because they are the ones people notice:

- **Two kicks closer together than about a quarter of a second read as one.**
  There is a 120 ms minimum gap between onsets, but the practical floor is longer:
  after a ring the detector waits for the bass to fall back before it will fire
  again. So a sixteenth-note kick roll merges into one ring, while eighth notes at
  120 BPM do not. The rings from one hit last far longer than the gap anyway.
- **A track starting, a clip launching or a channel un-muting can ring once**,
  even with no kick in it. An amplitude ramp has a spectrum of its own, and a fast
  one puts real energy into the bass band whatever pitch is playing. It is a
  genuine transient, so it is treated as one.
- **It hears the plugin's input.** On NI Side-Chain that is the main input, not
  the key signal.

## Turning it off

The **Motion** switch in the hint bar stops it immediately: the field flattens to
zero and the loop stops. The setting is remembered per plugin, on your machine.

It is deliberately **not** a plugin parameter. Whether a background animates is a
property of *your view*, not of the sound — so it is not automatable, it is not
saved into presets, and a project you send to somebody else will not turn their
background off.

If your system is set to reduce motion (macOS: *System Settings → Accessibility →
Display → Reduce motion*), the ground never animates at all and the Motion switch
cannot override that.

## Where it lives in the code

| | |
|---|---|
| `engines/ground` | the 20–80 Hz onset detector, in Rust, on the audio thread |
| `ui-kit/src/lib/field.js` | the wave field — a port of the design system's own reference implementation |
| `ui-kit/src/components/Ground.jsx` | the canvas, its sizing and the panel measurement |
| `ui-kit/src/lib/motion.js` | the Motion switch's remembered state |
| `design/files/project/README.md` | the design system's Motion section, which is the specification |

The detection has to happen in Rust because a plugin editor is a WebView with no
access to the host's audio — there is no `AudioContext` to hand the signal to. The
audio thread counts onsets; the editor reads that count about fifty times a second
and turns each new one into a ring. `engines/ground/include/ground_detect.h`
spells out the whole contract, including why it is a count and not a flag.

The physics is the damped 2D wave equation on a 6 px grid, one Ricker wavelet per
kick, reflecting at every panel edge. The parameters — wave speed, damping,
source strength, dot and grain response — all come from the design system and are
checked against it by `node --test ui-kit/test/field.test.mjs`, which reads the
reference implementation and diffs the numbers. The **detector** is the one part
that deviates, for the reason in the note above.

## What moves, exactly

Both the dots and the grain respond, and neither of them travels.

As a ring passes, the dots grow from 1.0 px to about 1.3 px and brighten by
roughly a fifth, and the grain thickens from 5 % to about 9 %; in the trough
behind it they shrink, dim and thin. A full-strength kick redraws around **30 % of
the window's dots** at its peak and stays visible for eight seconds or more.

The grain **tile itself never moves** — only its local density changes. That is a
design rule rather than an optimisation: grain that travels reads as television
static rather than as a surface responding to sound.

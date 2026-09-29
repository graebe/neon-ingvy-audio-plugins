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

Only low bass. The detector listens to the band from **20 Hz to 80 Hz** and asks
whether there is more energy there right now than there has been over the last
300 ms. So a kick drum moves it; a snare, a hi-hat, a vocal or a loud guitar do
not, however loud they are.

Because the question is *relative*, there is no sensitivity to set. The same
plugin behaves the same way on a quiet dub mix and on a loud master — it is
comparing the music to itself.

Two details worth knowing, because they are the ones people notice:

- **Two kicks closer together than about a quarter of a second read as one.**
  There is a 120 ms minimum gap between onsets, but the practical floor is longer:
  after a ring the detector waits for the bass to drop back down before it will
  fire again. Straight eighth notes above roughly 120 BPM will therefore ring on
  alternate hits. This is how the design's own reference behaves, and the rings
  from one hit last far longer than the gap anyway.
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
reference implementation and diffs the numbers.

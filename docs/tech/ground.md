---
title: The Animated Ground
order: 5
slug: ground
---

Every window in this repository has the same background: violet dot paper over a
fine grain. That background **keeps time with the song**: while the host's
transport plays it rings on every beat, a little harder on the first beat of
each bar, and while the transport is stopped it is perfectly still.

This page is about how it works and how to turn it off. If you just want the
switch, it is called **Motion** and it sits in the hint bar at the bottom of
every window.

## What you see

Press play in Live and watch the background rather than the controls. Every
beat makes each panel edge — and the window border itself — emit one slow ring.
The rings travel outward through the background, bounce off the panels and off
the border, cross each other, and fade out over about twenty seconds. Where a
ring peaks the dots grow a little and brighten and the grain thickens; in the
trough between two rings they shrink and dim and it thins. The ring on a bar's
downbeat is the tall one; the others are visibly smaller, so the bar reads in
the background as one strong pulse and its lighter beats.

Three things it deliberately does **not** do:

- **It never idles.** With the transport stopped the field rings out to exactly
  zero, the picture is the static background, and the render loop stops
  entirely.
- **The grain never travels.** Only its local density changes. Moving grain reads
  as television static.
- **Rings do not cross a panel.** Panels, wells and the step grid are solid to
  the field, which is what keeps the motion feeling like it is *behind* the
  interface rather than on top of it.

## When it rings

The rule is short, and it is the whole of it:

- **one ring on every quarter note**, from the host's song position, while the
  transport plays;
- **a strong ring on each bar's downbeat**, from the host's time signature — a
  bar is *numerator × 4 ÷ denominator* quarter notes, and a host that reports
  no time signature is taken to be in 4/4;
- **nothing while the transport is stopped.**

It reads no audio. Every plugin rings identically — NI Side-Chain on a bass,
NI Spectrogram on a silent return track — because all of them read the same
clock.

How the edges behave, because they are the ones people notice:

- **Bars count from the start of the song.** In 6/8 a bar is three quarters,
  so every third ring is the strong one. In 7/8 a bar is three and a half: the
  rings fall on quarters 1 2 3 4, the next downbeat lands half a beat after the
  fourth and rings there, and the bar after it starts on a quarter again.
- **Pressing play on a beat rings it once**, at once. Pressing play between two
  beats waits for the next one.
- **A loop or a jump never rings the beats it skipped.** Landing on a beat rings
  that beat; landing between two rings nothing until the next. A loop back to
  bar 1 rings bar 1's downbeat, once.
- **A tempo change simply changes the spacing.** Very slow and very fast tempos
  work the same way; a beat cannot be lost or doubled between two audio blocks.
- **It keeps time only while the window is open.** With the window closed the
  plugin skips the clock entirely and it costs the audio thread nothing;
  reopening the window mid-song picks up at the next beat, with no backlog.

The strengths are **1.0** for a downbeat and **0.4** for any other beat. The
field is linear in strength, so a beat's ring is two fifths the height of a
downbeat's. Measured in the field, at its peak a downbeat ring moves about 53 %
of the window's dots two levels or more from rest, and a beat ring about 14 %:
plainly there, plainly the lesser. 0.55 was the first candidate and moved 34 %,
which at 120 BPM — where each ring is still swelling when the next one starts —
read too close to the downbeat. `ui-kit/test/field.test.mjs` holds the field to
this.

> **The design followed the plugins here.** Ultraviolet 1.0.0's Motion spec
> drove the ground *by sound, not by time*: a 20–80 Hz onset detector on each
> plugin's own input. That made four backgrounds that disagreed — each heard
> only its own track, so a plugin on a pad or a silent return never moved — and
> it needed an audio feed per plugin. The plugins moved to the host's tempo by
> the owner's decision, and Ultraviolet 1.1.0 adopted the same rule. Its
> reference strength for a beat is 0.55, "about half", which the system lets a
> plugin tune; 0.4 is that tuning, for the reason above.

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
| `engines/ground` | the beat clock, in Rust, on the audio thread |
| `plugins/_shared/ni/WebPlugin.cpp` | feeds it the host's transport once a block, and sends each ring to the editor |
| `ui-kit/src/lib/field.js` | the wave field — a port of the design system's own reference implementation |
| `ui-kit/src/components/Ground.jsx` | the canvas, its sizing and the panel measurement |
| `ui-kit/src/lib/motion.js` | the Motion switch's remembered state |
| `ui-kit/harness/beat.js` | a playing transport for the editors' review harnesses |
| `design/scheme/project/README.md` | the design system's Motion section |

The beat is found in Rust because a plugin editor is a WebView: it cannot see
the host's transport, and its timers are neither sample-accurate nor running
when the host renders offline. In every audio block `ni::WebPlugin` hands the
clock the host's position, tempo, time signature and play state
(`gnd_tick`); the clock counts the beats and downbeats that block crossed. The
editor's side reads that count about fifty times a second and turns each new
one into a ring. `engines/ground/crates/ground-capi/src/lib.rs`, which `ground.h`
is generated from, spells out the whole contract, including why it is a count and not a flag, and
`engines/ground/crates/ground-core/src/beat.rs` the rule, down to how a block
tells a loop from rounding.

**The plugin is the field's clock — never animation frames, and it does not
pause when the page says it is hidden.** In a real host WebKit treats a plugin
editor as a hidden page for as long as it is open: `document.hidden` is true,
the page gets about one animation frame in three seconds, and its own timers
are throttled to a few hertz — while the plugin's messages arrive at once. A
field clocked by animation frames, or one that paused while hidden, as this one
once did, never moved in Live. So the editor tells the plugin when its field
starts and stops moving (tag 123, `groundRun`), and in between the plugin sends
an empty frame tick on every idle tick, about fifty a second (tag 114,
`groundTick`). Each tick steps the simulation by the time that really passed
and draws once. A page timer steps it only if the ticks stop coming, and in a
host's hidden page that is slow by design. Work stops when the field rings out,
and the plugin stops ringing it, and ticking it, when the window closes.

The physics is the damped 2D wave equation on a 6 px grid, one Ricker wavelet per
ring, reflecting at every panel edge. The parameters — wave speed, damping,
source strength, dot and grain response — all come from the design system and are
checked against it by `node --test ui-kit/test/field.test.mjs`, which reads the
reference implementation and diffs the numbers.

To review it without a host, open any editor's harness
(`plugins/<plugin>/ui/test/harness/index.html`, served over http from the
repository root): it plays a transport at 120 BPM in 4/4. `?bpm=`, `?sig=7/8`
and `?stopped` change that, and **T** starts and stops it.

## What moves, exactly

Both the dots and the grain respond, and neither of them travels.

As a ring passes, the dots grow from 1.0 px to about 1.3 px and brighten by
roughly a fifth, and the grain thickens from 5 % to about 9 %; in the trough
behind it they shrink, dim and thin.

The grain **tile itself never moves** — only its local density changes. That is a
design rule rather than an optimisation: grain that travels reads as television
static rather than as a surface responding to the beat.

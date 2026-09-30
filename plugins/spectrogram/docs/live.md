---
section: live
title: Ableton Live Interface
---

The whole interface is the picture. There are no parameters to automate and
nothing to set up: insert it, and it draws what is passing through.

## Reading it

Frequency is the vertical, logarithmic, 10 Hz at the bottom and 20 kHz at the
top across 256 bands — about two per semitone, so a semitone is roughly two
rows. Time scrolls right to left and the visible window holds thirteen seconds.

Brightness is level: the ground is quiet, a near-white core is loud, and the
violet between them is the falloff.

Time is also ticked along the bottom, a mark a second from `0s` at the right
edge back to `-12s` at the left. The ticks are arithmetic rather than a clock:
one column is one pixel and the engine holds the column rate constant across
sample rates, so the width *is* the span.

## The x-axis can be bars instead of seconds

Throw the **bars** switch and the picture stops scrolling. The x-axis becomes a
window of the host's own timeline — 1, 2, 4, 8 or 16 bars — and columns fill it
left to right. When the sweep reaches the right edge it wraps to the left and
writes over the pass before it, with a bright hairline marking where it is
writing now.

The point is that **position decides the pixel**. A given beat always lands on
the same column, so a loop draws itself on top of itself: a kick that drifts
smears, a hat that is early sits left of the line, and two takes of one bar can
be compared by looking at one picture instead of remembering the last one.

The grid under the picture follows: a numbered tick per bar, and the beats
inside it whenever a bar is wide enough to hold them — at 16 bars a bar is 38
pixels, so the beats are dropped rather than drawn as a haze.

**Switching views loses nothing.** Both pictures are drawn from the same columns
the whole time, whichever one is on screen, so the other is already complete the
moment you ask for it — the same bargain Pause makes, and for the same reason.

**With the transport stopped** — or in a host that reports no beat timeline at
all — the sweep keeps filling at the last tempo it saw, so the picture still
moves while you audition. The hint bar says `free` when it is running on that
rather than on a playhead.

One honest limit: the analyzer finishes about 47 columns a second whatever the
window is, so a window shorter than ~13 seconds has fewer columns than it has
pixels. The gaps are interpolated rather than held flat, which keeps the picture
smooth, but a 1-bar window at a fast tempo genuinely has less time resolution
than a wide one. It is smoothed, not invented back.

## Listening in on other tracks

Put a **Listen-In** on the track you want and give it a name. The Spectrogram
then lists every bus that exists, by that name, in two separate controls — and
they are separate on purpose, because they answer different questions.

### View — what the picture is of

Tick one or more channels. `input` is the track this plugin sits on; the rest
are Listen-In buses.

**Ticking several ADDS them.** Not overlays them in different colours — added,
in power, the way two sources actually sum: two equal ones read **+3 dB**, not
+6 (that would be amplitude addition, which assumes they are phase locked) and
not twice as bright (that would be adding decibels, which multiplies
amplitudes). So the picture stays one picture, brightness still means level, and
what you are looking at is what the two tracks do *together*.

Changing the view clears the history, because thirteen seconds of the previous
mix spliced onto the new one with no seam is not a picture of anything.

### Compare — what the orange is measuring

Two pickers and a switch, independent of the view: comparing two channels you
are not currently looking at is a legitimate thing to ask for, and tying the two
together was the confusion this replaced.

**Clash** marks where the two chosen channels are fighting for the same place in
the spectrum — intensity is how hard, and each region is outlined so a broad
shallow clash and a narrow fierce one do not read as the same smudge. A cell
counts when **both** are above −60 dB **and within 12 dB of each other**; past
that the louder one is simply masking the quieter, which is a different problem.

### What it will not do

A bus at a **different sample rate** is listed but cannot be picked, with its
rate as the hint: a different rate picks a different window and so a different
group delay, and two pictures offset by an amount nobody can see is worse than
one that says it will not draw.

A Listen-In on a **muted track** publishes nothing. The picture keeps moving —
that source is simply drawn as the silence it is sending. It used to stop
everything, including this plugin's own track, which is the one failure worth
naming here because the editor went on claiming it was live.

## The crosshair reads the picture

Hover anywhere over it and a hairline each way follows the pointer, with the
three numbers under the picture naming exactly what is beneath it:

| | |
|---|---|
| `freq` | the band's centre frequency, from the axis the analyzer sent |
| `time` | how long ago that column was measured — or, in the bar view, `pos`: the bar and beat it sits on, as `2:2.3` |
| `level` | what the analyzer measured there, in dBFS |

The level is the byte the engine encoded, read back through the same mapping
that wrote it — not the pixel's colour inverted, which would answer a question
about the palette rather than about the audio.

It is honest under pause, too: the numbers freeze with the picture, so the
readout and the pixel under it always describe the same moment.

## Pause holds the view, not the analysis

The columns keep arriving and keep filling the history behind the frozen
picture, so letting go shows a view that is already current — with the paused
seconds *in* it, scrolled past, rather than cut out of it.

The plugin is never told that you paused; it is a repaint gate in the editor.
Which is why pausing cannot affect the audio, and why the history behind the
freeze is real rather than reconstructed.

## The range dropdown zooms

| range | span |
|---|---|
| Full | 10 Hz – 20 kHz |
| Sub | 10 – 200 Hz |
| Bass | 40 – 800 Hz |
| Mid | 200 Hz – 4 kHz |
| High | 2 – 20 kHz |

They overlap on purpose. All 256 bands spread over whatever is chosen.

It does **not** add bins — the window is what decides those. What it does is
give the ones that exist the whole height: in Sub, ~33 bins crowded into the
bottom 40 pixels become ~33 bins at 8 pixels each, which is what makes 50 Hz and
60 Hz two visibly different rows. Higher up there are several bins to a band
already, and the zoom is detail in the ordinary sense.

Changing it while audio runs takes no lock and allocates nothing:
`spectro_set_range` stores a request, the audio thread rebuilds its own band
table at the next frame, and columns measured against the old range are dropped
rather than drawn under the new scale.

## Installing

The release carries an unsigned universal bundle in all three formats. macOS
will refuse to load it until the quarantine attribute is removed:

```sh
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/NISpectrogram.vst3
```

## For developers

The column format on the wire, and the mount-ordering bug that the `ready` tag exists
to fix, are in [plugins/spectrogram/ui/README.md](../ui/README.md).

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

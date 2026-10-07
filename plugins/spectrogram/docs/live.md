---
section: live
title: Ableton Live Interface
---

The whole interface is the picture. There are no parameters to automate and
nothing to set up: insert it, and it draws what is passing through.

## It remembers how you left it

The view (which channels are ticked), the two Compare pickers, the **Clash**
switch and the **range** zoom are saved with your Live set. Close the window
and open it again, or save the set and reopen it tomorrow, and the Spectrogram
is looking at what it was looking at — not back at its own input on the full
range.

Pause and the **Span** are not saved: a reopened window always starts live,
in seconds.

## Reading it

Frequency is the vertical, logarithmic, 10 Hz at the bottom and 20 kHz at the
top across 256 bands — about two per semitone, so a semitone is roughly two
rows. Time scrolls right to left and the visible window holds thirteen seconds.

Brightness is level: the ground is quiet, a near-white core is loud, and the
violet between them is the falloff.

The quiet line at the top, just left of **Range**, states what the picture
spans: the frequencies it really drew, bottom band to top band, and the floor
below which everything is the ground, `−96 dB`. The top is the analyzer's own,
so at 44.1 kHz it reads a little under 20 kHz, because nothing above half the
sample rate exists to draw. Until the plugin has sent its first axis it says
`waiting for the plugin`.

Time is also ticked along the bottom, a mark a second from `0 s` at the right
edge back to `−12 s` at the left. The ticks are arithmetic rather than a clock:
one column is one pixel and the engine holds the column rate constant across
sample rates, so the width *is* the span.

## The x-axis can be bars instead of seconds

The **Span** select beside Range says what the x-axis is and how much of it:
`13 s`, the history it holds in seconds, or `1 bar` to `16 bars`. Choose a
number of bars and the picture stops scrolling. The x-axis becomes a window of
the host's own timeline, 1, 2, 4, 8 or 16 bars wide, and columns fill it left
to right. When the sweep reaches the right edge it wraps to the left and
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
moves while you audition. The line at the top gives the tempo in the bar view,
and says `free` after it (`120 BPM free`) when the sweep is running on that
rather than on a playhead.

One honest limit: the analyzer finishes about 47 columns a second whatever the
window is, so a window shorter than ~13 seconds has fewer columns than it has
pixels. The gaps are interpolated rather than held flat, which keeps the picture
smooth, but a 1-bar window at a fast tempo genuinely has less time resolution
than a wide one. It is smoothed, not invented back.

## Listening in on other tracks

Put an **NI Listen-In** on the track you want and give it a name. The
Spectrogram then lists every bus that exists, by that name, in two separate
controls — and they are separate on purpose, because they answer different
questions. It reads up to three buses at once beside its own input.

For example, to see a bass and a pad together: put a Listen-In named `bass` on
the bass track and one named `pad` on the pad track, then tick `bass` and `pad`
under **View** in the Spectrogram.

If you delete a Listen-In and put it back, or its track is re-created, the
Spectrogram finds it again by itself within about a second — there is nothing
to re-tick.

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

### Compare — what the hatching is measuring

Two pickers and a switch, independent of the view: comparing two channels you
are not currently looking at is a legitimate thing to ask for, and tying the two
together was the confusion this replaced.

**Clash** marks where the two chosen channels are fighting for the same place in
the spectrum — intensity is how hard, and each region is outlined so a broad
shallow clash and a narrow fierce one do not read as the same smudge. A cell
counts when **both** are above −60 dB **and within 12 dB of each other**; past
that the louder one is simply masking the quieter, which is a different problem.

### What it will not do

A bus at a **different sample rate** — a Listen-In in another session running
at 96 kHz while this one runs at 48, say — is listed under View but greyed
out, with its rate as the hint (`96k`), and cannot be ticked. A different rate
picks a different window and so a different group delay, and two pictures
offset by an amount nobody can see is worse than one that says it will not
draw. If you choose such a bus in a Compare picker, it is never analysed, so it
adds nothing to the clash marking. Once the sender runs at this session's rate,
the bus becomes available again.

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

It works from the keyboard as well. Press **Tab** until the picture has the
focus ring and the crosshair appears at its centre. The arrow keys move it one
pixel, ten with **Shift** held. **Home** and **End** jump to the left and right
edges (in seconds, the oldest and the newest column), and **Escape** takes it
away.

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

Changing it while audio plays is safe and immediate: columns measured against
the old range are dropped rather than drawn under the new scale, so the picture
never shows two scales at once.

## Installing and updating

1. **Quit Live completely** — Live keeps every plugin it has loaded in memory
   until it quits, so a bundle replaced while it runs is not the one you hear.
2. Copy `NISpectrogram.vst3`, `NISpectrogram.component` and
   `NISpectrogram.clap` into `~/Library/Audio/Plug-Ins/VST3`, `…/Components`
   and `…/CLAP`.
3. The bundles are unsigned, so clear the quarantine attribute:

   ```sh
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/NISpectrogram.vst3
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/NISpectrogram.component
   ```

4. Start Live, open **Settings → Plug-Ins** and press **Rescan** beside
   *Rescan Plug-Ins*. NI Spectrogram appears in the browser under **Neon Ingvy**.

Update NI Listen-In at the same time: a Spectrogram reads buses only from a
Listen-In of the same release (see the NI Listen-In manual).

**On an Intel Mac**, reading Listen-In buses now works. Older builds could not
open an existing bus there, so the View list stayed at `no Listen-In found`;
if that is what you saw, updating both plugins fixes it.

Every bundle carries `LICENSE` and `THIRD_PARTY_LICENSES.md` in
`Contents/Resources/`.

## The background, and the Motion switch

The window's background is not a static image: it keeps time with your song.
While Live's transport plays, every beat sends one slow wave out from every
panel edge and from the window border, and the first beat of each bar sends a
stronger one. The waves reflect, cross and fade out over about twenty seconds,
and the dots and grain swell and thin as they pass. Stop the transport and the
background comes to rest.

It follows Live's tempo and time signature, not the sound: it pulses the same
way in every Neon Ingvy plugin, on a drum bus or on a silent track. The plugin
keeps time **only while its window is open**: close the window and it stops, so
the background costs nothing in a set you are only playing.

The **Motion** switch in the hint bar at the bottom turns it off. That setting
is remembered on your machine, separately for each Neon Ingvy plugin, and is not
a plugin parameter — it will not be automated, saved into a preset, or changed
on somebody else who opens your project. If your system is set to reduce
motion, it is off regardless.

The window does not repeat the plugin's name — Live already shows it in the
title bar. The **Neon Ingvy** mark sits at the right end of the hint bar.

There is more detail, including what happens on a loop, a jump and in 6/8 or
7/8, under **The Animated Ground**, on the Tech pages.

<!-- NOT A LINK. This file is rendered in two places -- on the docs site, which
     is served under a base path, and on GitHub, which is not -- so a path that
     works in one is broken in the other, and `ctest -R site_links` catches the
     site half. Naming the page works in both. -->

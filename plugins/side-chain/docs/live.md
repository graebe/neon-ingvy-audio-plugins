---
section: live
title: Ableton Live Interface
---

The window is one picture on one axis, and the axis is **one cycle**: the
audio you got, and over it the shape you asked for. They line up on purpose: a
dip in the waveform sits directly under the curve that made it.

Below the picture sit the knobs, then two rows: the trigger's (**Source**, and
**Rate** or **Gate** for the source you chose) and the shape's (**Curve** and
**% of cycle**). The header above the picture says what the trigger is doing.

## The shape well

The line with four handles is what the stage controls describe — an idealised
single duck, from open, down, held, and back. The plugin's engine draws it from
the parameters as they are this moment, so a handle you drag is under the
pointer at once.

Behind it is the audio: the input dimmed, the output in front, so wherever the
duck took something away the input shows as a halo around the output. The thin
line mirrored about the centre is **what actually happened**: the gain the
plugin applied, measured. Most of the time it sits on the shape and you do not
see it. When it parts from the shape, something interrupted a duck — a trigger
arriving part way through a recovery anchors on the level the envelope had
actually reached rather than restarting from the top. That is the whole reason
both are drawn.

The vertical rule is the playhead, where in the cycle the plugin is now. The
ruler under the well is in milliseconds: the cycle's length at the end, and a
mark at the instant the duck reaches the bottom. The caption above names the
picture and its span — `SHAPE   ONE CYCLE, 500 MS` — and, while the stage
times read in milliseconds, their total: `STAGES 225 MS`. With nothing coming
in, it says `NOTHING REACHING THE PLUGIN`.

Four handles, and each one is a parameter:

| handle | drag | changes |
|---|---|---|
| the first, on the ceiling | sideways | **Delay** — when it begins |
| the corner at the bottom | sideways | **Attack** — how fast it gets there |
| | up and down | **Depth** — how far down |
| the second corner | sideways | **Hold** |
| the last, back on the ceiling | sideways | **Release** |

Hold **Shift** as you press for fine control. **Double-click** a handle to reset
what it moves to the plugin's default — double-clicking the bottom corner puts
both Attack (2 %) and Depth (100 %) back. Because they are parameters, dragging
one is an ordinary edit: it lands in Live's undo history and it writes to the
automation lane, exactly as turning the knob below it would.

**From the keyboard**, Tab to a handle and it behaves as a slider:

| key | what it does |
|---|---|
| ← / → | moves the handle along the cycle — Delay, Attack, Hold or Release — by 1 % of its range (**Shift**: 0.2 %) |
| ↑ / ↓ | on the bottom corner only: Depth. ↑ is a shallower duck, as dragging up is |
| Page Up / Page Down | along the cycle by 10 % |
| Home / End | to the start or the end of its range |

For example, to make the duck let go later: Tab to the last handle and press →
a few times; each press is one undo step.

An **amber line at the right edge** means the shape is longer than one cycle and
cannot finish before the next trigger. That is allowed — the overlap is a real
sound — but it is worth knowing you are hearing it.

### What the audio shows

Input and output are drawn as the minimum and maximum of every column rather
than an average, because a transient is a fraction of a column and averaging
would show you a signal nobody is playing.

The columns are locked to the trigger rather than scrolling, which is what keeps
them under the shape. One consequence is worth stating: a column is rewritten
once per cycle. At 1/4 and 120 bpm that is twice a second and reads as live; at
1/1 and 60 bpm the picture really is four seconds old.

A part-drawn picture is a picture still filling, not a signal that stopped — the
trace simply ends where the playhead has got to.

### How loud the audio is drawn

The audio **zooms to fit the track**. Most material peaks well under full scale
— a bass at −18 dBFS, a pad at −24 — and on a full-scale axis it would be a
thin strip through the middle of the well. So the well draws the audio on a
range that follows what is playing, and says which in its top-right corner:
`−15 dB` means the well's top edge is −15 dBFS. The range sits **3 dB above the
loudest peak of the last two seconds**, so the loudest moments reach about
seven-tenths of the way to the edge and never touch it. It moves smoothly, never
in jumps, and only the label is rounded, to whole decibels:

- **Louder material lifts it quickly** — most of the way in a few frames, past
  the new peak within a tenth of a second. A sudden very loud hit can run off the
  edge for that tenth of a second; it is cut at the edge, never drawn outside the
  well.
- **Quieter material lowers it slowly.** The well holds the range for two
  seconds after the last loud moment, then eases down: two thirds of the way in
  a second and a half, nearly all of it in about five. A track whose level stays
  the same keeps the range perfectly still — nothing pumps with the beat.
- **Silence stays silence.** The range never goes below `−48 dB`, so a track
  that stops does not blow its noise floor up to fill the well. Over a
  full-scale track it reads `+3 dB`: the headroom above 0 dBFS.

**Only the audio zooms.** The shape and the measured gain are gains — how much of
the input is let through — so they stay on their own scale, and so do the
handles. A fully open duck is on the ceiling at `−24 dB` as at `0 dB`; the audio
under it is simply drawn larger.

For example, with the plugin on a pad peaking at −20 dBFS the corner reads
`−17 dB`, the pad fills the well, and you can see exactly where the duck bites.
Turn the pad up by 12 dB and the range rises to `−5 dB` as the louder pad
arrives, in about a tenth of a second; turn it back down and, two seconds later,
the well eases back to `−17 dB` over the next few.

**Double-click the well** — anywhere but on a handle, where a double-click resets
the handle — to hold the audio at full scale; the corner then reads
`0 dB fixed`. Double-click again to let it follow the track. The choice lasts as
long as the plugin window is open: closing and reopening it starts on the
following range again. To a screen reader the well is a picture that takes a
press, which does the same.

## Knobs and readouts

The knobs work as in every Neon Ingvy editor: drag (Shift for fine),
**double-click to reset** to the default, and **click the readout** to type a
value — **Enter** commits, **Escape** leaves it as it was. With a knob focused,
the arrow keys turn it by 1 % (Shift: 0.2 %), Page Up/Down by 10 %, Home/End go
to the ends, and Enter opens the readout. Hover over any control, or Tab to it,
and the hint bar at the bottom says what it does in one line.

| knob | what it sets |
|---|---|
| **Depth** | how far the signal is pushed down; 100 % is silence |
| **Delay** | when the duck starts. Its arc grows from 12 o'clock both ways: left of it, on Cycle, the duck starts early |
| **Attack**, **Hold**, **Release** | how fast the duck gets down, how long it stays, how long it takes to come back |
| **Note**, **Channel**, **Velocity** | with Source on MIDI only: the trigger note, its channel, and how much velocity scales the depth |
| **Threshold**, **Lockout** | with Source on Sidechain only: how loud the key must get, and how soon it may fire again |

Only the knobs of the source you chose are there: a Threshold on a tempo-locked
duck would invite turning it and concluding the plugin is broken. Note and
Channel step one note or one channel at a time.

The four stage readouts follow **% of cycle**: off, they read in milliseconds
(`40 ms`); on, in percent of the cycle (`20.0 %`). Type either — the unit you
type wins, so `40 ms` is milliseconds and `20 %` a percentage whatever the
readout shows, and a bare number is in the unit shown. The same goes for the
handles. Threshold's lowest setting reads `-inf dB`.

## Choosing a trigger

**Cycle** needs nothing set up. Pick a rate and it fires on every division,
locked to the transport. It survives loop jumps and tempo ramps because the
host's position is treated as something to follow rather than something to
divide.

**MIDI** takes a note (**Note**, C1 by default), a **Channel** (1 by default,
or Omni), and the **Gate** switch: off, the Hold times out on its own; on, the
duck stays down until the note is released. **Velocity** lets velocity scale
the depth. Only with Source on MIDI do notes duck — on the other two sources
the plugin ignores them. A note ducks from its own sample, not from the start
of the buffer it arrived in.

**All Notes Off and All Sound Off** (CC 123 and CC 120) open the duck at once,
whatever note it listens to, when they arrive on its channel — on any channel
with Omni — so a held Gate note can never leave the track down after a panic. The plugin hears them while it is bypassed
too, and a note released while it is bypassed is released.

> **Live does not route MIDI to a plugin on an audio track.** MIDI mode fires
> when Side-Chain sits on a MIDI track after an instrument. If you are on an
> audio track, use Cycle or Sidechain. The header says `no midi` when nothing is
> arriving, so this is visible rather than mysterious.

**Sidechain** listens to a real key input. **Threshold** sets how loud the key
has to be; **Lockout** sets how close two triggers may be, which is what stops a
snare 30 ms behind the kick firing a second duck.

### Routing a key in Live

1. Put NI Side-Chain on the track you want ducked — the bass, say — and set
   **Source** to Sidechain.
2. Open the device's **sidechain** section: click the small triangle in the
   device's title bar.
3. In **Audio From**, choose the track that should do the ducking — the kick —
   and where to take it from (Pre FX, Post FX or Post Mixer).
4. Play. Every kick above Threshold now ducks the bass along the shape you drew.

The plugin takes a stereo or mono main input and a stereo or mono key, on a
sidechain input of its own, which is how Live delivers a key to a VST3.

The header shows `no key routed` until something is patched.

## Times are percentages of the cycle

All four stages are proportional to the cycle, always. Change the tempo or the
rate and the shape keeps its proportions, which is what staying in time means.

The **% of cycle** switch chooses whether you read them as percentages or as
milliseconds; it changes the reading, not the sound. While they read in
milliseconds, the caption above the picture shows their total.

## What the header tells you

Source, rate, and which stage the envelope is in right now. On the right, at
most one amber mark, because a ducker whose trigger is not arriving looks
exactly like one set to zero depth:

| mark | means |
|---|---|
| `no key routed` | Source is Sidechain and no key is patched |
| `no input` | the track is silent, so there is nothing to duck |
| `transport stopped` | Source is Cycle and the transport is not running |
| `no midi` | Source is MIDI and no trigger note has arrived |
| `no trigger` | Source is Sidechain and the key has not crossed Threshold |

The last three appear only after the trigger has been missing for most of a
second, so they do not flicker between hits.

## Installing and updating

1. **Quit Live completely** — Live keeps every plugin it has loaded in memory
   until it quits, so a bundle replaced while it runs is not the one you hear.
2. Download `side-chain-<version>-macOS.zip` from the release (universal: Apple
   silicon and Intel) and copy `NISideChain.vst3` from it into
   `~/Library/Audio/Plug-Ins/VST3`, replacing the one there.

3. A bundle that is not notarised is refused until its quarantine
   attribute is removed:

   ```sh
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/NISideChain.vst3
   ```

4. Start Live, open **Settings → Plug-Ins**, turn on **Use VST3 Plug-In System
   Folders** if it is off, and press **Rescan**. NI Side-Chain appears in the browser
   under **Neon Ingvy**.

NI Side-Chain is a VST3, and only a VST3, for macOS; Linux and Windows follow in a
later release. **Updating from a release before v2026.10.08.2?** The
[changelog](../../../CHANGELOG.md) says which older bundles to delete, and what a set
that used one of them needs.

Signing needs an Apple Developer ID and a notarytool round trip; until those
exist, the `xattr` command is the difference. It is a property of the
distribution, not of the plugin.

The licence notices travel with the plugin: the bundle carries `LICENSE`,
`THIRD_PARTY_LICENSES.md`, the font's `OFL.txt` and the texts of the licences
of what it is built with (`AGPL-3.0.txt`, `Apache-2.0.txt`) in
`Contents/Resources/`.

### Coming from an earlier version

**Sets saved with an earlier version open as they were saved**: the plugin
keeps the identity, the fifteen parameters and the saved state of every earlier
VST3 build, so the shape, the trigger and its automation come back.

A few things look different to Live:

- **Automation drawn on a MIDI controller does not come back.** The earlier
  build listed 128 MIDI controllers, aftertouch and pitch bend as parameters of
  their own (IDs 65538 to 65667). This build takes MIDI controllers as MIDI, so
  those parameters are gone, and a lane drawn on one finds nothing to move.
  Nothing in the plugin ever read them; the panic still works, because CC 120
  and CC 123 still arrive.
- The plugin's **Bypass** has a new ID (15, where it was 65536). Live treats
  it as the plugin's bypass either way, and a set's bypass state comes back.
- In Live's own panel for the plugin, the choices — Source, Rate, Time, Curve,
  Channel, Trigger and Mode — may show as stepped sliders rather than menus.
  They still step through the same values and show the same words.
- Where Live shows a parameter's short name, it is the first eight letters of
  its name.

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

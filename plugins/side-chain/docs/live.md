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

## Knobs and readouts

The knobs below the wells work as in every Neon Ingvy editor: drag (Shift for
fine), **double-click to reset** to the default, and **click the readout** to
type a value — **Enter** commits, **Escape** leaves it as it was. With a knob
focused, the arrow keys turn it by 1 % (Shift: 0.2 %), Page Up/Down by 10 %,
Home/End go to the ends, and Enter opens the readout.

The stage readouts are always percentages of the cycle, so type a percentage:
`25` into Release is a quarter of a cycle. Threshold's lowest setting reads
`-inf dB`.

## Choosing a trigger

**Cycle** needs nothing set up. Pick a rate and it fires on every division,
locked to the transport. It survives loop jumps and tempo ramps because the
host's position is treated as something to follow rather than something to
divide.

**MIDI** takes a note (**Trigger**, C1 by default), a **Channel** (1 by
default, or Omni), and **Mode**: in Trigger mode the Hold times out on its own;
in Gate mode the duck stays down until the note is released. **Vel** lets
velocity scale the depth. Only with Source on MIDI do notes duck — on the
other two sources the plugin ignores them.

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

The plugin takes a stereo or mono main input and a stereo or mono key. The key
arrives on **inputs 3 and 4**, after the main pair, which is how Live delivers
a sidechain to a VST3 or AU. A mono track with a mono key works in VST3 and
AU. In the CLAP build a key needs a stereo main — CLAP packs its inputs back to
back, so on a mono track the key could not be told apart from the main.

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
most one amber mark, because a ducker whose trigger is not arriving looks
exactly like one set to zero depth:

| mark | means |
|---|---|
| `no key routed` | Source is Sidechain and no key is patched |
| `key is the input` | Source is Sidechain and the key is a copy of the track itself |
| `no input` | the track is silent, so there is nothing to duck |
| `transport stopped` | Source is Cycle and the transport is not running |
| `no midi` | Source is MIDI and no trigger note has arrived |
| `no trigger` | Source is Sidechain and the key has not crossed Threshold |

The last three appear only after the trigger has been missing for most of a
second, so they do not flicker between hits.

## Installing and updating

1. **Quit Live completely** — Live keeps every plugin it has loaded in memory
   until it quits, so a bundle replaced while it runs is not the one you hear.
2. Copy `NISideChain.vst3`, `NISideChain.component` and `NISideChain.clap`
   into `~/Library/Audio/Plug-Ins/VST3`, `…/Components` and `…/CLAP`.
3. The bundles are unsigned, so clear the quarantine attribute:

   ```sh
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/NISideChain.vst3
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/NISideChain.component
   ```

4. Start Live, open **Settings → Plug-Ins** and press **Rescan** beside
   *Rescan Plug-Ins*. NI Side-Chain appears in the browser under **Neon Ingvy**.

Every bundle carries `LICENSE` and `THIRD_PARTY_LICENSES.md` in
`Contents/Resources/`.

## The background, and the Motion switch

The window's background is not a static image: a kick drum makes it ring. Each
bass onset — a kick, the attack of an 808 — sends one slow wave out from every
panel edge and from the window border; the waves reflect, cross and fade out
over about twenty seconds, and the dots and grain swell and thin as they pass.
With no bass playing it is completely still.

Only 20–80 Hz counts, and only a sudden rise in it, so a snare, a hi-hat or a
sustained bass note will not move it however loud it is. The plugin listens for
these **only while its window is open**: close the window and the detector
stops, so the background costs nothing in a set you are only playing. It also
stops drawing whenever the window is hidden.

The **Motion** switch in the hint bar at the bottom turns it off. That setting
is remembered on your machine, separately for each Neon Ingvy plugin, and is not
a plugin parameter — it will not be automated, saved into a preset, or changed
on somebody else who opens your project. If your system is set to reduce
motion, it is off regardless.

The window does not repeat the plugin's name — Live already shows it in the
title bar. The **Neon Ingvy** mark sits at the right end of the hint bar.

There is more detail, including why two very close kicks read as one, under
**The Animated Ground**, on the Tech pages.

<!-- NOT A LINK. This file is rendered in two places -- on the docs site, which
     is served under a base path, and on GitHub, which is not -- so a path that
     works in one is broken in the other, and `ctest -R site_links` catches the
     site half. Naming the page works in both. -->

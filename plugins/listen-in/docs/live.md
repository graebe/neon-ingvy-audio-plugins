---
section: live
title: NI Listen-In in Ableton Live
---

## Where to put it

Anywhere the audio you want is already flowing: on a track, or inside an
instrument rack's chain. It is an insert like any other and passes audio through
bit for bit, so it can sit in the middle of a chain without changing what comes
out of it. **Latency is zero** — a tap that made the track arrive late would be
a tap nobody leaves in place.

Put it *after* whatever you want to hear. An NI Listen-In before the compressor
publishes the uncompressed signal, which is sometimes what you want and is
almost never what you meant.

## Setting it up

1. Pick a **Bus** — 1 to 16. It is a host parameter, so Live saves it with the
   set and you can automate it.
2. Type a **Name**. Thirty-one bytes; a colon or a newline is dropped, because
   the name travels in a message that is parsed by position.
3. The meter moves when audio is passing. The word beside the title says
   `listening` when the bus is live.

Repeat on a second track with a different bus number. That is the setup the
overlaid spectra need.

## When it says something other than "listening"

**`slot taken`** — another NI Listen-In already holds that bus. Two senders on one
bus is the single collision this design can have, and the second one refuses
rather than fighting over it. Pick a free number; `abus_tap` with no arguments
lists what is in use.

**`unavailable`** — the shared-memory segment could not be opened. In practice
this means the host is sandboxing the plugin, which Live does not do for VST3 or
AU. If you see it in Live, it is worth reporting.

## Checking it is really routing

Before any plugin exists that reads a bus, `abus_tap` is how you confirm the
tap end works against real audio:

```
build/tests/abus_tap          # what every bus is doing
build/tests/abus_tap 3        # follow bus 3
```

Play something. The peak should track what you hear, and `dropped` should stay
at zero.

## What Live will not do for you

The bus number lives in the **plugin**, not in the track. Duplicating a track
duplicates the NI Listen-In on it, bus number and all — so the copy lands on a bus
that is already taken and says so. Change it, which is one click and is
preferable to a silent second sender fighting the first.

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

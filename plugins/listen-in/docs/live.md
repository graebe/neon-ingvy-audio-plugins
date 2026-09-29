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

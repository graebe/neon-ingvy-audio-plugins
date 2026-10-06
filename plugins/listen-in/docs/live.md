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
2. Click the **Name** field and type, say, `bass`. **Enter** keeps it,
   **Escape** abandons the edit. Up to thirty-one bytes; a colon or a control
   character such as a newline is dropped.
3. The meter moves when audio is passing. The status word at the top says
   `listening` when the bus is live, and the hint bar names the bus and the
   name.

Repeat on a second track with a different bus number. That is the setup the
overlaid spectra need.

## When the bus is not live

The status word says what is wrong, and the hint bar says why:

| status | hint bar | what to do |
|---|---|---|
| `slot taken` | bus *N* is taken — another Listen-In holds it | another NI Listen-In already holds that number, on another track or in another open set. Pick a free number; if you removed the other one, move Bus off the number and back to claim it |
| `unavailable` | bus unavailable — the host may be sandboxed | the bus could not be opened at all. Live does not sandbox VST3 or AU, so in Live this is worth reporting |
| `idle` | status starting | the plugin has not been given audio yet — press play |

A slot is never shared, and never taken over silently: the second NI Listen-In
on a number waits, idle, until you give it one of its own.

## After a restart

Nothing to do. Reopen the set and every NI Listen-In claims its saved bus
number again under its saved name. A Spectrogram reading it reconnects by
itself.

## Checking it is really routing

NI Spectrogram is the everyday way to see a bus. Without one, the test tool
`abus_tap` (built with the repository's tests) confirms the tap end works
against real audio:

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

## Installing and updating

1. **Quit Live completely** — Live keeps every plugin it has loaded in memory
   until it quits, so a bundle replaced while it runs is not the one you hear.
2. Copy `NIListenIn.vst3`, `NIListenIn.component` and `NIListenIn.clap` into
   `~/Library/Audio/Plug-Ins/VST3`, `…/Components` and `…/CLAP`.
3. The bundles are unsigned, so clear the quarantine attribute:

   ```sh
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/NIListenIn.vst3
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/NIListenIn.component
   ```

4. Start Live, open **Settings → Plug-Ins** and press **Rescan** beside
   *Rescan Plug-Ins*. NI Listen-In appears in the browser under **Neon Ingvy**.

**This release changes the bus format, so update NI Listen-In and NI
Spectrogram together and quit Live fully before you do.** An old and a new
build cannot share a slot: whichever claims a number second replaces the
other's bus, and a Spectrogram does not list a bus written by a build other
than its own. If Live is still running with the old plugin loaded while the
new one is installed, the two can end up side by side, each saying it is
listening, with nothing reaching the Spectrogram. Quitting Live fully makes
every instance the new one.

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
the background costs nothing in a set you are only playing. It also stops
drawing whenever the window is hidden.

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

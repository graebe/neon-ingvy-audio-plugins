---
section: live
title: Ableton Live Interface
---

The editor shows the pattern as a ring, the controls in three panels, and the
steps as pads underneath. It is the same picture the Move draws, at a size a
screen allows.

## The ring

The ring **is** the pattern, and it mirrors the Move display. Filled arcs are
steps that sound, hollow ones are gaps, and the band thickens with the step's
Amount. A dot inside the ring is the playhead; a bracket outside it is the step
you are editing.

While the fade is part way in, a step that has not arrived yet draws as a
**hollow** arc — it is in the pattern, so it is not a gap, and it is not
sounding, so it is not a fill. The pads say the same thing the same way.

## Editing a step

| gesture | what it does |
|---|---|
| click a pad | toggles the step, and selects it |
| shift-click a pad | sets a **tie** — the step holds through the next one without retriggering |
| drag up/down on a pad | sets that step's Amount. Drag to the bottom and the step turns off |
| click a wedge on the ring | toggles that step, like its pad |
| drag around the ring | paints the same state across every wedge you cross |

For example, to make step 5 half as loud as the others: press on its pad and
drag down to the middle of the pad. To fill a
whole bar at once, press on the ring's first wedge and sweep round.

## Controls

**Gate** holds the four that decide the shape of the pattern in time:

| | |
|---|---|
| **Rate** | the length of one step, as a musical division — 1/1T through 1/128, including triplets |
| **Length** | how many steps the pattern has, 1–128. The pads wrap at sixteen to a row and the window grows to fit |
| **Amount** | dry/wet for the whole effect. 0% is a true bypass |
| **Width** | how much of a step stays open before it releases, 5–100 % |

**Envelope** holds **Attack, Decay, Sustain** and **Release**, which shape every
step. Attack, Decay and Release are measured against the gate — see
[Timing](../README.md#timing) — and Sustain is a level. Amount and Sustain glide
over about 5 ms when they move, so sweeping them never clicks.

**Fade** introduces the steps one at a time, in the order they carry:

| | |
|---|---|
| **Fade** | how much of the drawn pattern is present. **This is the knob a build-up is drawn on** |
| **Dir** | **In** brings the steps you drew on in, from silence. **Out** brings the *holes* in, from a gate that has none. 100% is the pattern either way |
| **Soft** | a step arriving ramps in on its own level rather than jumping on. An arriving hole ramps the other way — from a full step down to a gap |
| **ORDER** | tap the steps in the order the fade should introduce them. The button counts how far into the sequence you are; press it again to finish |
| **SHUFFLE** | a random arrival order, leaving the pattern alone |

The numbers on the pads are the arrival order, and they are drawn only while
ORDER is on or the fade is part way in — the rest of the time they would be
clutter, because the fourth-step borders already say where the bars are.

**Click a number to type one.** If the number you type is already taken, the two
steps **swap**, so nothing between them moves. The hits and the holes are ranked
separately — a step is one or the other, never both — so Fade Out sequences the
holes and Fade In the hits.

**A pad's border is what you drew; its fill is what you hear.** A step waiting to
arrive keeps its outline with no fill; a hole that Fade Out has not removed yet is
lit with no outline. Neither can be mistaken for the other, or for a step you
drew.

Below those, four controls decide how the rest is read:

| | |
|---|---|
| **Slot** | which of the 8 patterns is playing and being edited |
| **Join Neighbors** | consecutive on-steps run together instead of retriggering |
| **Curve** | Linear, Exponential or S-Curve, applied to the envelope stages |
| **Time** | whether the stages are read in **ms** or as a **%** of the gate's width — the same envelope, two ways of asking for it |

These fifteen are ordinary host parameters and automate normally. The pattern and
its arrival order are not among them — see
[what the host can automate](../README.md#what-the-host-can-automate-and-what-it-cannot).

## Working with knobs and readouts

Every knob works the same way.

- **Drag** up or down to turn it. Hold **Shift** as you press for fine control —
  the same drag moves it a fifth as far.
- **Double-click** a knob to reset it to its default. To put Amount back to
  100 %, double-click the Amount knob. The reset is one ordinary edit, so Live's
  undo takes it back.
- **Click the readout** under a knob to type a value. The field opens with the
  text selected: type, then press **Enter** to commit or **Escape** to leave the
  value as it was. Clicking elsewhere commits too.

Typing a stage time follows the **Time** setting, and a unit you type wins over
it. With Time on `ms`, typing `40` into Attack sets 40 milliseconds; typing
`25 %` sets a quarter of the gate even though the readout is showing
milliseconds. With Time on `%`, `40` means 40 % and `40 ms` still means
milliseconds.

### Length holds on whole bars

Most patterns are half a bar, a bar, two bars or four bars long, so the
**Length** knob makes those lengths easy to hit. Small marks outside its arc
show where they are, and the mark you are on lights up. While you drag past one,
the knob stays on that length for a short stretch before it moves on. You can
land on it without aiming, and you never get stuck there.

Say Rate is **1/32** and the set is in 4/4. A bar is 32 steps, so Length holds
at **16, 32, 64 and 128**. To get a two-bar pattern, drag Length up from where
it is. When the readout says 64, let go. Change Rate to 1/16 and the marks move
to 8, 16, 32 and 64. They follow Live's time signature too: at 1/16 in 3/4 they
are 6, 12, 24 and 48. A triplet rate holds on its own bars: 1/16T in 4/4 holds
at 12, 24, 48 and 96. The marks always count in the current slot's Rate.

The other ways to set Length ignore these lengths. Hold **Shift** as you drag
to pass through them, or type any length into the readout. With the knob
focused, **Page Up** and **Page Down** jump straight to the next one. The holds
are part of the editor only: automation and Live's own controls set Length one
step at a time, as before.

## The keyboard

Everything the pointer does, the keyboard does too. **Tab** moves between
controls; a focused control shows a ring.

| on | key | what it does |
|---|---|---|
| a knob | ↑ / → and ↓ / ← | turns it by 1 % of its range (**Shift**: 0.2 %) |
| | Page Up / Page Down | by 10 % (Length: to the next whole-bar length — see above) |
| | Home / End | to the minimum or the maximum |
| | Enter | opens the readout to type a value |
| the pads | arrow keys | move between pads (↑ and ↓ jump a row) |
| | Space or Enter | toggles the focused step, like a click (with **Shift**: a tie) |
| | **Alt** + ↑ / ↓ | raises or lowers that step's Amount by 10 % (**Shift** too: 1 %) |
| the ring | ↑ / → and ↓ / ← | Length, one step longer or shorter |
| | Page Up / Page Down | Length to the next whole-bar length (four steps where there is none) |
| | Home / End | Length 1 or 128 |
| the Pattern / Signal tabs | arrow keys | switch tab |
| a menu (Slot, Curve, Time…) | | opens and chooses the way your system's menus do |

For example, to tie step 9: Tab to the pads, press → until step 9 is focused,
then **Shift + Space**. To thin it out, **Alt + ↓** twice takes it to 80 %.

## Random

Fills the current slot with a new pattern and a new arrival order — a Euclidean
gate, so the hits are spread evenly and one always lands on the downbeat. Ties
are cleared and the levels return to full. It does not disturb the playhead, so
it is safe to press while the transport runs.

## The envelope plot

Under the ring, one gate on a millisecond axis: the envelope the patch
produces, from the step opening to the release dying away. It is **the
engine's own rendering**, not a drawing of the controls — the plugin runs the
patch through a spare copy of the engine and plots what comes out — so the
curve, the Curve shape and the point where the release outlives the step are
exactly what you will hear.

## Pattern and Signal

Two tabs, laid over the right edge of the plot they switch between.

**Pattern** plots one cycle of the gate — and it is not a drawing *of* the gate,
it **is** the gate: the plugin renders the real patch through a spare engine with
a steady input and sends the samples, so a release that outlives its step, a tie,
Join Neighbors and the fade are all correct because none of them is being
reasoned about. Amount is the one thing applied when it paints, because it is a
floor under the curve rather than a different curve.

**Signal** shows the dry input against what the plugin did to it, on the same
axis: one cycle of the pattern, standing still, with the trace filling left to
right and a violet line marking where it is being written. The envelope is drawn
over it as an outline, so the gap between the outline and the trace is the
difference between what the gate asked for and what the audio did. The dry is
grey rather than a dimmer violet because the difference between the two should
not need a legend.

## Copy and paste

The two icons beside RANDOM are **Copy slot** and **Paste into slot**. Use the
icons, not **⌘C** and **⌘V**: Live keeps those keys for its own menu, so a
plugin window never receives them. The icons work anyway, because the plugin
reads and writes the clipboard itself.

To start slot 2 from slot 1:

1. Select slot 1 and press **Copy slot**. The hint bar says "Copied slot 1."
2. Select slot 2.
3. Press **Paste into slot**. The hint bar says "Pasted into slot 2." Slot 2
   now has slot 1's pattern and its whole sound; slot 1 is unchanged.

To copy a slot to another track, press **Copy slot** in one Trance Gate and
**Paste into slot** in the other, on the slot you want to replace.

A whole patch on the clipboard — a Move patch, or what the copy icon put
there in a Trance Gate older than v2026.10.06.1 — still pastes, and replaces
all 8 slots as before. Anything else is refused with the reason in the hint
bar, and nothing changes. See
[Patch interchange](../README.md#patch-interchange).

## Export and import

Under the envelope plot, **EXPORT**, **EXPORT ALL** and **IMPORT** save the
current slot or all 8 slots to a file and load them back; see
[Slot files](../README.md#slot-files). The result shows in the hint bar for a
few seconds. All three buttons work from the keyboard: Tab to them and press
Enter or Space.

## Installing and updating

1. **Quit Live completely** — not just the set. Live keeps every plugin it has
   loaded in memory until it quits, so a bundle replaced while it runs is not
   the one you hear.
2. Copy `NITranceGate.vst3`, `NITranceGate.component` and `NITranceGate.clap`
   from the release into `~/Library/Audio/Plug-Ins/VST3`, `…/Components` and
   `…/CLAP`.
3. The bundles are unsigned, so macOS refuses them until the quarantine
   attribute is removed:

   ```sh
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/NITranceGate.vst3
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/NITranceGate.component
   ```

4. Start Live, open **Settings → Plug-Ins** and press **Rescan** beside
   *Rescan Plug-Ins*. NI Trance Gate appears in the browser under **Neon Ingvy**.

Signing needs an Apple Developer ID and a notarytool round trip; until those
exist, the `xattr` command is the difference. It is a property of the
distribution, not of the plugin.

The licence notices travel with the plugin: every bundle carries `LICENSE` and
`THIRD_PARTY_LICENSES.md` in `Contents/Resources/`.

**Coming from a bundle named `TranceGate`** (before v2026.09.29.1)? The plugin's
identity did not change — Live stores the plugin's IDs, not the filename, so
sets relink after a rescan — but an old bundle left beside the new one is two
bundles claiming one ID. Delete the old ones:

```sh
rm -rf ~/Library/Audio/Plug-Ins/VST3/TranceGate.vst3 \
       ~/Library/Audio/Plug-Ins/CLAP/TranceGate.clap \
       ~/Library/Audio/Plug-Ins/Components/TranceGate.component
```

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

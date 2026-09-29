---
section: live
title: Ableton Live Interface
---

The editor is a WebView: Solid drawing the Ultraviolet design system, with the
engine behind a C ABI. It is the same picture the Move draws, at a size a screen
allows.

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
| click | toggles the step, and selects it |
| shift-click | sets a **tie** — the step holds through the next one without retriggering |
| drag up/down | sets that step's Amount |

## Controls

**Gate** holds the four that decide the shape of the pattern in time:

| | |
|---|---|
| **Rate** | the length of one step, as a musical division — 1/1 through 1/64, including triplets |
| **Length** | how many steps the pattern has, 1–32 |
| **Amount** | dry/wet for the whole effect. 0% is a true bypass |
| **Width** | how much of a step stays open before it releases |

**Envelope** holds **Attack, Decay, Sustain** and **Release**, which shape every
step.

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

All fifteen are ordinary host parameters and automate normally. The pattern and
its arrival order are not among them — see
[what the host can automate](../README.md#what-the-host-can-automate-and-what-it-cannot).

## Random

Fills the current slot with a new pattern and a new arrival order — a Euclidean
gate, so the hits are spread evenly and one always lands on the downbeat. Ties
are cleared and the levels return to full. It does not disturb the playhead, so
it is safe to press while the transport runs.

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

## Copy patch / Paste patch

These move the whole state as one string — see
[Patch interchange](../README.md#patch-interchange). It is the same text the
Move module writes, so a pattern travels between the hardware and the DAW in
either direction.

## Installing

The release carries an unsigned universal bundle in all three formats. macOS
will refuse to load it until the quarantine attribute is removed:

```sh
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/NITranceGate.vst3
```

Signing needs an Apple Developer ID and a notarytool round trip; until those
exist, that one command is the difference. It is a property of the distribution,
not of the plugin.

**The bundle is `NITranceGate` as of v2026.09.29.1**, where it used to be
`TranceGate`. The plugin's identity did not change -- a host stores the
four-character IDs, not the filename, so sessions relink after a rescan -- but an
old bundle left beside the new one is two bundles claiming one ID, which hosts
report in their own confusing ways. Delete the old ones:

```sh
rm -rf ~/Library/Audio/Plug-Ins/VST3/TranceGate.vst3 \
       ~/Library/Audio/Plug-Ins/CLAP/TranceGate.clap \
       ~/Library/Audio/Plug-Ins/Components/TranceGate.component
```

## For developers

The editor talks to the plugin over numbered message tags — the protocol, and
why the pattern travels as a state blob rather than as parameters, is in
[plugins/trance-gate/ui/README.md](../ui/README.md).

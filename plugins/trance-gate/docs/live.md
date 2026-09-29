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
step. Below them, four controls decide how those are read:

| | |
|---|---|
| **Slot** | which of the 8 patterns is playing and being edited |
| **Join Neighbors** | consecutive on-steps run together instead of retriggering |
| **Curve** | Linear, Exponential or S-Curve, applied to the envelope stages |
| **Time** | whether the stages are read in **ms** or as a **% Step** — the same envelope, two ways of asking for it |

All twelve are ordinary host parameters and automate normally. The pattern is
not one of them — see
[what the host can automate](../README.md#what-the-host-can-automate-and-what-it-cannot).

## Pattern and Signal

Two tabs under the controls. **Pattern** plots one cycle of the gate — the
envelope actually applied, step by step, with the playhead crossing it.
**Signal** shows the dry input against what the plugin did to it, which is why
the dry trace is grey rather than a dimmer violet: the difference between the
two should not need a legend.

## Copy patch / Paste patch

These move the whole state as one string — see
[Patch interchange](../README.md#patch-interchange). It is the same text the
Move module writes, so a pattern travels between the hardware and the DAW in
either direction.

## Installing

The release carries an unsigned universal bundle in all three formats. macOS
will refuse to load it until the quarantine attribute is removed:

```sh
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/TranceGate.vst3
```

Signing needs an Apple Developer ID and a notarytool round trip; until those
exist, that one command is the difference. It is a property of the distribution,
not of the plugin.

## For developers

The editor talks to the plugin over numbered message tags — the protocol, and
why the pattern travels as a state blob rather than as parameters, is in
[plugins/trance-gate/ui/README.md](../ui/README.md).

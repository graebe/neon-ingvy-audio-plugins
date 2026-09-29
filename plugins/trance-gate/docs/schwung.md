---
section: schwung
title: Schwung Interface
---

On the Move the Trance Gate is a [Schwung](https://github.com/charlesvestal/schwung)
`audio_fx` chain component. Same engine, same patches — a display of 128×64
pixels and 32 pads instead of a window.

## The ring

The ring is the pattern. Filled arcs are steps that sound, hollow ones are gaps,
and thickness is the step's Amount. A dot inside the ring is the playhead; a
bracket outside it is the step you are editing.

## The pads

One pad per step, from the top left.

| pad | meaning |
|---|---|
| green | the step sounds |
| red | it is a gap |
| dark | past the end of the pattern |

**Brightness is that step's Amount.** A quiet step is a dimmer pad, but it never
goes below about half — so an on step always reads as on. The **selected** step
is one shade lighter than the same step would otherwise be.

A press toggles the step *and* selects it. **Shift + press** sets a tie: the step
holds through the next one without retriggering. A white pad sweeps with the
playhead while the transport runs.

## Gate page

| knob | control |
|---|---|
| 1 | **Len** — steps in the pattern, 1–32 |
| 2 | **Rate** — length of one step |
| 3 | **All Amount** — dry/wet for the whole effect; 0% is a bypass. Shown as the bar down the right-hand side |
| 4–7 | **Attack, Decay, Sustain, Release** |
| 8 | **Step Amount** — how loud *this* step is; 0% is silent. Pick the step with a pad first |

## Settings page

Attack, Decay, Sustain and Release shape every step, and **Gate** is how much of
a step stays open before releasing. **All Amount** is the dry/wet for the whole
effect. **Slot** picks one of 8 patterns.

**Fade** introduces the steps one at a time in the order the patch carries, and
100% is the whole pattern. **Dir** chooses which end it builds up from: In brings
the steps you drew on in, from silence; Out brings the *holes* in, from a gate
that has none. **Shape** chooses whether an arrival ramps (Soft) or jumps (Hard). **Rnd** rolls a new pattern and a new arrival
order — turn it to `Roll` and back to `Hold`; each turn to `Roll` is one roll, and
it does not disturb the playhead.

The fade is on the settings page rather than under the ring, and that is the
eight-knob ceiling rather than a choice: a canvas page carries the level's first
eight knobs and every one of those is already load-bearing. The ORDER itself is
edited in the Live plugin; a patch carries it in both directions, so a build-up
sequenced at the desk plays back on the hardware.

With the transport stopped the gate holds open and the audio passes.

## Installing

Open the Web Manager on your Move — `http://move.local:7700` — go to
**Modules**, and install from the GitHub URL:

```
graebe/neon-ingvy-audio-plugins
```

It installs to `modules/audio_fx/trance-gate/` and is added to a chain slot as
an Audio FX. **Nothing else is required** — it runs on released Schwung, 1.3.0
or newer.

> The module moved into this repository recently, and the Schwung catalog entry
> still names the old one. Until that is updated and the first tag is cut here,
> install from the URL above rather than from the catalog listing.

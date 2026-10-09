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

One pad per step, from the top left. A pattern can be up to 128 steps and
there are 32 pads, so the pads show the block of 32 that holds the step you
have selected; at 32 steps or fewer they show the whole pattern.

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

### Steps 33 to 128

With **Len** above 32, the **left and right arrow buttons** page the pads
through the pattern a block of 32 at a time: steps 1–32, 33–64, 65–96 and
97–128. The selected step moves with the pads and keeps its place on the grid,
so the third pad of one block becomes the third pad of the next, and paging
back returns you to the pad you left. The screen says which block you are
on: the top-left corner, which shows the length, reads **33-64** with
**of 96** beneath it.

For example, to silence the last step of a four-bar gate (64 steps at 1/16):
press the right arrow once, so the screen reads 33-64 of 64, then press the
bottom-right pad.

A few details:

- **The ends stop.** Right on the last block and left on the first do nothing;
  the pads do not wrap round.
- **A short last block** — 33–40 of a 40-step pattern — has fewer than 32 pads
  lit. Paging into it from a pad past its end selects its last step.
- **Shortening Len** below the block you are on moves the pads to the new last
  block, with the selected step on the pattern's last step.
- **The pads stay where you are editing** while the pattern plays: they do
  not follow the playhead, because the knobs edit the selected step and it
  would change under your fingers. The ring shows the playhead across the whole
  pattern, and the white pad appears whenever it passes through your block.
- At 32 steps or fewer the arrows do nothing.

> **The arrows need a newer Schwung than 1.7.3.** Up to 1.7.3 the host never
> hands claimed buttons to a module that draws its own screen, so the arrows
> keep doing Move's own thing and steps past 32 can't be reached from the
> pads. The fix is
> [schwung#633](https://github.com/charlesvestal/schwung/pull/633). Paging
> works in the first Schwung release that includes it; update Schwung from the
> web manager when that release is out.

## The ring page

The editor has two pages, the ring page and the settings page. **Turn the jog
wheel** to move between them; the arrows page the pads, not the knobs.

The page with the ring puts these under the eight encoders:

| knob | control |
|---|---|
| 1 | **Slot** — which of the 8 slots plays and is edited. Each slot is a complete sound; turning Slot recalls its pattern and every knob, and the knob page shows the new values |
| 2 | **All Amount** — dry/wet for the whole effect; 0% is a bypass. Shown as the bar down the right-hand side |
| 3 | **Step Amount** — how loud *this* step is; 0% is silent. Pick the step with a pad first |
| 4–7 | **Attack, Decay, Sustain, Release** |
| 8 | **Gate** — the step's Width: how much of it stays open before releasing |

For example, to soften the off-beat hats in a 16-step gate: press pad 3, turn
knob 3 down to 60 %, then do the same on pads 7, 11 and 15.

## Settings page

The next page holds the settings you set once: **Len** (steps in the pattern,
1–128), **Rate** (the length of one step), **Time** (whether the envelope reads
in ms or as a % of the gate), **Curve**, and the fade.

Attack, Decay and Release are a percentage of the gate, exactly as in the
plugin, so the envelope keeps its shape when the tempo changes. Amount and
Sustain glide over about 5 ms when you turn them, so a knob sweep is smooth
rather than stepped.

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

With the transport stopped the gate holds open and the audio passes. Starting
and stopping the transport no longer clicks: the gate picks up from the level
it is at, and opens smoothly on a stop.

The module renders the same samples as the plugin, rounded to the Move's 16-bit
audio rather than cut off — quiet, gated passages keep their last bit of detail
instead of losing it towards silence.

## Installing

Open the Web Manager on your Move — `http://move.local:7700` — go to
**Modules**, and install from the GitHub URL:

```
graebe/neon-ingvy-audio-plugins
```

It installs to `modules/audio_fx/trance-gate/` and is added to a chain slot as
an Audio FX. **Nothing else is required** — it runs on released Schwung, 1.3.0
or newer.

> If the Schwung catalog listing points somewhere other than this repository,
> install from the URL above rather than from the listing.

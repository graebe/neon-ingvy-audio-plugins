---
section: schwung
title: On the Ableton Move
---

The same Rust engine as the plugin, wrapped in Schwung's `audio_fx` vtable
instead of the VST3 shell. Not a port and not a reimplementation — one core, two
shells, with a test that renders four seconds through both paths and compares
them sample for sample.

## Install

```sh
./modules/_shared/package.sh side-chain   # cross-builds in Docker, makes the tarball
./modules/_shared/install.sh side-chain   # scp to move.local
```

The module id is `ni-side-chain`. That is deliberately not `ducker`:
[`charlesvestal/schwung-ducker`](https://github.com/charlesvestal/schwung-ducker)
already owns that id on the device.

## What is different here

**No Sidechain source.** The chain host hands over one interleaved buffer and
the API has no aux input anywhere on the device. So `Source` offers Cycle and
MIDI, and the third option is *absent* rather than present and silent — along
with Threshold and Lockout, which belong to a detector with nothing to listen
to. A control that can never do anything reads as a broken module.

**MIDI has no sample offset.** `move_audio_fx_on_midi` takes no offset, so a
note lands at the top of the block it arrived in. The plugin does better because
VST3 reports one. This is the one place the Move version is measurably behind,
and it is also what makes the two comparable: drive the plugin at offset zero and
the renders agree.

**Panic works through MIDI.** The v2 vtable has no reset hook, so CC 120 (All
Sound Off) and CC 123 (All Notes Off) are the whole of it. Either one opens the
gate immediately and forgets the trigger, whatever the note filter says — a
ducker still holding a note after a panic leaves a track silent with nothing
playing, which is the worst way this could fail.

**No Kick picture.** The plugin can draw the kick you duck against behind its
shape, from an NI Listen-In or its sidechain key. The Move has neither, and the
module has no screen picture of the duck, so there is nothing to choose.

## Choosing the trigger note

**Trigger** is a note *name*, stepped a semitone per detent: `C-2` up to `G8`,
numbered the way Live numbers them. The default is **C1** — MIDI note 36, the
note a kick pad usually sends — and resetting the knob to its default puts it
back on C1. **Channel** offers Omni and 1–16, and defaults to 1.

For example, to duck on a snare on D1: set Source to MIDI, turn Trigger two
semitones up to `D1`, and set Channel to the channel your snare pad plays on —
or to Omni to take it from any.

## The interface

The host's own knob grid, from the module's `chain_params`. The four envelope
stages are declared as a `viz` group, so the grid draws the duck's shape from
the same numbers the plugin's editor does.

There is no custom canvas page in this version. That is a scope decision rather
than an omission: a hand-written page would be a second editor to keep in step
with the first, and the `shape` canvas entry is already declared so one can be
added without changing the parameter contract.

## In your browser

The module comes with its own editor page, which looks and works like the
plugin's editor in Live. It runs in the browser on a laptop or phone on the
same network as the Move, and it changes the NI Side-Chain that is running on
the Move.

1. Put NI Side-Chain on a track's audio effect slot.
2. Open **http://move.local:7700/remote-ui** and choose that track's tab
   (**Slot 1** to **Slot 4**).
3. The editor appears in the module's section, in place of the generic
   controls. **Interface: Default** switches back to them.

**Drag the shape** to set Delay, Attack, Hold, Release and Depth, as in Live.
The knobs below are the same values as numbers: turn one, or click its readout
and type. **Source** chooses between Cycle and MIDI, and only the chosen
source's controls are shown: Rate for Cycle, and Note, Channel, Mode and Vel
for MIDI.

Changes go both ways. Turn a knob on the Move and the page follows. Drag on the
page and the Move's knob moves. The playhead runs through the shape while the
duck is moving, and the background ripples on the beat of a Cycle duck.

What the page cannot do on the Move:

- **No audio in the shape.** The Move sends a page values, not sound, so the
  input and output that Live draws behind the shape are not shown, and neither
  is the *no input* warning.
- **No Sidechain source, Threshold or Lockout.** The Move has no key input
  (see above).
- **Readouts can lag by a beat.** The page hears about the Move's own changes
  up to twice a second, and moves the playhead smoothly in between.
- **On Master FX the picture stands still.** Schwung sends a Master FX page
  the values but not the live readouts, so the controls work and the playhead
  does not move. On a track's slot it does.


---
section: schwung
title: On the Ableton Move
---

The same Rust engine as the plugin, wrapped in Schwung's `audio_fx` vtable
instead of iPlug2's. Not a port and not a reimplementation — one core, two
shells, with a test that renders four seconds through both paths and compares
them sample for sample.

## Install

```sh
./modules/side-chain/package.sh     # cross-builds in Docker, makes the tarball
./modules/side-chain/install.sh     # scp to move.local
```

The module id is `ni-side-chain`. That is deliberately not `ducker`:
[`charlesvestal/schwung-ducker`](https://github.com/charlesvestal/schwung-ducker)
already owns that id on the device, and its MIDI trigger semantics are what this
engine's were ported from — see `THIRD_PARTY_LICENSES.md`.

## What is different here

**No Sidechain source.** The chain host hands over one interleaved buffer and
the API has no aux input anywhere on the device. So `Source` offers Cycle and
MIDI, and the third option is *absent* rather than present and silent — along
with Threshold and Lockout, which belong to a detector with nothing to listen
to. A control that can never do anything reads as a broken module.

**MIDI has no sample offset.** `move_audio_fx_on_midi` takes no offset, so a
note lands at the top of the block it arrived in. The plugin does better because
iPlug2 reports one. This is the one place the Move version is measurably behind,
and it is also what makes the two comparable: drive the plugin at offset zero and
the renders agree.

**Panic works through MIDI.** The v2 vtable has no reset hook, so CC 120 (All
Sound Off) and CC 123 (All Notes Off) are the whole of it. Either one opens the
gate immediately and forgets the trigger, whatever the note filter says — a
ducker still holding a note after a panic leaves a track silent with nothing
playing, which is the worst way this could fail.

## The interface

The host's own knob grid, from the module's `chain_params`. The four envelope
stages are declared as a `viz` group, so the grid draws the duck's shape from
the same numbers the plugin's editor does.

There is no custom canvas page in this version. That is a scope decision rather
than an omission: a hand-written page would be a second editor to keep in step
with the first, and the `shape` canvas entry is already declared so one can be
added without changing the parameter contract.

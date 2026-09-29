---
section: m4l
title: NI Trance Gate for Max for Live
---

The same gate as the plugin, as a device that draws its pattern **inside Live's
chain**. No window to open: the pads, the playhead and the controls are in the
device row.

## Why this exists beside the plugin rather than instead of it

Live draws a third-party plugin's editor in a floating window and gives the
device row a generic strip of knobs. For most effects that is fine. For a step
sequencer it is backwards — the pattern *is* the instrument, and it is the one
thing the chain cannot show. Max for Live is the only way to draw it there.

The cost is that **a Max for Live device needs Live Suite** (or the Max for
Live add-on). The VST3 does not, and it is not going anywhere: it remains the
answer for Live Standard and Intro, and for Logic, Bitwig, Reaper and
everything else. See [the plugin's Live notes](../../trance-gate/docs/live.md).

## It is the same gate, and that is enforced rather than claimed

There is one engine and it is written once. `tg.gate~`, the external this
device is built around, links **the same `libtg_capi.a`** the VST3, the AU and
the CLAP link, and that the Schwung module for the Move wraps. There is no
second envelope to drift.

What is *not* shared is the transport: every other host hands over a beat
position, and Max hands over ticks. `tests/render_m4l.c` renders four seconds
through this shell's tick conversion and asserts the same FNV-1a hash
`tests/render_plugin.c` gets from the beats path. A wrong divisor moves it.

## Patches travel

The pattern is not a host parameter in either shell — it is the engine's own
state blob, the same text the Move module writes. So **Copy patch** in the
VST3 pastes into this device, this device's pastes into the VST3, and both
move to and from the hardware. See
[Patch interchange](../../trance-gate/README.md#patch-interchange).

## Building it

The external is off by default, because it needs a submodule a checkout that
only wants the plugin should not have to fetch:

```sh
git submodule update --init external/max-sdk-base
cmake -B build -DTG_BUILD_M4L=ON
cmake --build build --target tg.gate_tilde
```

`tg.gate~.mxo` lands in `plugins/trance-gate-m4l/externals/`, universal
(arm64 + x86_64). To author the patcher, add that folder to Max's file
preferences; to ship it, **freeze the device** so the external travels inside
the `.amxd`.

Max externals are per-platform. macOS is built here; Windows (`.mxe64`) and
Push 3 (aarch64 Linux) are follow-ups, and the Push 3 slice is nearly free
because `tg-capi` already cross-builds for the Move.

## The object, for anyone opening the patcher

`tg.gate~` is two signal inlets and two signal outlets, plus two message
outlets. It takes:

| message | what it does |
|---|---|
| `num <index> <value>` | one of the twelve, in the units a user reads. **The index is `tg_param_t`'s** |
| `param <key> <value>` | the engine's string door, for anything that is not one of the twelve |
| `step <index> <0\|1\|2>` | off / on / tie |
| `depth <index> <0-255>` | that step's Amount |
| `bang` | emit the `ui` readout, and the slot's length if it just changed |
| `getstate` / `setstate <blob>` | the patch, as one string |

`bang` rather than a push from the audio thread is deliberate: the only thread
that knows the playhead moved may not touch an outlet, so the patcher asks —
a `qmetro` into the inlet — and the answer is produced on the thread that
asked.

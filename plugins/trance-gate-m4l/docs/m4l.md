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

## The device itself

`NI Trance Gate.amxd` is **generated**, not hand-built:

```sh
node plugins/trance-gate-m4l/build-device.mjs
```

A `.amxd` is a binary-framed blob of machine-written JSON — unreadable in a
diff and full of numbers that have to agree with things stated elsewhere in
this repository. Two in particular: every control's `num` index has to be its
`tg_param_t` index, and the Rate menu has to be the engine's ladder in the
engine's order (`rates.rs` says *"APPENDED, never inserted: RATE_DEFAULT is an
index into this table"*). Both are silent when wrong — the device works and
means something else — so the generator reads the ladder out of `rates.rs` and
`ui/test/device.test.mjs` asserts both facts back out of the built file.

Run the generator when the **parameters** change. Once Max has laid the device
out, Max owns the pixels and saves them back into the file; re-running the
generator would discard that.

## Still needs eyes in Max

Everything above is machine-checked as far as it can be. These are not, and
cannot be — a structural test can say the file is the shape a device is, not
that Live opens it:

- [ ] Point Max's file preferences at `plugins/trance-gate-m4l/externals/`,
      then open the device. `tg.gate~` must instantiate.
- [ ] **Null test.** The `.amxd` on one track, the VST3 on another, same
      source, one inverted. Any residue is a transport or wire bug, not taste.
- [ ] The twelve appear in Live's own parameter list and automate.
- [ ] Click toggles, shift-click ties, drag sets the amount — the same
      gestures as the plugin and the Move.
- [ ] Change Slot: Length must follow the slot, not overwrite it.
- [ ] Copy patch from the VST3, paste into the device, and the reverse.
- [ ] Save the Set, reopen: pattern, ties, depths and all eight slots intact.
- [ ] Freeze, then load on a machine without the external installed.

Two things are known to be unfinished rather than unverified: **the pattern is
not yet persisted** with the device (the `v8` holding the state blob across
save/load is not written), and the layout is at workable positions rather than
designed ones.

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

# Trance Gate

The DSP behind the Trance Gate, in both of its shells. `tg-core` is the engine;
`tg-capi` wraps it in a C ABI for the Ableton Live plugin and `tg-move` wraps it
in Schwung's `audio_fx` vtable for the Ableton Move. Modelled on the Kilohearts
Trance Gate.

- 8 pattern slots, length 1–128 steps, ties between steps
- Resolution as a musical division (1/1T … 1/128, incl. triplets)
- Per-step ADSR, a gate-length control, and one **Amount** at two scopes —
  global (dry/wet, 0% is a true bypass) and per step (an accent)
- Locked to song position via `get_beat_position()`, so it stays bar-aligned
- Pattern edited **on the pads** — press toggles a step, Shift+press ties it
  into the next one. Green is sound, red is a gap, brightness is that step's
  amount, and a **white pad sweeps with the playhead** while the transport runs
- Circular display: filled segments are sound on, hollow are off, and the band
  thickens with the step's amount

Chain `audio_fx` component.

## Install

Both shells are built and released from this repository — see the
[Trance Gate documentation](../../plugins/trance-gate/README.md) for the plugin
and the module.

## Build from source

From the repository root:

```bash
cmake --build build --target schwung    # cross-compiles via Docker -> dist/
./modules/_shared/install.sh trance-gate  # scp to ableton@move.local
ctest --test-dir build -R tg_           # the engine's tests, no device needed
```

## Releasing

Tagging is what publishes, and the **tag picks the channel**: a version
containing `-beta.` updates `channels.beta`, anything else updates
`channels.stable` and the top-level fields a channels-unaware manager reads.

`versions.json` decides the version and `modules/trance-gate/module.json` must
agree with it — the workflow fails the build if the tag disagrees with either,
before it builds anything. The tag names the product, because this repository
releases more than one.

```bash
git tag trance-gate-v2026.09.29.3 && git push origin trance-gate-v2026.09.29.3
```

The tag is the product prefix plus `versions.json`'s version as written. A beta
is only offered when it is strictly newer than stable.

## Licence

**MIT**, © 2026 Torben Gräber. See `LICENSE`, which also ships inside the
module tarball -- what lands on a device is a `.so` and a `.js` with no
repository near them, and MIT asks that the notice travel with the copy.

Every crate here has **no external dependencies at all** -- `Cargo.lock` holds
`tg-core`, `tg-capi` and `tg-move` and nothing else -- so there is no
third-party licence to be compatible with and nothing to attribute.

It was briefly GPL-3.0-or-later, on the belief that the Ableton Live plugin
had to be. It does not: Steinberg relicensed the **VST3 SDK to MIT**, and
their GPLv3 and proprietary options are withdrawn. What forced the GPL was
nih-plug's third-party VST3 bindings, which are GPLv3 and now needlessly so.
A plugin built on iPlug2 against the official SDK is MIT throughout.

---
title: Compatibility Promises
order: 8
slug: compatibility
---

Everything NI Trance Gate has promised to keep working, in one place, with
what holds each promise to account. The product is three weeks old and has
already been a C engine, a Rust engine, a JUCE plugin, an iPlug2 plugin, a
JUCE plugin again and a Max for Live device. Each of those left something
behind that users' files or other programs depend on. A promise costs
something every time the format changes, so this page is also where one is
retired.

## The promises

| # | Promise | Why | Held by |
|---|---|---|---|
| 1 | The 15 parameters keep their index, ID, name, range, default and units | A Live set stores plain values by index; the host shows the names | `tg_core::schema` (`the_host_columns_are_what_the_iplug2_build_reported`), `tests/tg_processor.cpp`, `tests/fixtures/iplug2/NITranceGate/parameters.json` |
| 2 | Parameters, rates and readout fields are only ever appended | The index is the C ABI, the automation ID and a saved value | comments at `tg-core/src/params.rs` and `rates.rs`; `schema::tests::the_table_is_in_abi_order` |
| 3 | The plugin's VST3 class ID is the iPlug2 build's | Old Live sets find the plugin | `IPLUG2_CLASS` in `plugins/trance-gate/CMakeLists.txt`; `tests/tg_host.cpp` |
| 4 | Every layout of the `NIst` chunk loads (15, 14 and 12 parameters, headerless and versioned) | Projects saved by every released build | `tests/nist_fixtures.cpp`, `tests/tg_processor.cpp` |
| 5 | State blobs from v0 to v7 load, and a newer one is read best-effort | Projects and Move presets from every build | `tg-core/tests/fixtures.rs` (about 50 blobs), `tests/state_round_trip.rs` |
| 6 | A v7 blob is written byte for byte as v7 writes it | Older builds and the Move read it | `tg-core/tests/fixtures.rs` |
| 7 | Slot and bank files, format version 1 | Files users saved | `tg-core/src/slotfile/tests.rs` |
| 8 | Every clipboard form ever written pastes (slot, bank, whole patch, pre-v2026.10.06.2 copy) | Text users keep in notes | `tg-core/src/paste/tests.rs` |
| 9 | The `tg_*` C ABI is source-compatible with its last hand-written headers | The archived Max for Live external (`archive/trance-gate-m4l`) | `capi_compat_tg`, `capi_compat_tg_names` |
| 10 | Plugin and Move render the same bytes for the same patch | One engine, two shells | `tests/tg_render_golden.h`, `engines/trance-gate/tests/run.sh`, and the in-process A/B in `tg-move` |
| 11 | The Move module runs on released Schwung of its declared minimum | Devices in the field | `modules/trance-gate/module.json`, `modules/_shared/schwung.version` |
| 12 | Users who had the earlier bundle named `TranceGate` are told to delete it | The same plugin ID in two bundles confuses Live | `plugins/trance-gate/docs/live.md` |

## What each costs

- **1–4 (Live sets)** cost nothing day to day. They only forbid renaming or
  reordering, and the schema now states them once.
- **5–8 (saved text)** cost a reader per version. Eight readings of the blob
  live in `Patch::from_fields`, `Instance::load` and `read_pattern`.
- **9 (the Max for Live ABI)** freezes the `tg_*` surface for a device the
  archive itself calls unfinished ("pattern persistence and layout were
  open"). Every addition must be appended to a fixture by hand, and nothing may
  ever be removed.
- **10** moves on purpose with every intended sound change, and is cheap.
- **11** depends on a host whose API has no version contract (see
  `docs/tech/ci-schwung.md`).

## Proposal (owner's decision)

1. **Retire promise 9.** The Max for Live device was archived unfinished on
   2026-09-29 and has no users. Without it, the C ABI is a private boundary
   between two parts of one build, and `capi_compat_tg` can shrink to "the
   generated header compiles against the plugin".
   - Keep the tag, so the device can still be built against the commit it
     was written for.
2. **Keep 1–8 and 10–12.** They protect real users' files and devices.
3. **Set a floor for promise 5** once the oldest blob versions (v0–v2, from
   before the first public tag) are known not to exist in the field. Until
   then, keep them: a reader is cheap, and a project that silently loads
   wrong is not.

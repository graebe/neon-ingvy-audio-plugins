---
title: Architecture
order: 2
slug: architecture
---

Four layers, and each one exists because the layer above it cannot do the job.

```
Rust core        the DSP, on the shared ni-dsp and established crates
  ├── C ABI      extern "C", for the plugin
  └── Schwung    the audio_fx vtable (ni-schwung), for the Move
C++ shell        JUCE 9 — the VST3 for macOS, Windows and Linux, and the host plumbing
Native editor    JUCE Components, drawing the Ultraviolet design system
```

Three decisions shaped these layers. Each is recorded with its context and
what it costs:

- [0001](../adr/0001-gpl-3.0-or-later.md): the licence is GPL-3.0-or-later
  rather than MIT.
- [0002](../adr/0002-juce-native-editors.md): JUCE is the shell, and the
  editors are native JUCE Components.
- [0003](../adr/0003-established-rust-crates.md): established Rust crates
  replace hand-written code.

## The Rust core

One Cargo workspace at the repository root. A dependency on this repository's
own code is a `path`: to a sibling inside the same engine, or to one of the
product-free shared crates (`engines/shared`, `ground`, `shell`, `audio-bus`).
Those are rlibs. Each product's single static library absorbs them, and the
crates from crates.io with them, so a plugin still links exactly one archive.

Having no external crates was a rule, kept so that the MIT licence audit
finished in one sitting. [0003](../adr/0003-established-rust-crates.md)
reversed it. The FFT is realfft's, the Spectrogram's rings are rtrb's, the
float atomics are atomic_float's, the bus maps its memory through libc and
windows-sys, the Side-Chain decodes MIDI with wmidi, the Trance Gate reads its
formats with serde_json, and the engines read numbers with lexical-core. The
shell's command queue is rtrb's too, its snapshot triple_buffer's and its
handoff basedrop's (see Threads below). Each is under a licence on the
allowlist, and its version is written once, in the root `Cargo.toml`'s
`[workspace.dependencies]`. The C headers are next. The one-archive rule
stays.

`[profile.release]` sets `panic = "abort"`, and that one is load-bearing rather
than a size tweak: unwinding out of an `extern "C"` function into a C or C++
host is undefined behaviour. Aborting also removes the landing pads, which
matters for a module that ships to a device.

## The C ABI

`tg-capi` exports the `tg_core_*` C ABI (`trance_gate_core.h`) — byte for byte the surface
the original C engine exported, and grown since. That was a deliberate
constraint when the DSP was ported to Rust: keeping the ABI identical meant the
existing C tests (`engines/trance-gate/tests/test_core.c` and `test_gate.c`)
were relinked against the new engine rather than rewritten, so the port was
checked by tests that had never seen Rust.

The C headers are generated. Each `*-capi` crate's `build.rs` writes its own
with cbindgen from the Rust it declares (`engines/shared/cbindgen/capi_header.rs`,
configured by the crate's `cbindgen/*.toml`), into the build's
`cargo/include` directory, so a header cannot drift from the Rust side. What a
generated header can still do is compile and mean something else to a caller,
so the plain-C tests call through every one of them, and `tests/spectro_columns.c`
exercises the analyzer's boundary specifically: argument order, the meaning of
`bands`, and who owns the output buffer. The Trance Gate's headers have a caller
outside this repository, the archived Max for Live external, so their last
hand-written versions are fixtures (`tests/fixtures/tg-capi`) that
`capi_compat_tg` and `capi_compat_tg_names` hold the generated ones to,
declaration for declaration. Only C++ with no Rust behind it stays hand-written:
`engines/shell/include`'s state header and denormal guard, and Schwung's own
headers vendored for the Move-side tests.

## The JUCE shell

The build recipe is three files. `cmake/NiRust.cmake` builds the engines through
[Corrosion](https://github.com/corrosion-rs/corrosion), pinned in
`cmake/NiCorrosion.cmake`; `cmake/NiJucePlugin.cmake` builds the plugins around
them.

`ni_add_rust_engine(<target> CRATE <crate> LIB <lib> [INCLUDE <dir>])` registers
a product's C ABI, and `ni_build_rust_engines()` imports every registered crate
with Corrosion, which runs cargo once per crate in one target directory, so the
crates they share (`ni-dsp`, `ground`, `shell`, `audio-bus`) compile once:

1. Corrosion builds each product's static library for the build's target
   triple, which it takes from the C++ toolchain, so a Linux or Windows build
   selects its triple through its CMake toolchain file
2. a universal macOS build is the one thing Corrosion does not do: it builds
   one triple per CMake project. So each further slice is a sub-build
   (`cmake/rust-slice`, the same import for `x86_64-apple-darwin`), and
   `lipo -create` joins each product's two slices into one universal `.a`
3. each is exposed as a CMake `INTERFACE` library its plugin and tests link

It is still **one static library per plugin**: each product's capi crate is a
staticlib that absorbs the rlibs it depends on, because two Rust staticlibs in
one binary each carry the Rust runtime. The cargo step always runs — cargo is the
dependency scanner, not CMake — and Corrosion copies an archive out with
`copy_if_different`, so an unchanged engine keeps its timestamp, `lipo` does not
run again, and the plugin does not relink.

A plugin's `CMakeLists.txt` is one `ni_add_juce_plugin(<product> TARGET … NAME …
CODE … ENGINE … [EDITOR] SOURCES …)` call. It decides once, for every product:
VST3 only, from Neon Ingvy; the version from `versions.json`; the licence
notices copied into the bundle; on macOS an ad hoc signature as the bundle's
last build step; the bundle collected in `build/out`, where every test and
validator loads it, and copied into the system's VST3 folder only with
`-DNI_DEPLOY_PLUGINS=ON`. A product that took over an earlier bundle keeps its
VST3 class ID (`VST3_CLASS`, or `IPLUG2_CLASS` from
`tests/fixtures/iplug2/ids.json`) and its parameter IDs (`LEGACY_PARAM_IDS`), so
a saved set finds it exactly as it found the old one.

## The shared shell

Every processor derives from `ni::Processor` (`plugins/_shared/juce`), a
`juce::AudioProcessor` with the house's answers to what every product would
otherwise answer on its own:

- **The block.** `processBlock` is `final`: it runs the product's `process()`
  under `juce::ScopedNoDenormals`, and a bypassed block — the host's bypass or
  the product's own Bypass parameter — goes to `processBypassed()`, which a
  product whose engine must keep hearing MIDI overrides.
- **The clock.** `readClock()` reads the host's transport into a plain value,
  allocation-free, for the engines' block clocks and the ground's beat clock
  (`GroundClock.h`).
- **The state.** `getStateInformation` and `setStateInformation` are `final`
  and call `writeState` and `readState`. A product that took over an earlier
  bundle reads and writes that bundle's chunk through `ni::nist` (`Nist.h`),
  one codec for every product's layout; its parameters are a table
  (`ParamSpec.h`) built into `ni::Parameter`s that hold plain values exactly, so
  a fixture loaded and saved again is the same bytes.
- **Dirty.** `nonParameterStateChanged()` tells the host that state it cannot
  see changed — an import, a pasted pattern — so the set is marked unsaved.

`ni::PluginEditor` is the window a host opens: the product's editor at its
design size, scaled by its Zoom, in the kit's look. `MachineSettings` keeps what
belongs to the machine rather than the set, such as the Motion switch.
`plugins/_shared/ni` holds the host-free helpers: `ni/Wire.h` (the float round
trip, the passthrough, locale-free numbers) and `ni/Scope.h` (the capture a plot
draws), which `tests/cpp` links without a host.

## Threads

A plugin's engine belongs to the **audio thread**, and to nothing else. The
editor's messages, the host's state calls and the idle timer all run elsewhere,
and the engines have no locks in them, by design. So there are exactly two doors,
built once in `engines/shell` and used by every product:

- **In, as a command.** A non-audio thread posts an edit; the audio thread
  applies it at the top of its next block. Each product has its own command
  type, and whatever arrives as text — an edit's key and value, a host's blob,
  the clipboard, a slot file — is read on the posting thread, so the audio
  thread applies values and never parses. The commands cross on rtrb's
  wait-free ring; anything heavy they carry rides in a basedrop `Shared`,
  whose last release only queues it, so the audio thread neither allocates
  nor frees.
- **Out, as a snapshot.** The audio thread formats what readers need into
  preallocated text and publishes it through triple_buffer's triple buffer.
  The message thread's timers, the editor and the state calls read that —
  never the engine.

A save must be right even with the host's audio engine off, so an edit that has
not been applied yet is still visible to readers: they are answered from the
latest snapshot replayed through the outstanding edits — each edit once, as it
arrives, however long the engine stays off. `tg_shell.h` and `sc_shell.h` are
the per-product surfaces.

Objects the main thread builds and frees — a Listen-In's bus pusher, a
Spectrogram's receiver — are lent to the audio thread through
`shell_handoff.h`, on basedrop as well: a block holds a counted copy, and the
release function runs only where the main thread collects.

The Trance Gate's also keeps the host's parameters on the right slot. Every
parameter but Slot is stored per slot in the engine (`tg-core`'s `sound.rs`), so
the fifteen host parameters are a window onto the current slot:

- **A host value is pushed when the host moved it** (`tg_shell_push`), into the
  current slot. A value the host has not moved is never re-asserted, so a slot
  switched to keeps its own values while the host still holds the last slot's.
- **On a switch the engine wins.** The block the host's Slot moves -- automation,
  the editor, the host's UI -- writes nothing else; the frame with the new slot is
  published, and the next idle tick moves every host parameter to it
  (`tg_shell_take_params`, `Processor::followEngine`), so automation lanes, the host's
  UI and the editor all show the recalled sound. A paste is the same.
- **A state load is one edit** (`tg_shell_load`): the blob, then the restored
  parameters into the current slot. The parameters are the exact values the
  project saved -- the blob holds them rounded -- so a project reopens bit for
  bit, and a blob from before slots had sounds of their own (state v6 and older)
  takes them in all eight slots.
- **A save writes what the next block will hold** (`tg_shell_save`): the
  engine's blob with the host's values applied the way the next push would apply
  them, so a project saved before any audio has run still has the parameters the
  host shows, in the slot they belong to -- and, between a Slot switch and the
  block that applies it, the new slot's values beside the new slot's blob
  (`tg_shell_next_params`), never the slot that was left.

**Slot files** (`.nitgslot`, one slot; `.nitgbank`, all eight) are the engine's
text, written and strictly read by `tg-core`'s `slotfile.rs` -- the state blob's
own per-slot fields under a format id and a version. NI Trance Gate is on the
JUCE shell, and its native editor shows the system's save or open panel itself
(`juce::FileChooser`, behind `editor/FilePanels.h`), remembering the last
folder in the processor's model; the model turns a slot into text and back
(`EngineModel`: `tg_shell_export`, `tg_shell_import`), and the editor words the
outcome in the hint bar. An import is checked on the message thread and queued
whole (`tg_shell_import`), with the slot the host showed, as a paste is
(below): a refused file changes nothing, and an accepted one is followed by the
host exactly like a paste, and marks the set unsaved.

**Copy and paste** are the editor's too: it reads and writes the system's
clipboard (`juce::SystemClipboard`, behind `editor/Clipboard.h`, so the
editor's tests stand a fake in). Copy is the current slot as a slot file's text
(`tg_shell_export`). Paste hands whatever is there to the engine, which
classifies it whole (`tg-core`'s `paste.rs`, through `tg_shell_paste`): a slot
replaces the slot the host showed when it was pasted -- carried in the command,
because the host may move its Slot in the very block the paste is applied in --
a bank all eight, and a whole state blob, the pre-slot Copy's text and the
Move's patch, everything. Anything else is refused with a reason and changes
nothing; an accepted paste is applied at the top of the next block, the host
follows it as it follows a switch, and the set is marked unsaved
(`ChangeDetails::withNonParameterStateChanged`).

On the Move the module has no host parameters to mirror: the knob grid reads
`get_param`, and the module's editor re-reads the grid (`revalue()`) when the
slot -- carried as the `ui` readout's last field -- changes.

Objects the shell builds and frees on the main thread — the audio thread's half
of Listen-In's bus claim (its `abus_pusher_t`; the main thread keeps the
`abus_writer_t`), the Spectrogram's receiver — reach the audio thread through
`shell_handoff.h`, which frees a replaced one only once the audio thread has let
go of it. The Spectrogram's receiver also owns a worker thread, which runs its
transforms off both the audio and the UI thread; freeing the receiver joins it.
Nothing that allocates, maps memory or talks to the editor runs on the audio thread;
it only records what it wants, and a timer on the message thread does it.

The host's state calls get the same treatment, because the host picks their
thread: a DAW restoring a session may call `setStateInformation` from a thread
of its own, and validators do. A state load therefore never calls a
message-thread API and never touches an editor: it records the load, and the
editor's model catches up on its next tick. NI Spectrogram
records the load in its `Session` (`plugins/spectrogram/State.h`), and its
processor's next service on the message thread hands the receiver whatever
moved; `writeState` reads the same `Session`, so a save straight after a load
writes the load. The receiver's
own C ABI serialises its message side as a floor beneath this, so a caller that
gets it wrong waits its turn instead of deadlocking (`spectro_recv.h`).

Every plugin's state chunk starts with `shell_state.h`'s versioned header. A
chunk without it is an older build's and loads as that build wrote it. A chunk
is read whole before any of it is applied, and one no build could have written
-- empty, a parameter that is not a number of its kind, a string running off
the end -- is refused and changes nothing. The chunk is `ni::nist`
(`plugins/_shared/juce/Nist.h`), and the processor tests save and reload it the
way a host does.

`ni::Processor::processBlock` runs a product's block under
`juce::ScopedNoDenormals`: flush-to-zero for the block, the engines included,
and the host's floating-point mode back on the way out.

## The editor

Every editor is a native JUCE Component built from the Ultraviolet kit
(`plugins/_shared/ui`), drawn to the published Ultraviolet 1.1.0 design system.
An editor reads the processor through its model on the message thread and
never touches the engine; its parameters are host parameters, and what is not a
parameter — the Trance Gate's pattern, the Spectrogram's columns — travels
through the engine's own snapshot. The kit, its components and its tests are on
the **Native UI** page.

## One engine, two shells, and a test that says so

`tg-capi` and `tg-move` are members of the same workspace and both reach
`tg-core` by relative path. They cannot drift apart — not by policy, by
construction. `tg-move` links `tg-capi` for the `tg_core_*` surface its C tests
use, without the crate's default `shell` feature: the plugin's shell and the
ground have no caller on the Move, so its `.so` does not carry them. The
Side-Chain's two do the same.

`tests/render_plugin.c` then proves it after the fact. It sets the identical
patch the module's reference render uses, generates four seconds of a 220 Hz
sine **quantised to int16 before gating** (the Move hands the module an int16
buffer; without the quantisation the test would be measuring rounding rather
than the port), and drives the plugin's own audio path — `tg_core_process_f32_split`
in 128-sample blocks against a DAW-shaped transport at 123 BPM. The result is
hashed and compared to a golden constant.

The two sides hash differently on purpose — FNV-1a over float-split output here,
md5 over int16-interleaved output there — and both goldens are re-recorded
together whenever the sound legitimately moves. The last time it moved, the
cause was tracked down to FMA contraction and *verified*: C with clang's default
gave one hash, C with `-ffp-contract=off` gave another, and Rust agreed with the
second.

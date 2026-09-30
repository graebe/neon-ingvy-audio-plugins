---
title: Architecture
order: 2
slug: architecture
---

Four layers, and each one exists because the layer above it cannot do the job.

```
Rust core        the DSP. No dependencies at all.
  ├── C ABI      extern "C", for the plugin
  └── Schwung    the audio_fx vtable, for the Move
C++ glue         iPlug2 — the VST3/AU/CLAP shell and the host plumbing
Solid editor     a WebView, drawing the Ultraviolet design system
```

## The Rust core

One Cargo workspace at the repository root, and **zero external crates** — every
`[dependencies]` entry in it is a `path` to a sibling inside the same engine.
That is not asceticism; it is what makes the licence audit finish in one
sitting, and it is why both engines compile for an aarch64 Linux device and a
universal macOS bundle without a cross-compilation story.

`[profile.release]` sets `panic = "abort"`, and that one is load-bearing rather
than a size tweak: unwinding out of an `extern "C"` function into a C or C++
host is undefined behaviour. Aborting also removes the landing pads, which
matters for a module that ships to a device.

## The C ABI

`tg-capi` exposes fourteen `extern "C"` symbols — byte for byte the surface the
original C engine exported. That was a deliberate constraint when the DSP was
ported to Rust: keeping the ABI identical meant 1,510 lines of existing C tests
relinked against the new engine rather than being rewritten, so the port was
checked by tests that had never seen Rust.

The headers in `engines/*/include/` are hand-written rather than generated,
which is a real risk — a hand-written header can drift from the Rust side
without either one failing to compile. `tests/spectro_columns.c` exists
specifically to exercise that boundary: argument order, the meaning of `bands`,
and who owns the output buffer.

## The C++ glue

`cmake/RustToolchain.cmake` finds cargo — across every layout rustup.rs,
Homebrew and a bare toolchain use — and the engine files
(`cmake/TranceGateEngine.cmake`, `cmake/SpectroEngine.cmake`) do the same three
steps for each engine:

1. `cargo build --release -p <capi> --target aarch64-apple-darwin --target x86_64-apple-darwin`
2. `lipo -create` the two static-library slices into one universal `.a`
3. expose it as a CMake `INTERFACE` library the plugin target links

The cargo step is wrapped in a target that **always runs**, deliberately: cargo
is the dependency scanner here, not CMake. The `copy_if_different` after `lipo`
is what stops an unchanged engine from relinking three plugin formats.

`iplug_add_plugin(... FORMATS VST3 CLAP AU UI WEBVIEW ...)` produces the
bundles. There is no Standalone target: its `main()` and preferences dialog
reference menu and combo-box resource IDs that only exist for an IGraphics UI,
and this editor is a WebView.

## Threads

A plugin's engine belongs to the **audio thread**, and to nothing else. The
editor's messages, the host's state calls and the idle timer all run elsewhere,
and the engines have no locks in them, by design. So there are exactly two doors,
built once in `engines/shell` and used by every product:

- **In, as a command.** A non-audio thread posts an edit; the audio thread
  applies it at the top of its next block. The queue is bounded, lock-free and
  allocation-free on the audio side.
- **Out, as a snapshot.** The audio thread formats what readers need into
  preallocated text and publishes it through a triple buffer. `OnIdle`, the
  copy button and `SerializeState` read that — never the engine.

A save must be right even with the host's audio engine off, so an edit that has
not been applied yet is still visible to readers: they are answered from the
latest snapshot replayed through the outstanding edits. `tg_shell.h` and
`sc_shell.h` are the per-product surfaces.

Objects the shell builds and frees on the main thread — the audio thread's half
of Listen-In's bus claim (its `abus_pusher_t`; the main thread keeps the
`abus_writer_t`), the Spectrogram's receiver — reach the audio thread through
`shell_handoff.h`, which frees a replaced one only once the audio thread has let
go of it. Nothing
that allocates, maps memory or talks to the editor runs on the audio thread;
`OnParamChange` and `OnReset` only record what they want, and `OnIdle` does it.

Every plugin's state chunk starts with `shell_state.h`'s versioned header. A
chunk without it is an older build's and loads as that build wrote it. A chunk
is read whole before any of it is applied, and one no build could have written
-- empty, a parameter that is not a number of its kind, a string running off
the end -- is refused and changes nothing. The parameter declarations and the
chunk code live outside the plugin class (`Params.cpp`, `Patch.cpp`,
`State.cpp`) so `tests/cpp` can save and reload them the way a host does.

`ProcessBlock` opens with `shell_denormals.h`'s guard: flush-to-zero for the
block, the engines included, and the host's floating-point mode back on the way
out.

## The editor

Solid and Vite, built into `plugins/<product>/resources/web` and globbed into
the bundle as web resources. Everything is inlined into a single `index.html` —
`assetsInlineLimit` is set absurdly high on purpose — because **a WKWebView over
a custom scheme is not a web server**, and a second request would simply not
arrive.

CMake runs vite at configure time *and* at build time. Configure-only shipped
stale bundles, and a stale editor looks exactly like a broken one.

`resources/web` is **build output and is not tracked**. A committed copy was a
second source of truth that could disagree with `ui/src`, and a fresh clone
builds it anyway — so after `npm ci`, configuring is enough. If the editor did
not build, configure stops with an error naming `npm ci`
(`cmake/EditorGuard.cmake`) rather than producing a plugin with a blank window.
The build also writes `assets/ui.js.LICENSE.txt` beside `ui.js`, with the
licence text of every npm package the minified bundle contains.

The editor and the plugin talk over numbered message tags rather than through
parameters, which is what lets the Trance Gate's pattern travel as the engine's
own state blob — the same text the Move module writes.

## One engine, two shells, and a test that says so

`tg-capi` and `tg-move` are members of the same workspace and both reach
`tg-core` by relative path. They cannot drift apart — not by policy, by
construction.

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

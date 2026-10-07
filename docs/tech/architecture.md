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
C++ glue         iPlug2 — the VST3/AU/CLAP shell and the host plumbing
Solid editor     a WebView, drawing the Ultraviolet design system
```

Three decisions are reshaping these layers. Each is recorded with its context
and what it costs:

- [0001](../adr/0001-gpl-3.0-or-later.md): the licence is GPL-3.0-or-later
  rather than MIT.
- [0002](../adr/0002-juce-native-editors.md): JUCE is the shell, and the
  editors are native JUCE Components. They replace iPlug2 and the WebView.
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

## The C++ glue

The build recipe is two files. `cmake/NiRust.cmake` builds the engines through
[Corrosion](https://github.com/corrosion-rs/corrosion), pinned in
`cmake/NiCorrosion.cmake`; `cmake/NiPlugin.cmake` builds the plugins around them.

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
run again, and three plugin formats do not relink.

`ni_add_plugin(<NAME> SOURCES … LINK …)` builds the plugin's editor with vite
(at configure time and at build time), refuses to configure without one
(`cmake/EditorGuard.cmake`), and calls iPlug2's
`iplug_add_plugin(... FORMATS VST3 CLAP AU UI WEBVIEW ...)` with the shared shell
compiled in. There is no Standalone target: its `main()` and preferences dialog
reference resource IDs that only exist for an IGraphics UI.

## The shared shell

Every plugin class derives from `ni::WebPlugin` (`plugins/_shared/ni`), which
owns everything the four used to repeat: the WebView bootstrap (a custom URL
scheme per product, developer tools in debug builds only), the editor protocol,
the animated ground's beat clock, flush-to-zero around every block, and `OnIdle`'s
order — the ground first, then the product's host-facing work, then, only while
an editor is open, its editor work. The iPlug2 hooks it owns are `final`; a
product supplies `ProcessAudio`, `ResetAudio`, `OnHostIdle`, `OnEditorIdle`,
`OnEditorReady` and `OnEditorMessage`, and its state chunk.

The protocol itself (`ni/Editor.h`) has no host in it and is tested without one.
Tags `0..NParams()-1` carry a parameter's display string; each product's own
tags are `64..111`; the shell's are the same in every plugin:

| tag | direction | payload |
|---|---|---|
| 112 `ground` | → editor | `<strength>`, 1 on a downbeat and 0.4 on a beat, three decimals; one message per ring |
| 113 `defaults` | → editor | `<d0>:<d1>:…:<dN-1>`, every parameter's normalised default in index order |
| 114 `groundTick` | → editor | none: the ground's frame clock, every idle tick while the editor reports it moving |
| 120 `ready` | ← editor | none: mounted, send the whole state |
| 121 `setText` | ← editor | `<paramIdx>:<typed text>` |
| 122 `height` | ← editor | the height it needs, in viewport pixels |
| 123 `groundRun` | ← editor | `1` while the ground's field is moving, `0` once at rest or switched off |

Each product's own tags are listed with its editor: the Trance Gate's — and why
its pattern travels as a state blob rather than as parameters — in
[plugins/trance-gate/ui/README.md](../../plugins/trance-gate/ui/README.md), the
Spectrogram's column format and the mount-ordering problem the `ready` tag
solves in [plugins/spectrogram/ui/README.md](../../plugins/spectrogram/ui/README.md).

`tests/editor_tags.test.mjs` holds every tag and parameter index in C++ to the
editors' `msg.js` tables by name. Numbers on the wire are written and read with
`'.'` whatever the host's locale (`ni/Wire.h`).

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
  `OnIdle`, the copy button and `SerializeState` read that — never the engine.

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
  (`tg_shell_take_params`, `SetParamFromPlugin`), so automation lanes, the host's
  UI and the editor all show the recalled sound. A paste is the same.
- **A state load is one edit** (`tg_shell_load`): the blob, then the restored
  parameters into the current slot. The parameters are the exact values the
  project saved -- the blob holds them rounded -- so a project reopens bit for
  bit, and a blob from before slots had sounds of their own (state v6 and older)
  takes them in all eight slots.
- **A save writes what the next block will hold** (`tg_shell_save`): the
  engine's blob with the host's values applied the way the next push would apply
  them, so a project saved before any audio has run still has the parameters the
  host shows, in the slot they belong to.

**Slot files** (`.nitgslot`, one slot; `.nitgbank`, all eight) are the engine's
text, written and strictly read by `tg-core`'s `slotfile.rs` -- the state blob's
own per-slot fields under a format id and a version. The editor asks with a
message (107 export, 108 import); the plugin shows the system's save or open
panel as a sheet on the editor's window (`ni/FileDialog.mm`, which remembers the
last folder per product), moves the bytes (`Patch.cpp`), and answers with the
outcome in words (68), which the hint bar shows. A WKWebView in a plugin has no
download manager, which is why the panels are the plugin's and not the page's.
An import is checked on the main thread and queued whole (`tg_shell_import`),
with the slot the host showed, as a paste is (below): a refused file changes
nothing, and an accepted one is followed by the host exactly like a paste.

**Copy and paste** go the same way, for the same kind of reason: inside a host a
WKWebView may not read the clipboard, and ⌘V never reaches it -- the host's menu
takes it. The editor asks (109 copy, 110 paste); the plugin reads or writes the
system's clipboard on the main thread (`ni/Clipboard.mm`, NSPasteboard, behind
the `ni::Clipboard` interface so `tests/cpp` stands a fake in) and answers on 68.
Copy is the current slot as a slot file's text (`tg_shell_export`). Paste hands
whatever is there to the engine, which classifies it whole (`tg-core`'s
`paste.rs`, through `tg_shell_paste`): a slot replaces the slot the host showed
when it was pasted -- carried in the command, because the host may move its
Slot in the very block the paste is applied in -- a bank all eight, and a whole
state blob, the pre-slot Copy's text and the Move's patch, everything. Anything
else is refused with a reason and changes nothing; an accepted paste is applied
at the top of the next block and the host follows it as it follows a switch.

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
`OnParamChange` and `OnReset` only record what they want, and `OnIdle` does it.

The host's state calls get the same treatment, because the host picks their
thread: auval's stress test calls `SetState` from threads of its own, and so may a
DAW restoring a session. `UnserializeState` therefore never calls a main-thread
API, and neither does what iPlug2 calls after it on that thread: each
parameter's `OnParamChangeUI` and the closing `OnRestoreState` only mark the
editor stale (`editor::Stale` in `ni/Editor.h`), and the next `OnIdle` sends
every value and display string once. The Spectrogram's records the load in its `Session` (`plugins/spectrogram/State.h`)
and the next `OnIdle` hands the receiver whatever moved; `SerializeState` reads the
same `Session`, so a save straight after a load writes the load. The receiver's
own C ABI serialises its message side as a floor beneath this, so a caller that
gets it wrong waits its turn instead of deadlocking (`spectro_recv.h`).

Every plugin's state chunk starts with `shell_state.h`'s versioned header. A
chunk without it is an older build's and loads as that build wrote it. A chunk
is read whole before any of it is applied, and one no build could have written
-- empty, a parameter that is not a number of its kind, a string running off
the end -- is refused and changes nothing. The parameter declarations and the
chunk code live outside the plugin class (`Params.cpp`, `Patch.cpp`,
`State.cpp`) so `tests/cpp` can save and reload them the way a host does.

`ni::WebPlugin::ProcessBlock` opens with `shell_denormals.h`'s guard:
flush-to-zero for the block, the engines included, and the host's
floating-point mode back on the way out.

## The editor

Solid and Vite, built into `plugins/<product>/resources/web` and globbed into
the bundle as web resources: `index.html`, one `assets/ui.js` with every module,
one `assets/style.css` with every stylesheet and the kit's font inlined as a data
URI (`assetsInlineLimit` is set absurdly high on purpose), the licence notices,
and `fonts/OFL.txt`. Fixed names, never hashed: **a WKWebView over a custom
scheme is not a web server**, and it serves exactly the files that are there.

Every editor is drawn in the kit's `EditorFrame` (the ground, the Hint bar, the
fit-to-viewport scale and the height it reports, and a window that cannot
scroll: the body and the frame's `<main>` are `overflow: clip`, which is not a
scroll container, so focusing a control past an edge moves nothing), reads
its host parameters from one store (`@ultraviolet/ui/params`, defaults from the shell's message
113), and speaks to the plugin through `useEditorBridge`. Binary payloads -- the
plot captures, the rendered curves, the spectrogram's columns -- are an ASCII
header and raw bytes, which the bridge decodes from base64 once (`onBytes`).

**What a control does** is declared on it once, as `data-info` (the kit's
controls take an `info` prop; anything else spreads `infoAttrs(text)`), and is
also its `aria-description`. The frame listens on its `<main>` and lays the
string of the control under the pointer, or of the one with visible keyboard
focus, over the Hint bar's clauses: at once on entering, 150 ms after leaving,
so moving along a row never flashes the clauses between two strings. An
action's outcome (tag 68 in the Trance Gate) outranks it while shown, and the
pointer outranks the focus. The clauses stay laid out underneath, hidden, so
the bar's width and the Motion switch never move (`ui-kit/src/lib/info.js`).
The Trance Gate's strings are all in `plugins/trance-gate/ui/src/lib/info.js`.

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

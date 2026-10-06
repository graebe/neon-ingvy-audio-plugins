---
title: Architecture
order: 2
slug: architecture
---

Four layers, and each one exists because the layer above it cannot do the job.

```
Rust core        the DSP, on the shared ni-dsp. No external dependencies.
  ├── C ABI      extern "C", for the plugin
  └── Schwung    the audio_fx vtable (ni-schwung), for the Move
C++ glue         iPlug2 — the VST3/AU/CLAP shell and the host plumbing
Solid editor     a WebView, drawing the Ultraviolet design system
```

## The Rust core

One Cargo workspace at the repository root, and **zero external crates** — every
`[dependencies]` entry in it is a `path` to a sibling inside the same engine
or to one of the product-free shared crates (`engines/shared`, `ground`,
`shell`, `audio-bus`). Those are rlibs: each product's single static library
absorbs them, so a plugin still links exactly one archive.
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

`cmake/NiPlugin.cmake` holds the whole build recipe, in two functions.

`ni_add_rust_engine(<target> CRATE <crate> LIB <lib> INCLUDE <dir>)` registers
a product's C ABI, and `ni_build_rust_engines()` builds every registered crate
in **one** cargo invocation with one target directory, so the crates they share
(`ni-dsp`, `ground`, `shell`, `audio-bus`) compile once:

1. `cargo build --release -p tg-capi -p sc-capi … --target aarch64-apple-darwin --target x86_64-apple-darwin`
2. `lipo -create` each product's two static-library slices into one universal `.a`
3. each is exposed as a CMake `INTERFACE` library its plugin and tests link

It is still **one static library per plugin**: each product's capi crate is a
staticlib that absorbs the rlibs it depends on, because two Rust staticlibs in
one binary each carry the Rust runtime. The cargo step always runs — cargo is the
dependency scanner, not CMake — and `copy_if_different` after `lipo` is what
stops an unchanged engine from relinking three plugin formats.

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
  applies it at the top of its next block. The queue is bounded, lock-free and
  allocation-free on the audio side.
- **Out, as a snapshot.** The audio thread formats what readers need into
  preallocated text and publishes it through a triple buffer. `OnIdle`, the
  copy button and `SerializeState` read that — never the engine.

A save must be right even with the host's audio engine off, so an edit that has
not been applied yet is still visible to readers: they are answered from the
latest snapshot replayed through the outstanding edits. `tg_shell.h` and
`sc_shell.h` are the per-product surfaces.

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

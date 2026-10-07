---
title: Repo Structure
order: 1
slug: structure
---

A monorepo: everything that ships from here is in here. There is one submodule
(iPlug2) and the engines are subtrees, not submodules — so a clone plus
`git submodule update --init --recursive` is the whole checkout.

```
engines/<product>/crates     the core, and its wrappers
engines/shared/crates        ni-dsp, ni-schwung, ni-testkit — shared, product-free
plugins/<product>/           the VST3/AU/CLAP shell, and its editor
plugins/_shared/ni/          ni::WebPlugin, the editor protocol, ni::wire
modules/<product>/           the Schwung module's shell and packaging
ui-kit/                      @ultraviolet/ui — tokens, controls, the iPlug2 bridge
site/                        this documentation site
design/scheme/               the Ultraviolet design system, vendored
design/designs/              the "NI Plugin Layouts" canvas, mirrored
tests/                       the cross-cutting suite
cmake/                       the Rust toolchain resolver, NiRust.cmake (Corrosion) and NiPlugin.cmake
versions.json                one version per product
```

## A crate belongs to exactly one product

The *product* engines never depend on each other. `engines/trance-gate/crates`
holds `tg-core` and its two wrappers; `engines/spectro/crates` holds
`spectro-core` and its one. They are all members of a single Cargo workspace
rooted at the repository, which is what lets `.cargo/config.toml` apply one set
of target flags to all of them.

`engines/audio-bus` is the exception that names the rule, and it is worth being
explicit about rather than letting it look like drift. It is **not a product
engine** — it is the house transport, the Rust counterpart of `ui-kit`, and it
exists precisely so that two products can share one thing: a Listen-In publishes
audio into a shared-memory bus and a Spectrogram reads it out. Any product may
depend on it; **it depends on no product in return**, which is the direction that
actually matters. `spectro-core` knowing about `tg-core` would still be a
coupling nobody asked for.

`engines/ground`, `engines/shell` and `engines/shared` follow the same rule.
`engines/shared/crates` holds what two products would otherwise each keep a
copy of: `ni-dsp` (C-compatible formatting and parsing, the rate parser, the
envelope curves, the one-pole and the glide, the transport-following phase,
the capi C helpers), `ni-schwung` (the Schwung audio_fx v2 glue every `*-move`
crate is built on) and `ni-testkit` (the counting allocator the `no_alloc`
tests install — a dev-dependency only). Each names no product. A piece moves
there once it is truly identical in two products; what differs stays in the
product, as each product's rate list does.

## One shell for every plugin

`plugins/_shared/` is to the plugins what `modules/_shared/` is to the Schwung
modules: what every product's shell would otherwise repeat. `ni::WebPlugin` is
the iPlug2 class all four derive from; `ni/Editor.h` is the editor protocol with
no host in it; `ni/Wire.h` the buffer and message helpers (chunking, the float
round trip, the passthrough, the host's transport, locale-free numbers) and
`ni/Scope.h` the capture a plot draws. None of it knows a product. It is
compiled into each plugin's format targets — `iplug::Plugin` is a different
class under each API — so it is sources, not a library, and `cmake/NiPlugin.cmake`
adds it. `tests/cpp` links the host-free half.

A plugin's `CMakeLists.txt` is one `ni_add_plugin` call naming its sources and
its engine.

## One version per product

`versions.json` is where a version is decided, and it is the only place. A
product's version otherwise appears in three or four files in three languages —
`config.h` as a string *and* as packed hex, `module.json`, and every crate of
its engine — and nothing held them together until they had already come apart.

`ctest -R versions` asserts the agreement rather than generating it. That is
this repository's habit throughout: the curve and envelope oracles pin the UI
against the engine's own measured output rather than deriving one from the
other, and `tokens.css` is tested against the vendored design system rather than
emitted from it. A generator hides a disagreement by overwriting it; a test
names it.

## Where the documentation lives

Each product's manual is the `README.md` in its own `plugins/<product>/`
directory, with the two interface documents beside it in `docs/`. This site
renders those files directly — there is no second copy. Browsing the repository
on GitHub and reading the site show the same text, because they are the same
text.

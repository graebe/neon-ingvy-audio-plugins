---
title: Repo Structure
order: 1
slug: structure
---

A monorepo: everything that ships from here is in here. There is one
submodule, JUCE 9.0.3 at `external/JUCE`, and the engines are subtrees, not
submodules — so a clone plus `git submodule update --init --recursive` is the
whole checkout.

```
engines/<product>/crates     the core, and its wrappers
engines/shared/crates        ni-dsp, ni-schwung, music-core — shared, product-free
plugins/<product>/           the VST3 on the JUCE shell
plugins/<product>/editor/    the product's native editor, on the kit
plugins/_shared/juce/        ni::Processor, ni::PluginEditor, the state codec
plugins/_shared/ni/          host-free helpers: ni::wire, the scope capture
plugins/_shared/ui/          the native Ultraviolet kit (JUCE), and its gallery
modules/<product>/           the Schwung module's shell and packaging
site/                        this documentation site
design/scheme/               the Ultraviolet design system, vendored
design/designs/              the "NI Plugin Layouts" canvas, mirrored
tests/                       the cross-cutting suite; tests/ui the native UI's
tests/fixtures/iplug2/       what the earlier VST3 builds saved: the compatibility contract
cmake/                       the Rust toolchain resolver, NiRust.cmake (Corrosion) and NiJucePlugin.cmake
tools/docker/, tools/cross/  the cross-build kit: Linux and Windows build images,
                             the Windows toolchain, the JUCE smoke plugin
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
engine** — it is the house transport, shared as the native kit is shared, and it
exists precisely so that two products can share one thing: a Listen-In publishes
audio into a shared-memory bus and a Spectrogram reads it out. Any product may
depend on it; **it depends on no product in return**, which is the direction that
actually matters. `spectro-core` knowing about `tg-core` would still be a
coupling nobody asked for.

`engines/ground`, `engines/shell` and `engines/shared` follow the same rule.
`engines/shared/crates` holds what two products would otherwise each keep a
copy of: `ni-dsp` (C-compatible formatting and parsing, the rate parser, the
envelope curves, the one-pole and the glide, the transport-following phase,
the capi C helpers) and `ni-schwung` (the Schwung audio_fx v2 glue every
`*-move` crate is built on). Each names no product. A piece moves
there once it is truly identical in two products; what differs stays in the
product, as each product's rate list does.

## One shell for every plugin

`plugins/_shared/` is to the plugins what `modules/_shared/` is to the Schwung
modules: what every product's shell would otherwise repeat. `ni::Processor`
(`juce/Processor.h`) is the JUCE processor all five derive from, and
`ni::PluginEditor` the window a host opens; `juce/Nist.h` is the state codec
and `juce/ParamSpec.h` the parameter table. `ni/Wire.h` holds the buffer and
number helpers (the float round trip, the passthrough, locale-free numbers) and
`ni/Scope.h` the capture a plot draws. None of it knows a product, and
`cmake/NiJucePlugin.cmake` compiles it into every plugin.

A plugin's `CMakeLists.txt` is one `ni_add_juce_plugin` call naming its sources,
its engine and its editor.

## One version per product

`versions.json` is where a version is decided, and it is the only place. A
product's version otherwise appears in several files in three languages — the
bundle's `Info.plist` and `moduleinfo.json`, `module.json`, and every crate of
its engine — and nothing held them together until they had already come apart.

`ctest -R versions` asserts the agreement rather than generating it. That is
this repository's habit throughout: the curve and envelope oracles pin the
engine to its own measured output, and the kit's tokens are tested against the
vendored design system. A generator hides a disagreement by overwriting it; a test
names it.

## Where the documentation lives

Each product's manual is the `README.md` in its own `plugins/<product>/`
directory, with the two interface documents beside it in `docs/`. This site
renders those files directly — there is no second copy. Browsing the repository
on GitHub and reading the site show the same text, because they are the same
text.

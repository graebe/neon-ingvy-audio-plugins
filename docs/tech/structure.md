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
plugins/<product>/           the VST3/AU/CLAP shell, and its editor
modules/<product>/           the Schwung module's shell and packaging
ui-kit/                      @ultraviolet/ui — tokens, controls, the iPlug2 bridge
site/                        this documentation site
design/files/                the Ultraviolet design system, vendored
tests/                       the cross-cutting suite
cmake/                       the Rust toolchain resolver and the engine targets
versions.json                one version per product
```

## A crate belongs to exactly one product

The two engines never depend on each other. `engines/trance-gate/crates` holds
`tg-core` and its two wrappers; `engines/spectro/crates` holds `spectro-core`
and its one. They are all members of a single Cargo workspace rooted at the
repository, which is what lets `.cargo/config.toml` apply one set of target
flags to all of them.

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

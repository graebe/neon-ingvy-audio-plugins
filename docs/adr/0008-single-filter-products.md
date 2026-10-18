# 0008. One Filter Designer node, nine products

- Status: accepted, 2026-10-18 (the owner asked for a plugin per Filter
  Designer filter, with the full node, and the Move modules beside them)
- Decided by: Torben Gräber
- For: NI High Pass, NI Low Pass, NI Band Pass, NI Notch, NI Bell, NI Low
  Shelf, NI High Shelf, NI Tilt, NI All Pass

## Context

Dropping one filter into a chain should not need NI Filter Designer's
sixteen nodes and 175 parameters. The owner asked for one plugin per filter
type, each the whole node: Freq, Slope, Q, Family, Ripple, Stopband, Gain,
Matched, Channel, Output, and the analyzer.

The house rules shape the answer: a product engine may not depend on
another (Cargo.toml), one `plugins/<product>` is one bundle and one version
(`bundleOf`, `NI_PRODUCT`, versions.json), and one Move module builds one
crate (`cargo build -p $MODULE_CRATE`).

## Decision

**The node is shared.** fd-core's node (its design, glides, morphs,
crossfades, budget) and the analyzer moved into two product-free crates,
`ni-filter-node` and `ni-analyzer`. A node reads a `Target`, never an
engine's table. Filter Designer is unchanged to the bit; a single filter is
bit-identical to an FD node of its type under the same automation.

**One engine, nine products.** `engines/single-filter` (sf-core, sf-capi,
sf-move) is to the nine what audio-bus is to Listen-In and the Spectrogram:
shared, and naming no other product. The engine is made with its shape; each
shape has a frozen, append-only parameter layout of only what it uses, its
host index the position there.

**One shell, nine bundles.** `plugins/_shared/single-filter` holds the
processor and the editor; each `plugins/<product>` calls
`ni_add_juce_plugin(... SHARED_SHELL ni_single_filter)` with one line naming
its shape. Each product keeps its own bundle, code, version key and tag, so
the release tooling stays one-bundle-per-product; the nine share their
engine's version (tests/versions.test.mjs).

**Family tests.** `ni_product_family(single-filter ...)` (cmake/NiTest.cmake)
labels a shared test with every member, so `scripts/test.sh product
high-pass` runs the engine and the shell the nine share, and each bundle's
host test is its own product's.

**A one-line crate per Move module.** `sf-move` is a generic module; each
`sf-move-<product>` exports `SingleFilter<Product>` as its one
`move_audio_fx_init_v2`. Cargo's own model of one cdylib per package: no
mutually exclusive features, no module that guesses its shape from its
directory, and package.sh unchanged.

**A kit component.** The response well is the kit's `FrequencyResponse`,
product-free; Filter Designer's own well can move onto it later.

## Consequences

- Nine more bundles in Live's browser and nine more modules on the Move;
  each small, all built from one engine.
- A fix to the node reaches Filter Designer and the nine at once, and the
  golden renders of both say so.
- The words a value is printed in (`ni_filter_node::words`) are shared by
  Filter Designer and the nine, so a value reads the same everywhere.

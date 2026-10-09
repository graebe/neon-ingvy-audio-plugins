# Subtractive Synthesis in Practice

A survey of the methods behind Vital/Serum-class subtractive synthesizers,
with tested Rust implementations.

| File | What it is |
|---|---|
| [paper.md](paper.md) | The paper. Markdown with LaTeX math; renders on GitHub and in the VS Code preview |
| [references.md](references.md) | The bibliography, with notes on how each entry was verified |
| [figures/](figures/) | Figures 1–4 (SVG, light and dark) and `results.md`, the measured tables |
| [code/](code/) | `ni-paper-subsynth`, the crate every listing in the paper comes from |
| [tex/](tex/) | The conference edition: `paper.pdf`, two-column (IEEEtran), generated from `paper.md` |

## Running the code

From the repository root:

```sh
cargo test  -p ni-paper-subsynth                                 # unit, snippet and no-allocation tests
cargo run   -p ni-paper-subsynth --release --example figures     # regenerate figures/ (about 10 s)
cargo bench -p ni-paper-subsynth                                 # the cost table, section 4.6
```

`cargo test` includes `tests/snippets.rs`, which fails when a Rust listing in
`paper.md` differs from the region of the crate it names.

## The conference edition

`tex/paper.tex` is generated from `paper.md` by `tex/md2tex.py`, which changes
the layout and nothing else. Never edit it by hand: edit the Markdown and run

```sh
papers/subtractive-synthesis/tex/build.sh    # paper.tex, figures/*.pdf, paper.pdf
```

The build needs `rsvg-convert` (librsvg) for the figures and Tectonic
(`brew install tectonic`) for the PDF.

## Editing the paper

To change a listing, change the code and copy its anchored region into the
paper. The test will tell you if you missed one.

The figures and `figures/results.md` are generated. After changing anything
the measurements depend on, run the `figures` example, then copy any changed
numbers into the paper's text and Tables 1–3.

## Licence

Text © 2026 Torben Gräber. Code GPL-3.0-or-later, like the rest of the
repository.

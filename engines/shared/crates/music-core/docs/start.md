---
title: Start here
order: 0
slug: start
---

`music-core` is a small Rust library for the things music theory is built out
of: the twelve pitches, the distances between them, notes that know which octave
they are in, and chords.

It has no dependencies, works without the standard library, and never allocates.
Every type is `Copy` and small enough to pass around without thinking about it.

```rust
use music_core::Pitch;

let c = Pitch::C;
let e = Pitch::E;
let g = Pitch::G;
```

## Two Levels

A `Pitch` is a note name with the octave thrown away, so every C is the same C.
A `Note` is that same pitch given a register.

```rust
use music_core::Interval;
use music_core::Pitch;

let pitch = Pitch::C;
let note = Pitch::C.at(4);

let wraps = pitch + Interval::OCTAVE;       // C, unchanged
let climbs = (note + Interval::OCTAVE).midi(); // 72, an octave higher
```

A pitch wraps around like a clock, because the octave is exactly what it throws
away. A note keeps climbing. Knowing which one you want is most of the library.

## Adding It to a Project

```toml title="Cargo.toml"
[dependencies]
music-core = { git = "https://codeberg.org/graebe/neo-riemann" }
```

Everything is exported from the crate root, so one `use` line is enough.

```rust
use music_core::{Chord, Interval, Note, Pitch, PitchSet};
```

## Where to Start

| Page | Covers |
|------|--------|
| [The twelve pitches](#pitches) | `Pitch`, `Interval`, arithmetic, measuring gaps |
| [Giving a pitch a register](#notes) | `Note`, MIDI numbers, frequency |
| [Sets of pitches](#pitch-sets) | `PitchSet`, set algebra |
| [Chords](#chords) | `Chord`, building, voicing |
| [Identifying a pitch set](#identification) | Working out the chord from the notes |
| [Keys and modes](#keys) | `Key`, `Mode`, signatures, spelling, roman numerals |

The three examples this tutorial follows are worth running as you read.

```sh
cargo run -p music-core --example pitch
cargo run -p music-core --example note
cargo run -p music-core --example chord
```

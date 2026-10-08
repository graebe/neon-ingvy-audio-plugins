---
title: Where to go next
order: 7
slug: neo-riemann
---

You have seen the whole of `music-core`: pitches, intervals, notes, sets of
pitches, chords, and keys.

## The Transformations

`neo-riemann` is the crate built on top of this one. It adds consonant triads —
major and minor only — and the PLR transformation group: the moves that change
one note by one step and hold the other two still.

| Move | Name | C major becomes |
|------|------|-----------------|
| `P` | parallel | C minor |
| `L` | leading-tone exchange | E minor |
| `R` | relative | A minor |

```rust
use neo_riemann::prelude::*;
use neo_riemann::Pitch;
use neo_riemann::Triad;

let c = Triad::major(Pitch::C);

let parallel = c.p().to_string();  // "Cm"
let leading = c.l().to_string();   // "Em"
let relative = c.r().to_string();  // "Am"
let shared = c.common_tones(c.p()); // 2
```

It re-exports everything from `music-core`, so depending on it alone is enough.

A `Triad` is the narrower type, and the two convert both ways.

```rust
use neo_riemann::Chord;
use neo_riemann::Pitch;
use neo_riemann::Triad;

let triad = Triad::major(Pitch::C);

let as_chord = Chord::from(triad);                  // C
let back = Triad::try_from(Chord::major(Pitch::C)); // Ok
let refused = Triad::try_from(Chord::dom7(Pitch::G)); // Err, not a triad
```

So a triad is always a chord, but only some chords are triads.

## What Is Not Here Yet

Analysis passes have not arrived in `music-core`, nor file or MIDI input and
output, nor key detection. Seventh-chord transformations are missing from
`neo-riemann`.

Anything that is not specifically neo-Riemannian belongs in `music-core`. The
split exists so a project wanting a `Pitch` does not have to take a
transformation group with it.

## Reference

`music-core` lives in this repository, at
[engines/shared/crates/music-core](https://github.com/graebe/neon-ingvy-audio-plugins/tree/main/engines/shared/crates/music-core),
with its test suite and the three examples this tutorial follows. NI
Chord-Detector names chords with it. `neo-riemann` is at
[codeberg.org/graebe/neo-riemann](https://codeberg.org/graebe/neo-riemann) and
depends on this repository for `music-core`.

---
title: Sets of pitches
order: 3
slug: pitch-sets
---

A `PitchSet` holds any group of pitches, like a handful of piano keys held down
at once. It has no order and no repeats.

```rust
use music_core::Pitch;
use music_core::PitchSet;

let c_major = PitchSet::from_pitches(&[Pitch::C, Pitch::E, Pitch::G]);

let size = c_major.len();              // 3
let has_e = c_major.contains(Pitch::E); // true
let printed = c_major.to_string();      // "{C, E, G}"
```

The whole set is twelve bits, one per pitch, so it takes two bytes whether it
holds one pitch or all twelve. C sits at bit 0.

```rust
use music_core::Pitch;
use music_core::PitchSet;

let c_major = PitchSet::from_pitches(&[Pitch::C, Pitch::E, Pitch::G]);

let bits = c_major.bits();       // 0b1001_0001
let empty = PitchSet::EMPTY;     // {}
let all = PitchSet::CHROMATIC;   // all twelve
```

## Adding and Removing

`insert` and `remove` return a new set rather than changing the one you have.
Every type here is `Copy`, so building a changed version costs nothing.

```rust
use music_core::Pitch;
use music_core::PitchSet;

let c_major = PitchSet::from_pitches(&[Pitch::C, Pitch::E, Pitch::G]);

let with_seventh = c_major.insert(Pitch::B); // {C, E, G, B}
let without_third = c_major.remove(Pitch::E); // {C, G}

let still_three = c_major.len();             // 3, the original is unchanged
let twice = c_major.insert(Pitch::E);        // {C, E, G}, no repeats
let absent = c_major.remove(Pitch::D);       // {C, E, G}, D was never there
```

## Set Algebra

The usual operators work, and the questions they answer are musical ones.

```rust
use music_core::Pitch;
use music_core::PitchSet;

let c_major = PitchSet::from_pitches(&[Pitch::C, Pitch::E, Pitch::G]);
let a_minor = PitchSet::from_pitches(&[Pitch::A, Pitch::C, Pitch::E]);

let shared = c_major & a_minor;                 // {C, E}
let only_in_c = c_major - a_minor;              // {G}
let together = c_major | a_minor;               // {C, E, G, A}
let count = c_major.common_tones(a_minor);      // 2
```

| Operator | Method | Asks |
|----------|--------|------|
| `&` | `intersection` | What do these two chords share? |
| `\|` | `union` | What notes do they use between them? |
| `-` | `difference` | What does the first have that the second lacks? |
| `!` | `complement` | Everything else in the octave. |

C major and A minor share two of their three notes, which is why the move
between them sounds smooth.

## Iterating

A set yields its pitches ascending from C, never in the order you inserted them.

```rust
use music_core::Pitch;
use music_core::PitchSet;

let spread = PitchSet::from_pitches(&[Pitch::G, Pitch::C, Pitch::E]);

for pitch in spread {
    println!("{}", pitch); // C, then E, then G
}
```

## Moving and Mirroring

Transposing rotates every bit at once. Inverting reflects the whole set.

```rust
use music_core::Interval;
use music_core::Pitch;
use music_core::PitchSet;

let c_major = PitchSet::from_pitches(&[Pitch::C, Pitch::E, Pitch::G]);

let up_a_fifth = c_major.transpose(Interval::PERFECT_FIFTH); // {D, G, B}
let mirrored = c_major.invert(0);                            // {C, F, G#}
```

## The Shape of a Chord

The interval vector counts how many pairs sit at each distance, 1 through 6. It
ignores transposition and inversion, so it describes the shape of a chord rather
than its position.

```rust
use music_core::Pitch;
use music_core::PitchSet;

let c_major = PitchSet::from_pitches(&[Pitch::C, Pitch::E, Pitch::G]);
let a_minor = PitchSet::from_pitches(&[Pitch::A, Pitch::C, Pitch::E]);

let major = c_major.interval_vector(); // [0, 0, 1, 1, 1, 0]
let minor = a_minor.interval_vector(); // [0, 0, 1, 1, 1, 0]
```

Both give the same answer: one minor third, one major third, one fourth. Major
and minor are mirror images of each other.

See the [Chords tutorial](#chords) for what happens when you give a set a root.

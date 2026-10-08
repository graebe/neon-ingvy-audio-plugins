---
title: The twelve pitches
order: 1
slug: pitches
---

A `Pitch` is a note name without the octave. E.g. every C is the same C. Let's
initialize some examples.

```rust
use music_core::Pitch;

let c = Pitch::C;
let f_sharp = Pitch::F_SHARP;
```

We can also initialize pitches by number, using `Pitch::new`.

```rust
use music_core::Pitch;

let c = Pitch::new(0);
let f_sharp = Pitch::new(6);
```

Or by string parsing.

```rust
use music_core::Pitch;

let c = "C".parse::<Pitch>().unwrap();
let f_sharp = "F#".parse::<Pitch>().unwrap();
let d_flat = "Db".parse::<Pitch>().unwrap();
```

| Index  | 0 | 1  | 2 | 3  | 4 | 5 | 6  | 7 | 8  | 9 | 10 | 11 |
|--------|---|----|---|----|---|---|----|---|----|---|----|----|
| Sharp  | C | C# | D | D# | E | F | F# | G | G# | A | A# | B  |
| Flat   | C | Db | D | Eb | E | F | Gb | G | Ab | A | Bb | B  |

Both rows are the same twelve values. Printing always uses sharps, so ask for
flats by name when they read better.

```rust
use music_core::Pitch;
use music_core::Spelling;

let e_flat = Pitch::E_FLAT;

println!("{}", e_flat);                      // D#
println!("{}", e_flat.name(Spelling::Flats)); // Eb
```

Overflow handling is treated by modulo twelve.

```rust
use music_core::Pitch;

let b = Pitch::new(-1); // modulo 12 yields 11 (B)
let c_sharp = Pitch::new(13); // modulo 12 yields 1 (C#)
```

## Pitch Arithmetic

We can manipulate pitches arithmetically, adding or subtracting intervals to move around the twelve-tone circle.

```rust
use music_core::Pitch;
use music_core::Interval;

let c = Pitch::C;
let d = c + Interval::MAJOR_SECOND;
let b = c - Interval::MINOR_SECOND;
```

| Semitones | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 |
|-----------|---|---|---|---|---|---|---|---|---|----|----|----|----|
| Constant  | `UNISON` | `MINOR_SECOND` | `MAJOR_SECOND` | `MINOR_THIRD` | `MAJOR_THIRD` | `PERFECT_FOURTH` | `TRITONE` | `PERFECT_FIFTH` | `MINOR_SIXTH` | `MAJOR_SIXTH` | `MINOR_SEVENTH` | `MAJOR_SEVENTH` | `OCTAVE` |

The pitch wraps, just like for instantiation. The interval itself does not: it
is a plain semitone count, so it can be negative or larger than an octave.

```rust
use music_core::Interval;

let down_a_semitone = Interval::new(-1);
let two_octaves_and_a_fifth = Interval::new(31);
```

## Measuring the Gap

There are three ways to ask how far apart two pitches are, and they give
different answers on purpose.

```rust
use music_core::Pitch;

let c = Pitch::C;
let g = Pitch::G;

let up = c.interval_to(g).semitones();   // 7, going up from C to G
let back = g.interval_to(c).semitones(); // 5, going up from G to C
let shortest = c.distance_to(g).value(); // 5, in either direction
let signed = (g - c).semitones();        // -5, the shortest path with a sign
```

| Method | Answer | Range |
|--------|--------|-------|
| `interval_to` | How far up? Direction matters, so it is not symmetric. | 0 to 11 |
| `distance_to` | How far must a voice move? Direction is ignored. | 0 to 6 |
| `a - b` | The shortest path, with a direction attached. | -5 to 6 |

The tritone is the one exception. It sits exactly half an octave away, so both
directions are equally short and there is nothing to be negative about. The tie
resolves upward and both answers come out `+6`.

```rust
use music_core::Pitch;

let up = (Pitch::F_SHARP - Pitch::C).semitones(); // 6
let down = (Pitch::C - Pitch::F_SHARP).semitones(); // 6
```

An `IntervalClass` is the folded form used by `distance_to`: direction and
octaves are both discarded, so a fifth up and a fourth down are the same class.

```rust
use music_core::IntervalClass;

let fifth_up = IntervalClass::new(7).value();   // 5
let fourth_down = IntervalClass::new(-5).value(); // 5
```

## Comparing Pitches

Two pitches are equal when they are the same note, whatever you called it when
you built them.

```rust
use music_core::Pitch;

let c = Pitch::C;
let also_c = Pitch::new(0);

let same = c == also_c;                            // true
let enharmonic = Pitch::C_SHARP == Pitch::D_FLAT;  // true
let different = c == Pitch::D;                     // false
```

## Voicing a Pitch Gives a Note

A `Pitch` by itself does not have an octave. To get a full `Note`, you need to add an octave.

```rust
use music_core::Pitch;
use music_core::Note;

let c = Pitch::C;
let c_in_fourth_octave = c.at(4);
```

See the [Notes tutorial](#notes) for more information on working with `Note`s.


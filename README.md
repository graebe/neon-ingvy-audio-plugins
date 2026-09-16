# music-core

General music theory primitives in Rust: pitch classes, intervals, notes,
chords and pitch-class sets.

No dependencies, `no_std` by default, and nothing here allocates. Every type is
`Copy` and small enough to pass around without thinking about it.

This is the layer underneath [`neo-riemann`](../neo-riemann), which adds
consonant triads and the PLR transformation group. Nothing here knows about
that theory, which is the point of the separation.

## Two levels

A **`Pitch`** is a note name with the octave thrown away. Every C is the same C.
There are exactly twelve, numbered 0 to 11 upward from C, and one fits in a
single byte.

A **`Note`** is that same pitch given a register. Middle C is C in octave 4,
MIDI 60.

The two are one idea rather than two. `Note` is a type alias for
`Voiced<Pitch>`, so giving something a register is a single act that applies at
either level.

```rust
use music_core::{Interval, Note, Pitch};

let middle_c: Note = Pitch::C.at(4);

assert_eq!(middle_c.pitch(), Pitch::C);
assert_eq!(middle_c.octave(), 4);
assert_eq!(middle_c.midi(), 60);

// A pitch wraps at the octave, because an octave is what it discards.
assert_eq!(Pitch::C.transpose(Interval::OCTAVE), Pitch::C);

// A note climbs, because it has a register to climb within.
assert_eq!((middle_c + Interval::OCTAVE).midi(), 72);
```

## Enharmonics

Equal temperament is assumed, so C sharp and D flat are one value with two
names. Spelling is a display choice made when you print.

```rust
use music_core::{Pitch, Spelling};

assert_eq!(Pitch::C_SHARP, Pitch::D_FLAT);
assert_eq!(Pitch::E_FLAT.name(Spelling::Sharps), "D#");
assert_eq!(Pitch::E_FLAT.name(Spelling::Flats), "Eb");
```

## Arithmetic

Addition and subtraction take an `Interval`, never a bare number, the same way
`Instant` takes a `Duration`. A pitch is a point and an interval is a
displacement, and keeping them apart stops you adding two pitches together.

```rust
use music_core::{Interval, Pitch};

assert_eq!(Pitch::C + Interval::MINOR_SECOND, Pitch::C_SHARP);
assert_eq!(Pitch::G - Interval::PERFECT_FIFTH, Pitch::C);
```

Two different questions can be asked about the gap between two pitches.

```rust
use music_core::Pitch;

// Directed: how far up? Asymmetric.
assert_eq!(Pitch::C.interval_to(Pitch::G).semitones(), 7);
assert_eq!(Pitch::G.interval_to(Pitch::C).semitones(), 5);

// Undirected: how far must a voice move? Symmetric, folds at the tritone.
assert_eq!(Pitch::C.distance_to(Pitch::G).value(), 5);

// Subtraction is the signed form of that second one.
assert_eq!((Pitch::G - Pitch::C).semitones(), -5);
assert_eq!((Pitch::C - Pitch::G).semitones(), 5);
```

## No edges to fall off

A pitch is modular by definition, so any interval wraps into 0 through 11 in
either direction, at any magnitude. The test suite checks all 65,536 of them.

```rust
use music_core::{Interval, Pitch};

assert_eq!(Pitch::new(13), Pitch::C_SHARP);
assert_eq!(Pitch::new(-1), Pitch::B);
assert_eq!(Pitch::C.transpose(Interval::new(i16::MIN)), Pitch::E);
```

`new` never fails anywhere in this crate. There is no invalid input to a pitch
class: twelve simply is C. A note round-trips for every MIDI number a 16-bit
integer can hold.

```rust
use music_core::Note;

assert_eq!(Note::from_midi(5000).midi(), 5000);
assert_eq!(Note::from_midi(i16::MIN).midi(), i16::MIN);
```

## Chords and sets

```rust
use music_core::{Chord, Harmony, Pitch, PitchSet};

let dm7 = Chord::min7(Pitch::D);
assert_eq!(dm7.to_string(), "Dm7");
assert_eq!(dm7.pitch_set().len(), 4);

// A pitch set is a 12-bit mask, so set operations are single instructions.
let major = PitchSet::from_pitches(&[Pitch::C, Pitch::E, Pitch::G]);
let minor = PitchSet::from_pitches(&[Pitch::A, Pitch::C, Pitch::E]);
assert_eq!((major & minor).len(), 2);
```

There is one constructor per chord quality, so nothing has to reach for the
parser: `major`, `minor`, `dim`, `aug`, `sus2`, `sus4`, `maj7`, `dom7`, `min7`,
`min_maj7`, `half_dim7` and `dim7`. `FromStr` exists for real user input.

## Frequency

Only a note has a frequency, because only a note has a register. This is the
one part behind the `std` feature, since the maths needs floating point.

```rust
# #[cfg(not(feature = "std"))]
# fn main() {}
# #[cfg(feature = "std")]
# fn main() {
use music_core::Pitch;

assert!((Pitch::A.at(4).frequency_hz() - 440.0).abs() < 1e-9);
assert!((Pitch::A.at(4).frequency_hz_at(432.0) - 432.0).abs() < 1e-9);
# }
```

## Types

| Type | Holds | Size |
|------|-------|------|
| `Pitch` | one of twelve pitch classes | 1 byte |
| `Interval` | a signed semitone count | 2 bytes |
| `IntervalClass` | an interval folded to 0 through 6 | 1 byte |
| `PitchSet` | any subset of the twelve | 2 bytes |
| `Chord` | a named symbol like `Cmaj7` | 2 bytes |
| `Note` | a pitch with a register | 4 bytes |
| `Notes` | a free list, doublings allowed | 66 bytes |

## Try it

```sh
cargo run -p music-core --example pitch
cargo run -p music-core --example note
```

## License

MIT or Apache-2.0, at your option.

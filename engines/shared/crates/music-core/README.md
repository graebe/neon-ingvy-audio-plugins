# music-core

General music theory primitives in Rust: pitch classes, intervals, notes,
chords, keys and pitch-class sets.

No dependencies, `no_std` by default, and nothing here allocates. Every type is
`Copy` and small enough to pass around without thinking about it.

This is the layer underneath [`neo-riemann`](https://codeberg.org/graebe/neo-riemann),
which adds the PLR transformation group on top, and the theory NI Chord-Detector
names chords with. Nothing here knows about that theory, which is
the point of the separation.

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

## Chords

A chord is a root plus any set of pitches, so it is not limited to a fixed
vocabulary. A thirteenth, a cluster, or a chord nobody has named is as
representable as a triad.

```rust
use music_core::{Chord, ChordQuality, Pitch};

let dm7 = Chord::min7(Pitch::D);
assert_eq!(dm7.to_string(), "Dm7");
assert_eq!(dm7.quality(), Some(ChordQuality::Minor7));

// Add a note and the name follows.
let c = Chord::major(Pitch::C);
assert_eq!(c.with(Pitch::B).quality(), Some(ChordQuality::Major7));

// A chord with no name is still a chord, and still prints and parses.
let cluster = c.with(Pitch::C_SHARP);
assert_eq!(cluster.quality(), None);
assert_eq!(cluster.to_string(), "C[0,1,4,7]");
```

Thirty-one qualities are recognised, from the bare fifth through triads,
sixths, sevenths, added notes and extensions to the altered dominants. Each has
a constructor, so nothing needs to reach for the parser: `major`, `min7`,
`maj9`, `dom13`, `six_nine`, `dom7_sharp11`, `fifth`, `dom7_sus4` and the rest.

### The vocabulary

Two columns of numbers, because a chord is two things. `intervals()` gives the
pitch classes, which is what identification compares. `stacking()` gives the
heights they are written at, which is what you play, and what you type into a
chord effect. They differ only where a chord reaches past the octave.

| Symbol | Constructor | `intervals()` | `stacking()` | Character |
|---|---|---|---|---|
| *(none)* | `major` | 0 4 7 | 0 4 7 | Bright and settled; the sound of arrival |
| `m` | `minor` | 0 3 7 | 0 3 7 | Darker, and just as stable |
| `dim` | `dim` | 0 3 6 | 0 3 6 | Tense and unstable; resolves inward |
| `aug` | `aug` | 0 4 8 | 0 4 8 | Weightless; divides the octave evenly |
| `sus2` | `sus2` | 0 2 7 | 0 2 7 | Open and airy; neither major nor minor |
| `sus4` | `sus4` | 0 5 7 | 0 5 7 | Leaning; the fourth wants to fall |
| `maj7` | `maj7` | 0 4 7 11 | 0 4 7 11 | Soft and luminous |
| `7` | `dom7` | 0 4 7 10 | 0 4 7 10 | Restless; the engine of the cadence |
| `m7` | `min7` | 0 3 7 10 | 0 3 7 10 | Cool and unhurried |
| `mmaj7` | `min_maj7` | 0 3 7 11 | 0 3 7 11 | Uneasy and cinematic |
| `m7b5` | `half_dim7` | 0 3 6 10 | 0 3 6 10 | Aching; the Tristan chord |
| `dim7` | `dim7` | 0 3 6 9 | 0 3 6 9 | Maximum tension, no home |
| `6` | `sixth` | 0 4 7 9 | 0 4 7 9 | Sweet and old-fashioned |
| `m6` | `min6` | 0 3 7 9 | 0 3 7 9 | Wistful, slightly noir |
| `add9` | `add9` | 0 2 4 7 | 0 4 7 14 | Bright and open |
| `madd9` | `min_add9` | 0 2 3 7 | 0 3 7 14 | Cold and glassy |
| `6/9` | `six_nine` | 0 2 4 7 9 | 0 4 7 9 14 | Lush and final |
| `9` | `dom9` | 0 2 4 7 10 | 0 4 7 10 14 | Funk's dominant |
| `maj9` | `maj9` | 0 2 4 7 11 | 0 4 7 11 14 | Wide and gentle |
| `m9` | `min9` | 0 2 3 7 10 | 0 3 7 10 14 | Warm and deep; the dub techno chord |
| `11` | `dom11` | 0 2 4 5 7 10 | 0 4 7 10 14 17 | Blurred; the third usually goes |
| `m11` | `min11` | 0 2 3 5 7 10 | 0 3 7 10 14 17 | Spacious and modal |
| `13` | `dom13` | 0 2 4 7 9 10 | 0 4 7 10 14 21 | Full and brassy |
| `maj13` | `maj13` | 0 2 4 7 9 11 | 0 4 7 11 14 21 | The widest consonance here |
| `7b5` | `dom7_flat5` | 0 4 6 10 | 0 4 6 10 | Whole-tone and unmoored |
| `7#5` | `dom7_sharp5` | 0 4 8 10 | 0 4 8 10 | Straining upward |
| `7b9` | `dom7_flat9` | 0 1 4 7 10 | 0 4 7 10 13 | Sharp and dramatic |
| `7#9` | `dom7_sharp9` | 0 3 4 7 10 | 0 4 7 10 15 | The Hendrix chord |
| `7#11` | `dom7_sharp11` | 0 4 6 7 10 | 0 4 7 10 18 | Bright and acid; Lydian dominant |
| `5` | `fifth` | 0 7 | 0 7 | Bare and loud; no third, so no key |
| `7sus4` | `dom7_sus4` | 0 5 7 10 | 0 5 7 10 | Hanging; the gospel and house vamp |

That is declaration order, which is also `ChordQuality::ALL` order and the order
`completions` yields in.

## Inversions and slash chords

A chord carries a bass as well as a root. They are the same note until you say
otherwise, and when they differ the chord is written with a slash.

```rust
use music_core::{Chord, Pitch};

let c = Chord::maj7(Pitch::C);

assert_eq!(c.to_string(), "Cmaj7");
assert_eq!(c.over(Pitch::E).to_string(), "Cmaj7/E");
assert_eq!(c.over(Pitch::E).inversion(), 1);
assert_eq!(c.over(Pitch::B).inversion(), 3);

// The root never moves; only what is underneath it does.
assert_eq!(c.over(Pitch::G).root(), Pitch::C);
```

Setting a bass is not a pure relabelling. The bass is a note that sounds, so a
bass from outside the chord joins it, which is what makes `Am/F#` sayable at
all.

## Naming a chord you were handed

With a root known the answer is unique. Without one, a set of pitches usually
has several honest readings, so both are available.

```rust
use music_core::{Chord, ChordQuality, Pitch};

// C, E, G and A are a C6 and an A minor 7. Both readings are real.
let pitches = Chord::sixth(Pitch::C).pitches();
assert_eq!(pitches.interpretations().count(), 2);

// `identify` picks one under a documented heuristic: sevenths beat sixths.
assert_eq!(pitches.identify().unwrap().quality(), Some(ChordQuality::Minor7));

// A diminished seventh divides the octave evenly, so all four roots work.
assert_eq!(Chord::dim7(Pitch::C).pitches().interpretations().count(), 4);
```

`identify` is a heuristic wearing a definite-sounding name. When the answer
matters, read `interpretations` and choose with the context you have.

**A voicing knows more than a set does.** `Notes` holds real notes with octaves,
so it has a lowest one, and the bass is what decides between a chord and its
inversion. Naming a voicing uses it:

```rust
use music_core::{Chord, Notes, Pitch};

// The same four pitch classes, and the bass decides which chord they are.
let on_a = Notes::from_slice(&[
    Pitch::A.at(3), Pitch::C.at(4), Pitch::E.at(4), Pitch::G.at(4),
]).unwrap();
let on_c = Notes::from_slice(&[
    Pitch::C.at(3), Pitch::E.at(3), Pitch::G.at(3), Pitch::A.at(4),
]).unwrap();

assert_eq!(on_a.pitch_set(), on_c.pitch_set());
assert_eq!(on_a.identify(), Some(Chord::min7(Pitch::A)));
assert_eq!(on_c.identify(), Some(Chord::sixth(Pitch::C)));
```

Calling `pitch_set().identify()` on either gives A minor 7, because throwing the
register away throws away the evidence.

## Arranging a chord

A chord is a set of pitch classes, so it says nothing about octaves on its own.
`voice` gives it a register in close position; `voice_as` chooses something
else. Only a rootless voicing changes which pitches sound.

```rust
use music_core::{Chord, Pitch, Voicing};

let c = Chord::maj9(Pitch::C);

// Close packs everything into an octave. Stacked spells it as written.
assert_eq!(c.voice(4).to_string(), "[C4 D4 E4 G4 B4]");
assert_eq!(c.voice_as(4, Voicing::Stacked).to_string(), "[C4 E4 G4 B4 D5]");

// Drop 2 opens it out; rootless leaves the bass to someone else.
assert_eq!(c.voice_as(4, Voicing::Drop2).to_string(), "[B3 C4 E4 G4 D5]");
assert_eq!(c.voice_as(4, Voicing::Rootless).to_string(), "[E4 G4 B4 D5]");

// Turning a chord over is `rotate_up`, not `invert`: it moves the bass up an
// octave. `invert` already means mirroring pitch classes about an axis.
assert_eq!(Chord::major(Pitch::C).voice(4).rotate_up().to_string(), "[E4 G4 C5]");
```

## Finishing a fragment

Real voicings leave notes out, and a set that is missing one is not the chord it
happens to spell. `interpretations` asks what these notes *are*; `completions`
asks what they could be *part of*, and its root need not be among them.

```rust
use music_core::{Chord, Pitch, PitchSet};

// A C minor 9 as it would actually be played, with the root left to the bass.
let played = PitchSet::from_pitches(&[
    Pitch::E_FLAT, Pitch::G, Pitch::B_FLAT, Pitch::D,
]);

// Read as it stands it is an E flat major 7, and that is not wrong.
assert_eq!(played.identify(), Some(Chord::maj7(Pitch::E_FLAT)));

// It is also four fifths of a C minor 9, which nothing else would tell you.
assert!(played.completions().any(|c| c == Chord::min9(Pitch::C)));
```

## How open a voicing is

`Voicing::Open` spreads a chord past an octave by lifting every second note, and
any voicing can report how far it actually reaches.

```rust
use music_core::{Chord, Interval, Pitch, Voicing};

let chord = Chord::maj7(Pitch::C);
let close = chord.voice_as(3, Voicing::Close);   // C3 E3 G3 B3
let open = chord.voice_as(3, Voicing::Open);     // C3 G3 E4 B4

assert!(close.is_close());
assert!(!open.is_close());
assert!(open.spread().unwrap() > Interval::OCTAVE);

// Spreading changes register, never which notes sound.
assert_eq!(open.pitch_set(), close.pitch_set());
```

## Keys and modes

A **`Key`** is a tonic and one of the seven church modes. It knows its notes,
its signature, which way to spell, and what a chord is called inside it.

```rust
use music_core::{Chord, Key, Mode, Pitch, Spelling};

let d_dorian = Key::new(Pitch::D, Mode::Dorian);
assert_eq!(d_dorian.signature(), 0);              // C major's notes, from D

let c_minor = Key::new(Pitch::C, Mode::Aeolian);
assert_eq!(c_minor.signature(), -3);              // three flats
assert_eq!(c_minor.spelling(), Spelling::Flats);

// Roman numerals, with the root altered when it is not in the key.
let c_major = Key::new(Pitch::C, Mode::Ionian);
assert_eq!(c_major.degree_of(Chord::dom7(Pitch::G)).to_string(), "V7");
assert_eq!(c_major.degree_of(Chord::major(Pitch::B_FLAT)).to_string(), "bVII");
```

`Display` always spells with sharps, so one value has one text. **`spelled`**
prints the same chord or note with flats when the key asks for them:

```rust
use music_core::{Chord, Pitch, Spelling};

let chord = Chord::min7(Pitch::E_FLAT);
assert_eq!(chord.to_string(), "D#m7");
assert_eq!(chord.spelled(Spelling::Flats).to_string(), "Ebm7");
```

## Sets

```rust
use music_core::{Pitch, PitchSet};

// A pitch set is a 12-bit mask, so set operations are single instructions.
let major = PitchSet::from_pitches(&[Pitch::C, Pitch::E, Pitch::G]);
let minor = PitchSet::from_pitches(&[Pitch::A, Pitch::C, Pitch::E]);
assert_eq!((major & minor).len(), 2);
```

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
| `Chord` | a root plus any set of pitches | 4 bytes |
| `Voicing` | how a chord is spread out | 1 byte |
| `Triad` | major or minor, root plus quality | 2 bytes |
| `Note` | a pitch with a register | 4 bytes |
| `Notes` | a free list, doublings allowed | 66 bytes |
| `Mode` | one of the seven church modes | 1 byte |
| `Key` | a tonic and a mode | 2 bytes |
| `Degree` | a chord read as a roman numeral in a key | 6 bytes |

## Try it

```sh
cargo run -p music-core --example pitch    # the twelve, and moving between them
cargo run -p music-core --example note     # the same pitch, given a register
cargo run -p music-core --example chord    # roots, sets, and naming them
```

## License

Copyright © 2026 Torben Gräber. Released under the GNU General Public License,
version 3 or (at your option) any later version — see
[LICENSE](../../../../LICENSE).

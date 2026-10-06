---
title: Keys and modes
order: 6
slug: keys
---

A `Key` is a tonic and a mode. It says which seven pitches belong, how to spell
the black keys, and what a chord is called inside it.

```rust
use music_core::Key;
use music_core::Mode;
use music_core::Pitch;

let c_major = Key::new(Pitch::C, Mode::Ionian);
let a_minor = Key::new(Pitch::A, Mode::Aeolian);
let d_dorian = Key::new(Pitch::D, Mode::Dorian);
```

## The Seven Modes

A mode is the major scale started from another of its notes. Ionian is the
major scale itself, and Aeolian is the natural minor.

| Mode | Steps above the tonic | Starts on, in C major |
|------|-----------------------|-----------------------|
| Ionian | 0 2 4 5 7 9 11 | C |
| Dorian | 0 2 3 5 7 9 10 | D |
| Phrygian | 0 1 3 5 7 8 10 | E |
| Lydian | 0 2 4 6 7 9 11 | F |
| Mixolydian | 0 2 4 5 7 9 10 | G |
| Aeolian | 0 2 3 5 7 8 10 | A |
| Locrian | 0 1 3 5 6 8 10 | B |

```rust
use music_core::Mode;

let steps = Mode::Dorian.steps();       // [0, 2, 3, 5, 7, 9, 10]
let name = Mode::Dorian.to_string();    // "Dorian"
let all = Mode::ALL.len();              // 7
```

## The Notes Of A Key

`pitch_set` gives the seven pitches. `parent` gives the major scale they come
from.

```rust
use music_core::Key;
use music_core::Mode;
use music_core::Pitch;

let d_dorian = Key::new(Pitch::D, Mode::Dorian);

let notes = d_dorian.pitch_set().to_string();  // "{C, D, E, F, G, A, B}"
let parent = d_dorian.parent();                // C
let has_b = d_dorian.pitch_set().contains(Pitch::B); // true
```

D Dorian uses the white keys, just like C major. Only the starting note differs.

## The Signature

`signature` counts the sharps or flats: positive for sharps, negative for flats.
It is the parent major's, so it is also the key's place on the circle of fifths.

```rust
use music_core::Key;
use music_core::Mode;
use music_core::Pitch;

let g = Key::new(Pitch::G, Mode::Ionian).signature();          // 1
let f = Key::new(Pitch::F, Mode::Ionian).signature();          // -1
let c_minor = Key::new(Pitch::C, Mode::Aeolian).signature();   // -3
let a_minor = Key::new(Pitch::A, Mode::Aeolian).signature();   // 0
```

The range is -5 to 6. Six is always six sharps: a pitch class cannot say
whether F sharp or G flat was meant.

Each pitch also knows its own place on the circle, counted in fifths above C.

```rust
use music_core::Pitch;

let g = Pitch::G.fifths();     // 1
let d = Pitch::D.fifths();     // 2
let f = Pitch::F.fifths();     // 11, one fifth below C
```

## Spelling

Printing always uses sharps, so one value has one text. A key that has flats
asks for them with `spelling`. Hand that to `spelled` on a chord or a note.

```rust
use music_core::Chord;
use music_core::Key;
use music_core::Mode;
use music_core::Pitch;

let key = Key::new(Pitch::C, Mode::Aeolian);
let spelling = key.spelling();               // Flats

let chord = Chord::major(Pitch::A_FLAT);
let plain = chord.to_string();               // "G#"
let in_key = chord.spelled(spelling).to_string(); // "Ab"

let note = Pitch::E_FLAT.at(4);
let printed = note.spelled(spelling).to_string(); // "Eb4"
```

The value does not change. Only its text does.

## Roman Numerals

`degree_of` names a chord by where its root sits in the key. The case of the
numeral follows the chord's third: upper for major, lower for minor.

```rust
use music_core::Chord;
use music_core::Key;
use music_core::Mode;
use music_core::Pitch;

let c_major = Key::new(Pitch::C, Mode::Ionian);

let one = c_major.degree_of(Chord::major(Pitch::C)).to_string();   // "I"
let two = c_major.degree_of(Chord::min7(Pitch::D)).to_string();    // "ii7"
let five = c_major.degree_of(Chord::dom7(Pitch::G)).to_string();   // "V7"
let seven = c_major.degree_of(Chord::dim(Pitch::B)).to_string();   // "vii°"
```

A root outside the key still gets a numeral. It is marked flat or sharp
against the key's own step.

```rust
use music_core::Chord;
use music_core::Key;
use music_core::Mode;
use music_core::Pitch;

let c_major = Key::new(Pitch::C, Mode::Ionian);
let borrowed = c_major.degree_of(Chord::major(Pitch::B_FLAT)).to_string(); // "bVII"

let a_minor = Key::new(Pitch::A, Mode::Aeolian);
let leading = a_minor.degree_of(Chord::dim7(Pitch::G_SHARP)).to_string(); // "#vii°7"
```

| Root above the tonic | Written as, when outside the key |
|----------------------|----------------------------------|
| 1 semitone | `bII` |
| 3 semitones | `bIII` |
| 6 semitones | `#IV` |
| 8 semitones | `bVI` |
| 10 semitones | `bVII` |

In a minor or modal key the same rule raises a note instead, when the major
scale has it higher: the leading tone of A minor is `#vii`.

The bass does not change a numeral. `Am7/C` in C major is still `vi7`.

## Naming Two Notes

Chords need three notes, or a fifth. Two other notes are an interval, and
`name` says which.

```rust
use music_core::Interval;

let third = Interval::MAJOR_THIRD.name();    // "major 3rd"
let tenth = Interval::new(16).name();        // "major 3rd"
let more = Interval::new(16).compound_octaves(); // 1
```

A tenth is a third and an octave more, so its name is the third's.

See [Where to go next](#neo-riemann) for the crate that builds on all of this.

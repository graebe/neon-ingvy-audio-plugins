---
title: Giving a pitch a register
order: 2
slug: notes
---

A note is a voiced pitch (a pitch with an octave). We can initialize it in different ways

```rust
use music_core::Pitch;
use music_core::Note;

let c3 = Pitch::C.at(3);
let c4 = Note::from_midi(60);
let eb3 = "Eb3".parse::<Note>().unwrap();
```

## Converting a Pitch to a Note

We can turn a `Pitch` into a `Note` by giving it an octave.

```rust
use music_core::Pitch;

let c = Pitch::C;
let c_in_fourth_octave = c.at(4);
```
The data type of a `Note` is `Voiced<Pitch>`. Every note now corresponds to a specific frequency.

```rust
use music_core::Pitch;

let c = Pitch::C;
let c_in_fourth_octave = c.at(4);
let frequency = c_in_fourth_octave.frequency_hz();
```

This table shows some example frequencies for the 4th and 5th octaves.

| Note           | C4     | D4     | E4     | F4     | G4     | A4     | B4     | C5     |
|----------------|--------|--------|--------|--------|--------|--------|--------|--------|
| Frequency (Hz) | 261.63 | 293.66 | 329.63 | 349.23 | 392.00 | 440.00 | 493.88 | 523.25 |
| Midi Number     | 60     | 62     | 64     | 65     | 67     | 69     | 71     | 72     |

`music-core` gives you the number, not the sound. It has no audio output of its
own, so feed `.midi()` to a synth or a MIDI library when you want to hear it.

```rust
use music_core::Pitch;

let midi = Pitch::C.at(4).midi(); // 60
```

## Arithmetic

Notes support basic arithmetic operations like addition and subtraction with intervals.

```rust
use music_core::Pitch;
use music_core::Interval;

let c4 = Pitch::C.at(4);
let major_third = Interval::MAJOR_THIRD;

let e4 = c4 + major_third;
assert_eq!(e4, Pitch::E.at(4));
```

We can also calculate the difference between two notes using subtraction.

```rust
use music_core::Pitch;
use music_core::Interval;

let c4 = Pitch::C.at(4);
let e4 = Pitch::E.at(4);

let major_third = e4 - c4;
assert_eq!(major_third, Interval::MAJOR_THIRD);
```

## Comparison

Two notes are equal only when the pitch and the octave both match.

```rust
use music_core::Pitch;

let c4 = Pitch::C.at(4);
let c5 = Pitch::C.at(5);

let same = c4 == Pitch::C.at(4);           // true
let octave_apart = c4 == c5;               // false, the octave differs
let enharmonic = Pitch::D_SHARP.at(4) == Pitch::E_FLAT.at(4); // true
```

## Other Concert Pitches

The table above assumes A4 sounds at 440 Hz. Orchestras have not always tuned
there, so you can ask for another reference and every note shifts with it.

```rust
use music_core::Pitch;

let modern = Pitch::A.at(4).frequency_hz_at(440.0);  // 440.00
let older = Pitch::A.at(4).frequency_hz_at(432.0);   // 432.00
let baroque = Pitch::C.at(4).frequency_hz_at(415.0); // 246.76
```

These two methods are the only part of `music-core` that needs the standard
library, because `core` has no floating-point maths. Everything else on this
page works in a `no_std` build.

## A List of Notes

`Notes` holds up to sixteen notes in the order you add them. Unlike a set it
keeps that order, and it can hold the same note twice.

```rust
use music_core::Pitch;
use music_core::Notes;

let mut chord = Notes::EMPTY;
let _ = chord.push(Pitch::C.at(3));
let _ = chord.push(Pitch::G.at(3));
let _ = chord.push(Pitch::E.at(4));

let count = chord.len();          // 3
let lowest = chord.bass();        // Some(C3)
let printed = chord.to_string();  // "[C3 G3 E4]"
```

Drop the octaves and you are left with the pitches alone.

```rust
use music_core::Pitch;
use music_core::Notes;

let chord = Notes::from_slice(&[Pitch::C.at(3), Pitch::E.at(4), Pitch::C.at(5)]).unwrap();

let pitches = chord.pitch_set();     // {C, E}
let unique = chord.pitch_set().len(); // 2, the C appears twice above
```

## Printing Notes

A note can be printed using the `Debug` or `Display` traits.

```rust
use music_core::Pitch;
use music_core::Note;

let c4 = Pitch::C.at(4);
let c4_note = Note::from_midi(60);

println!("{:?}", c4_note); // Debug format
println!("{}", c4_note);   // Display format
```

See the [Pitch sets tutorial](#pitch-sets) for what happens when you throw
the octaves away entirely.

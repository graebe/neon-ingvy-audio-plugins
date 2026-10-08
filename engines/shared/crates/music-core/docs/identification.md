---
title: Identifying a pitch set
order: 5
slug: identification
---

A `PitchSet` has no root, so it does not know what chord it is. Working that out
means trying each pitch as a root and seeing which ones name something.

```rust
use music_core::Pitch;
use music_core::PitchSet;

let notes = PitchSet::from_pitches(&[Pitch::D, Pitch::F_SHARP, Pitch::A]);

let named = notes.identify(); // Some(D)
```

A set that spells nothing gets no answer.

```rust
use music_core::Pitch;
use music_core::PitchSet;

let nothing = PitchSet::from_pitches(&[Pitch::C, Pitch::C_SHARP]);

let named = nothing.identify(); // None
```

## Every Reading

Often a set can be read more than one way. `interpretations` yields all
of them, one per pitch that works as a root.

```rust
use music_core::Pitch;
use music_core::PitchSet;

let notes = PitchSet::from_pitches(&[Pitch::C, Pitch::E, Pitch::G, Pitch::A]);

for chord in notes.interpretations() {
    println!("{}", chord); // C6, then Am7
}

let count = notes.interpretations().count(); // 2
```

Both readings are correct. The same four notes really are a C sixth and an A
minor seventh.

## How identify Chooses

`identify` is a convenience over `interpretations` for when you want one answer. It prefers the lowest rank, and ties go to the
lower root.

| Rank | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|------|---|---|---|---|---|---|---|---|---|
| Kind | major, minor | diminished, augmented | suspended | sevenths | sixths | added ninths | ninths | elevenths, thirteenths | altered |

So for C, E, G, A the seventh beats the sixth.

```rust
use music_core::ChordQuality;
use music_core::Pitch;
use music_core::PitchSet;

let notes = PitchSet::from_pitches(&[Pitch::C, Pitch::E, Pitch::G, Pitch::A]);
let best = notes.identify().unwrap();

let root = best.root();        // A
let quality = best.quality();  // Some(ChordQuality::Minor7)
```

When the answer matters, read `interpretations` and decide with the context you
have.

## Chords With No Single Root

A few chords divide the octave into equal steps, so every one of their notes
works as a root.

```rust
use music_core::Chord;
use music_core::Pitch;

let dim7 = Chord::dim7(Pitch::C).pitches();
let aug = Chord::aug(Pitch::C).pitches();

let four = dim7.interpretations().count(); // 4: Cdim7, D#dim7, F#dim7, Adim7
let three = aug.interpretations().count(); // 3: Caug, Eaug, G#aug
```

This is not a flaw. It is a real property of those chords, and it is why a
diminished seventh can pivot to so many places in a piece of music.

## Naming a Voicing

Everything above works on pitch classes, which have no octave and so no lowest
note. But the bass is exactly what decides whether three pitches are a C major
triad or a C over E.

`Notes` holds real notes, so it has a bass, and it can identify itself.

```rust
use music_core::Notes;
use music_core::Pitch;

let rooted_on_a = Notes::from_slice(&[
    Pitch::A.at(3),
    Pitch::C.at(4),
    Pitch::E.at(4),
    Pitch::G.at(4),
]).unwrap();

let rooted_on_c = Notes::from_slice(&[
    Pitch::C.at(3),
    Pitch::E.at(3),
    Pitch::G.at(3),
    Pitch::A.at(4),
]).unwrap();

let first = rooted_on_a.identify();  // Some(Am7)
let second = rooted_on_c.identify(); // Some(C6)
```

The same four pitch classes, read two different ways, decided by what is
underneath. Throw the octaves away and that evidence is gone.

| Voicing | `Notes::identify` | `pitch_set().identify()` |
|---------|-------------------|--------------------------|
| `[A3 C4 E4 G4]` | `Am7` | `Am7` |
| `[C3 E3 G3 A4]` | `C6` | `Am7` |
| `[E3 G3 C4]` | `C/E` | `C` |

That last row is the other half of it. When the bass will not serve as a root,
the reading falls back to the pitch classes and records the bass as a slash.

```rust
use music_core::Notes;
use music_core::Pitch;

let over_e = Notes::from_slice(&[
    Pitch::E.at(3),
    Pitch::G.at(3),
    Pitch::C.at(4),
]).unwrap();

let named = over_e.identify(); // Some(C/E)
```

Prefer this over `pitch_set().identify()` whenever you have real notes. It is
strictly better informed, because it keeps the one piece of evidence that tells
an inversion from a chord in root position.

Inversions cannot round-trip exactly, and that is a fact about music rather than
a gap in the library. A C6 over E and an A minor 7 over E are the same sounding
notes; only the surrounding music decides which one was meant. What does survive
is the pitch set and the bass.

## What Could This Be Part Of

`interpretations` asks what these notes *are*. `completions` asks what they could
be *part of*, which is a different question — real voicings leave notes out, and
the root goes first.

```rust
use music_core::Pitch;
use music_core::PitchSet;

// A C minor 9 as it would actually be played, with no C in it.
let played = PitchSet::from_pitches(&[
    Pitch::E_FLAT,
    Pitch::G,
    Pitch::B_FLAT,
    Pitch::D,
]);

let read_as_is = played.identify(); // Some(D#maj7)

for chord in played.completions() {
    println!("{}", chord); // Cm9, Cm11, D#maj7, D#maj9, D#maj13
}
```

Reading it as it stands gives an E flat major 7, and that is not wrong. But it is
also four fifths of a C minor 9, which nothing else would tell you — and the root
of that chord is not among the notes at all.

Printing uses sharps, so the E flat reads back as `D#`. The value is the same
either way.

Every interpretation is also a completion, since a chord contains itself. The
empty set is contained in everything, so it yields all 372 named chords.

See the [Chords tutorial](#chords) for building a chord when you already know
its root.

See [Keys and modes](#keys) for printing it with flats, and for what a chord
is called inside a key.

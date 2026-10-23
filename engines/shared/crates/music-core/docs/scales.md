---
title: Scales and families
order: 7
slug: scales
---

A `Scale` is the shape of a scale: its notes counted upward from the root. The
root is always there, at zero, so a scale says how it is built and never where
it starts.

```rust
use music_core::Scale;

let major = Scale::MAJOR;
let blues = Scale::new(&[0, 3, 5, 6, 7, 10]);

let size = major.len();               // 7
let spelled = major.to_string();      // "1 2 3 4 5 6 7"
let name = blues.name();              // Some("Blues")
```

`on` puts a scale on a tonic and gives back its pitches.

```rust
use music_core::Pitch;
use music_core::Scale;

let g_major = Scale::MAJOR.on(Pitch::G);
let has_f_sharp = g_major.contains(Pitch::F_SHARP);   // true
```

## Modes

A mode is the same notes started on another degree. `mode(1)` starts on the
second note, because the root is degree zero.

```rust
use music_core::Scale;

let dorian = Scale::MAJOR.mode(1);
let lydian = Scale::MAJOR.mode(3);

let dorian_spelled = dorian.to_string();   // "1 2 b3 4 5 6 b7"
let lydian_name = lydian.name();           // Some("Lydian")
```

Every rotation of the major scale is a different shape, so it has seven modes.
The whole-tone scale has one: start it on any of its notes and you get the
whole-tone scale again.

```rust
use music_core::Scale;

let major_modes = Scale::MAJOR.mode_count();        // 7
let whole_tone_modes = Scale::WHOLE_TONE.mode_count(); // 1
```

## Four Numbers For Every Scale

Four counts describe how a scale sits in the octave. They are the numbers at
the top of a scale portrait.

| Letter | Method | Means |
|--------|--------|-------|
| N | `len` | how many notes |
| M | `mode_count` | how many different modes |
| s | `symmetry` | how often the shape repeats around the octave |
| T | `transpositions` | how many different sets the shape makes on the twelve tonics |

They always fit together: N is M times s, and 12 is T times s.

```rust
use music_core::Scale;

let octatonic = Scale::OCTATONIC;

let n = octatonic.len();             // 8
let m = octatonic.mode_count();      // 2
let s = octatonic.symmetry();        // 4
let t = octatonic.transpositions();  // 3
```

Most scales have a symmetry of one. Only fifteen families of three or more
notes repeat themselves, and Messiaen built a language from them.

## Families

All the modes of one shape form a family. `family` finds it, and any mode of
the family finds the same one.

```rust
use music_core::Scale;

let from_ionian = Scale::MAJOR.family();
let from_dorian = Scale::MAJOR.mode(1).family();

let same = from_ionian == from_dorian;       // true
let name = from_ionian.name();               // Some("Diatonic")
let known_by = from_ionian.reference();      // the major scale
```

A family is stored as its **canonical** mode, the one with the smallest bit
mask. That is Rahn's normal order, and for the diatonic family it is Locrian.
`reference` gives the mode the family is known by instead.

`ScaleFamily::all` walks every family. With three or more notes there are 344
of them, holding 2,036 different scales between them.

```rust
use music_core::ScaleFamily;

let families = ScaleFamily::all(3).count();    // 344
```

## Spelling The Degrees

`degrees` names each note by its degree, the way chord charts and method books
do: `b3` is a minor third, `#4` a raised fourth. The numbers never go down, and
each number is used once where it can be.

```rust
use music_core::Scale;

let dominant_with_both_thirds = Scale::new(&[0, 3, 4, 7, 10]);
let spelled = dominant_with_both_thirds.to_string();   // "1 #2 3 5 b7"

let whole_tone = Scale::WHOLE_TONE.to_string();        // "1 2 3 b5 b6 b7"
```

The minor third is written `#2` above because the 2 is free and the 3 is taken.
When a number has to be shared, a flat is chosen over a sharp.

## Brightness

Brightness counts how far a scale leans to the sharp side of the circle of
fifths. G counts +1, D +2, F -1, B flat -2, and the tritone counts zero. The
sum is divided by the most any scale of that size can reach, so it runs from
-1.0 to 1.0.

| Mode | Brightness |
|------|------------|
| Lydian | 1.00 |
| Ionian | 0.93 |
| Mixolydian | 0.47 |
| Dorian | 0.00 |
| Aeolian | -0.47 |
| Phrygian | -0.93 |
| Locrian | -1.00 |

```rust
use music_core::Scale;

let lydian = Scale::MAJOR.mode(3).brightness();   // 1.0
let dorian = Scale::MAJOR.mode(1).brightness();   // 0.0
```

The tritone counts zero because it lies as far round the circle one way as
the other. That is what makes a scale and its mirror image, `inverted`, always
come out with opposite brightness.

## A Chord Against A Scale

`ChordScale` puts a chord on a scale's root and says what each note of the
scale does there.

| Role | Means |
|------|-------|
| Guide | the root, third, seventh or sixth: what makes the chord its quality |
| Colour | the chord's other notes, like the fifth |
| Tension | a scale note outside the chord that sits well over it |
| Avoid | a scale note a semitone above a chord note, which rubs when held |

```rust
use music_core::ChordQuality;
use music_core::ChordScale;
use music_core::Pitch;
use music_core::Scale;
use music_core::ToneRole;

let ionian = ChordScale::from_parts(Scale::MAJOR, ChordQuality::Major7).unwrap();

let third = ionian.role(Pitch::E);     // ToneRole::Guide
let fourth = ionian.role(Pitch::F);    // ToneRole::Avoid
let ninth = ionian.role(Pitch::D);     // ToneRole::Tension
let clash = ionian.clash();            // one of three outside notes: 0.33
```

`from_parts` gives `None` when the chord does not fit inside the scale.

For a chord on another degree, ask the mode on that degree. A D minor 7 in C
major is the minor 7 against D Dorian, which is `Scale::MAJOR.mode(1)`.

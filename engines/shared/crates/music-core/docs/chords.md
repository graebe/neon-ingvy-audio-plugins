---
title: Chords
order: 4
slug: chords
---

A `Chord` is two things together: a root, and a set of pitches. The usual way to
build one is to name the kind you want.

```rust
use music_core::Chord;
use music_core::Pitch;

let c = Chord::major(Pitch::C);
let dm7 = Chord::min7(Pitch::D);
let g7 = Chord::dom7(Pitch::G);

let root = c.root();          // C
let pitches = c.pitches();    // {C, E, G}
let printed = dm7.to_string(); // "Dm7"
```

There are thirty-one named kinds, from a bare fifth up to thirteenths. None of
them can fail, so there is nothing to unwrap.

| Kind | `major` | `minor` | `dom7` | `maj7` | `min7` | `dim7` | `sixth` | `six_nine` |
|------|---------|---------|--------|--------|--------|--------|---------|------------|
| Prints as | `C` | `Cm` | `C7` | `Cmaj7` | `Cm7` | `Cdim7` | `C6` | `C6/9` |

You can also build a chord from a root and any set of pitches. If you leave the
root out of the set it is put back in, because a chord contains its own root.

```rust
use music_core::Chord;
use music_core::Pitch;
use music_core::PitchSet;

let without_root = PitchSet::from_pitches(&[Pitch::E, Pitch::G]);
let c = Chord::new(Pitch::C, without_root);

let pitches = c.pitches(); // {C, E, G}
```

## The Root Is Part of the Chord

The same four pitches with two different roots are two different chords.

```rust
use music_core::Chord;
use music_core::Pitch;
use music_core::PitchSet;

let notes = PitchSet::from_pitches(&[Pitch::C, Pitch::E, Pitch::G, Pitch::A]);

let on_c = Chord::new(Pitch::C, notes); // C6
let on_a = Chord::new(Pitch::A, notes); // Am7

let same_notes = on_c.pitches() == on_a.pitches(); // true
let same_chord = on_c == on_a;                     // false
```

Once the root is known, the kind of chord is not a matter of opinion.

```rust
use music_core::Chord;
use music_core::ChordQuality;
use music_core::Pitch;

let c = Chord::major(Pitch::C);

let quality = c.quality(); // Some(ChordQuality::Major)
```

## Inversions and Slash Chords

A chord carries a bass alongside its root. It equals the root until you say
otherwise, and `over` is how you say otherwise.

```rust
use music_core::Chord;
use music_core::Pitch;

let c = Chord::major(Pitch::C);
let first_inversion = c.over(Pitch::E);

let printed = first_inversion.to_string();   // "C/E"
let root = first_inversion.root();           // C, unchanged
let bass = first_inversion.bass();           // E
let inverted = first_inversion.is_inverted(); // true
```

The root still decides what kind of chord it is. The bass only decides which
note is underneath.

`inversion` counts upward from the root through the chord's own notes, so it
works for chords of any size.

```rust
use music_core::Chord;
use music_core::Pitch;

let c = Chord::maj7(Pitch::C); // C E G B

let root_position = c.inversion();            // 0
let first = c.over(Pitch::E).inversion();     // 1
let second = c.over(Pitch::G).inversion();    // 2
let third = c.over(Pitch::B).inversion();     // 3
```

| Bass | `C` | `E` | `G` | `B` |
|------|-----|-----|-----|-----|
| Prints as | `Cmaj7` | `Cmaj7/E` | `Cmaj7/G` | `Cmaj7/B` |
| `inversion()` | 0 | 1 | 2 | 3 |

Slash chords print and parse. The split happens at the last slash, so `C6/9`
stays a six-nine chord and only `C6/9/E` puts one underneath it.

```rust
use music_core::Chord;

let six_nine = "C6/9".parse::<Chord>().unwrap();   // C6/9, no bass given
let over_e = "C6/9/E".parse::<Chord>().unwrap();   // C6/9/E
let slash = "C/E".parse::<Chord>().unwrap();       // C/E
```

`in_root_position` puts the root back underneath. It does not undo `over`,
because a bass that was not already one of the pitches stays in the chord — it
sounded.

```rust
use music_core::Chord;
use music_core::Pitch;

let c = Chord::major(Pitch::C);

let back = c.over(Pitch::E).in_root_position(); // C
```

## Adding and Removing Notes

Adding a note can turn the chord into a different kind of chord.

```rust
use music_core::Chord;
use music_core::Interval;
use music_core::Pitch;

let c = Chord::major(Pitch::C);

let c_maj7 = c.with(Pitch::B);     // Cmaj7
let back = c_maj7.without(Pitch::B); // C
let no_change = c.without(Pitch::C); // C, the root cannot be removed
let g = c.transpose(Interval::PERFECT_FIFTH); // G
```

## Chords With No Name

Most possible chords have no name, and that is fine. Printing falls back to the
root followed by the semitones above it, and that form reads back in.

```rust
use music_core::Chord;
use music_core::Pitch;
use music_core::PitchSet;

let odd = Chord::new(
    Pitch::C,
    PitchSet::from_pitches(&[Pitch::C, Pitch::C_SHARP, Pitch::E, Pitch::F_SHARP]),
);

let quality = odd.quality();     // None
let printed = odd.to_string();   // "C[0,1,4,6]"
```

## Reading a Chord From Text

Chord symbols written the usual way can be read in.

```rust
use music_core::Chord;

let dm7 = "Dm7".parse::<Chord>().unwrap();
let maj7 = "F#maj7".parse::<Chord>().unwrap();
let refused = "Cwobble".parse::<Chord>(); // Err(InvalidQuality)
```

Prefer the constructors in your own code. They cannot fail, so there is nothing
to handle. Reading text is for input that came from a person.

## Playing a Chord

A chord has no octave, the same way a pitch has none. Give it one and you get
back real notes stacked upward from the root.

```rust
use music_core::Chord;
use music_core::Pitch;

let c = Chord::major(Pitch::C);

let low = c.voice(3).to_string();  // "[C3 E3 G3]"
let mid = c.voice(4).to_string();  // "[C4 E4 G4]"
let seventh = Chord::maj7(Pitch::C).voice(4).to_string(); // "[C4 E4 G4 B4]"
```

`voice` stacks everything inside one octave. `voice_as` lets you ask for
something else.

```rust
use music_core::Chord;
use music_core::Pitch;
use music_core::Voicing;

let c = Chord::maj7(Pitch::C);

let close = c.voice_as(3, Voicing::Close);       // [C3 E3 G3 B3]
let drop2 = c.voice_as(3, Voicing::Drop2);       // [G2 C3 E3 B3]
let open = c.voice_as(3, Voicing::Open);         // [C3 G3 E4 B4]
let rootless = c.voice_as(3, Voicing::Rootless); // [E3 G3 B3]
```

| Voicing | Puts the notes |
|---------|----------------|
| `Close` | All inside one octave. What `voice` does on its own. |
| `Stacked` | As written, with extensions above the octave. |
| `Drop2` | Stacked, with the second note from the top dropped an octave. |
| `Open` | Every second note lifted, so the chord reaches past an octave. |
| `Rootless` | Without the root, as a pianist would play under a bass. |

However a voicing was built, `spread` reports how far it actually reaches and
`is_close` says whether it fits inside an octave.

```rust
use music_core::Chord;
use music_core::Pitch;
use music_core::Voicing;

let c = Chord::maj7(Pitch::C);

let close = c.voice_as(3, Voicing::Close);
let open = c.voice_as(3, Voicing::Open);

let narrow = close.spread().unwrap().semitones(); // 11
let wide = open.spread().unwrap().semitones();    // 23
let fits = close.is_close();                      // true
let does_not = open.is_close();                   // false
```

See the [Identification tutorial](#identification) for going the other way:
working out the chord when all you have is the notes.

## The Chord Grammar

`ChordType` is the system behind chord names: a **base** (a triad, a sixth
or a seventh) plus **tensions** (♭9 9 ♯9, 11 ♯11, ♭13 13). `ChordType::all()`
lists every chord type the grammar allows: 111 of them. Every rule below has
a source; where sources disagree, the choice is stated.

```rust
use music_core::{ChordBase, ChordType, ScaleDegree, Tension, Tensions};

let altered = ChordType::new(
    ChordBase::Dominant7,
    Tensions::NONE.with(Tension::FlatNine).with(Tension::SharpEleven),
).unwrap();
assert_eq!(altered.symbol().as_str(), "7b9#11");
assert_eq!(altered.numeral(ScaleDegree::new(5, 0)).as_str(), "V7b9#11");
assert!(ChordType::all().any(|c| c == altered));
```

### The bases

The tertian kinds of MusicXML 4.0 (`kind-value`: the triads, `major-sixth`,
`minor-sixth`, the seven sevenths, the two suspensions), plus the
fifth-altered sevenths MusicXML spells with a `degree` alteration: 7♭5,
maj7♯5, and 7sus4 (`suspended-fourth` with an added 7). Source: W3C Music
Notation CG, *MusicXML 4.0 Reference*,
<https://www.w3.org/2021/06/musicxml40/musicxml-reference/data-types/kind-value/>.

### Avoid notes and tensions

- **An avoid note** is a scale tone a half step above the chord's root,
  third, fifth or seventh (Mulholland & Hojnacki, *The Berklee Book of Jazz
  Harmony*, p. 21, as reported in K. Salley's review, *Music Theory Online*
  21.1, 2015, <https://www.mtosmt.org/issues/mto.15.21.1/mto.15.21.1.salley.html>).
  `ChordScale::avoid_notes` applies it to the base's tones.
- **The dominant exception:** over a dominant seventh (7, 7♭5, 7♯5, 7sus4)
  the ♭9 and the ♭13 are tensions. Berklee lists the dominant's tensions as
  9, ♭9, ♯9, ♯11, ♭13, 13 (W. Naus, "II-V or Not II-V?", *Berklee Today*,
  <https://college.berklee.edu/berklee-today-55>); Mulholland & Hojnacki
  name lowered ninths and thirteenths on dominants as the exception (Salley,
  above).
- **A tension is available on a base** when at least one named scale of five
  or more notes holds the base on its root with that tone as a tension. All
  of a chord's tensions must come from one such scale. This reproduces
  Berklee's lists: major 7 takes 9, ♯11, 13; dominant 7 the six above;
  half-diminished 9, 11, ♭13 (and 13, from Dorian ♭5); diminished 7 9, 11,
  ♭13, "any note a whole step above a chord tone" (Naus).
- **Two disagreements, and the choice made:** Terefenko counts the 13 over a
  minor 7 as an avoid note while Open Music Theory equates Dm13 with Dorian;
  the rule here follows the half-step test, so m13 is in. Terefenko counts the
  major third over a sus chord as an avoid note while Levine allows it; the
  half-step test does not flag it, and no tension names it, so it does not
  arise.

### Combining tensions (`ChordType::follows_symbol_rules`)

- A triad takes only an added ninth (`add9`, `madd9`), a sixth chord only the
  ninth (`6/9`) (R. Hutchinson, *Music Theory for the 21st-Century
  Classroom*, §31.1; *Open Music Theory* 2e, 6.2 "Chord Symbols").
- Natural extensions imply those below: 11 implies the 9 (OMT 6.2;
  Hutchinson §31.1). A 13 implies a ninth; on a dominant or major chord it
  leaves the 11 out, on a minor chord it keeps it (Hutchinson §31.1 and Fig.
  31.4.4; C. Dobrian, "List of jazz chord symbols",
  <https://music.arts.uci.edu/dobrian/f14/music16D/dobrianchordsymbols.htm>).
- No natural 11 over a major third: arrangers "omit the unaltered 11th
  because it clashes" (Hutchinson §31.1).
- Altered ninths only on dominant sevenths (Naus), ♯9 only over a major
  third; ♭9 may join ♯9 (both in the half-whole and altered scales,
  Hutchinson §31.10). No source shows 9 with ♭9 or 13 with ♭13, so neither is
  allowed.

### Symbols

The chord's highest natural extension takes the seventh's place (`9`, `11`,
`13`); `maj` before it makes the seventh major (`maj9`); fifth alterations
follow the number (`9#5`, `m9b5`), then the other alterations in ascending
order (`13b9#11`), without parentheses (Dobrian's spelling; Berklee also
uses parentheses, Hutchinson both). A diminished seventh keeps its 7 and
lists tensions in parentheses (`dim7(9,b13)`). `sus4` follows the extension
(`13sus4b9`) (OMT 6.2). The minor-major chord is `m(maj7)`, unlike
`ChordQuality::MinorMajor7`'s older `mmaj7`.

### Roman numerals (`ChordType::numeral`)

Numerals are relative to the major scale on the tonic, as music-core spells
degrees: A Aeolian reads i ii° ♭III iv v ♭VI ♭VII. This is the convention of
D. Temperley & T. de Clercq, "Statistical Analysis of Harmony and Melody in
Rock Music" (*Journal of New Music Research* 42.3, 2013) and of *Open Music
Theory*'s modal chapter; the classical minor-key convention writes VI and VII
without flats. Case follows the third (upper case major, lower case minor);
° diminished, ø half-diminished, + augmented; `7` is a minor seventh, `maj7` a
major one (Hutchinson §8.3, with `maj7` for his `M7`). Sus chords are written
upper case; the only attested form is the rock corpus's `Vs4`, and `Vsus4` is
this library's spelling of it.

### The families

The columns of a chord table, from the headings of The New Real Book's chord
list (as Hutchinson reproduces it, §31.4): major (maj7♯5 included), dominant
(the altered dominants included), minor, half-diminished, diminished,
suspended, and other (the minor-major chords and the augmented triad). No
source found names an "augmented" or "minor-major" family of its own.

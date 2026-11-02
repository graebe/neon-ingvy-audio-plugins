// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! Chord types built by rule: a base chord (a triad, a sixth or a seventh)
//! with any tensions added on top.
//!
//! [`ChordQuality`] is a fixed list of names. A [`ChordType`] is the grammar
//! behind such names: the base kinds are those of the MusicXML standard, and
//! the tensions are the ninths, elevenths and thirteenths chord–scale theory
//! allows over them. Every quality is a chord type
//! ([`ChordQuality::chord_type`]); far more chord types exist than qualities.
//! docs/chords.md gives the rules and their sources.

use core::fmt::{self, Write};

use crate::DisplayBuffer;
use crate::chord::ChordQuality;
use crate::chordscale::ChordScale;
use crate::pitch::Pitch;
use crate::pitchset::PitchSet;
use crate::scale::{ScaleDegree, ScaleFamily};

/// The chord a type is built on: its third (or suspension), its fifth, and
/// its seventh or sixth.
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Debug)]
#[repr(u8)]
pub enum ChordBase {
    /// Root, major third, perfect fifth.
    Major,
    /// Root, minor third, perfect fifth.
    Minor,
    /// Root, minor third, diminished fifth.
    Diminished,
    /// Root, major third, augmented fifth.
    Augmented,
    /// Root, major second, perfect fifth.
    Sus2,
    /// Root, perfect fourth, perfect fifth.
    Sus4,
    /// A major triad with a major sixth.
    Major6,
    /// A minor triad with a major sixth.
    Minor6,
    /// A major triad with a major seventh.
    Major7,
    /// A major triad with a minor seventh.
    Dominant7,
    /// A minor triad with a minor seventh.
    Minor7,
    /// A minor triad with a major seventh.
    MinorMajor7,
    /// A diminished triad with a minor seventh.
    HalfDiminished7,
    /// A diminished triad with a diminished seventh.
    Diminished7,
    /// An augmented triad with a minor seventh: 7#5.
    Augmented7,
    /// An augmented triad with a major seventh: maj7#5.
    AugmentedMajor7,
    /// A dominant seventh with a diminished fifth: 7b5.
    Dominant7Flat5,
    /// A sus4 triad with a minor seventh.
    Dominant7Sus4,
    /// Root and fifth only. Not a chord of the table (it has two notes); it
    /// exists so that every [`ChordQuality`] is a chord type.
    Power,
}

impl ChordBase {
    /// Every base, in declaration order.
    pub const ALL: [ChordBase; 19] = [
        ChordBase::Major,
        ChordBase::Minor,
        ChordBase::Diminished,
        ChordBase::Augmented,
        ChordBase::Sus2,
        ChordBase::Sus4,
        ChordBase::Major6,
        ChordBase::Minor6,
        ChordBase::Major7,
        ChordBase::Dominant7,
        ChordBase::Minor7,
        ChordBase::MinorMajor7,
        ChordBase::HalfDiminished7,
        ChordBase::Diminished7,
        ChordBase::Augmented7,
        ChordBase::AugmentedMajor7,
        ChordBase::Dominant7Flat5,
        ChordBase::Dominant7Sus4,
        ChordBase::Power,
    ];

    /// The semitones above the root, ascending.
    #[must_use]
    pub const fn intervals(self) -> &'static [u8] {
        match self {
            ChordBase::Major => &[0, 4, 7],
            ChordBase::Minor => &[0, 3, 7],
            ChordBase::Diminished => &[0, 3, 6],
            ChordBase::Augmented => &[0, 4, 8],
            ChordBase::Sus2 => &[0, 2, 7],
            ChordBase::Sus4 => &[0, 5, 7],
            ChordBase::Major6 => &[0, 4, 7, 9],
            ChordBase::Minor6 => &[0, 3, 7, 9],
            ChordBase::Major7 => &[0, 4, 7, 11],
            ChordBase::Dominant7 => &[0, 4, 7, 10],
            ChordBase::Minor7 => &[0, 3, 7, 10],
            ChordBase::MinorMajor7 => &[0, 3, 7, 11],
            ChordBase::HalfDiminished7 => &[0, 3, 6, 10],
            ChordBase::Diminished7 => &[0, 3, 6, 9],
            ChordBase::Augmented7 => &[0, 4, 8, 10],
            ChordBase::AugmentedMajor7 => &[0, 4, 8, 11],
            ChordBase::Dominant7Flat5 => &[0, 4, 6, 10],
            ChordBase::Dominant7Sus4 => &[0, 5, 7, 10],
            ChordBase::Power => &[0, 7],
        }
    }

    /// The intervals as a set rooted at pitch class 0.
    #[must_use]
    pub const fn interval_set(self) -> PitchSet {
        set_of(self.intervals())
    }

    /// Whether the base has a seventh or a sixth: the chords tensions are
    /// stacked on. A triad takes a ninth only as an added note (`add9`).
    #[must_use]
    pub const fn has_seventh_or_sixth(self) -> bool {
        self.intervals().len() == 4
    }

    /// The family the base belongs to: the column of the chord table.
    #[must_use]
    pub const fn family(self) -> ChordFamily {
        match self {
            ChordBase::Major
            | ChordBase::Major6
            | ChordBase::Major7
            | ChordBase::AugmentedMajor7 => ChordFamily::Major,
            ChordBase::Dominant7 | ChordBase::Augmented7 | ChordBase::Dominant7Flat5 => {
                ChordFamily::Dominant
            }
            ChordBase::Minor | ChordBase::Minor6 | ChordBase::Minor7 => ChordFamily::Minor,
            ChordBase::HalfDiminished7 => ChordFamily::HalfDiminished,
            ChordBase::Diminished | ChordBase::Diminished7 => ChordFamily::Diminished,
            ChordBase::Sus2 | ChordBase::Sus4 | ChordBase::Dominant7Sus4 => ChordFamily::Suspended,
            ChordBase::MinorMajor7 | ChordBase::Augmented | ChordBase::Power => ChordFamily::Other,
        }
    }

    /// Whether the base is a dominant seventh: a minor seventh over a major
    /// third or a fourth (7, 7b5, 7#5, 7sus4). Over these, b9 and b13 are
    /// tensions, not avoid notes ([`crate::ChordScale::avoid_notes`]).
    #[must_use]
    pub const fn is_dominant(self) -> bool {
        matches!(
            self,
            ChordBase::Dominant7
                | ChordBase::Dominant7Flat5
                | ChordBase::Augmented7
                | ChordBase::Dominant7Sus4
        )
    }

    /// Whether the third is minor (and no major third): the numeral is
    /// written in lower case.
    #[must_use]
    pub const fn is_minor(self) -> bool {
        let set = self.interval_set();
        set.contains(Pitch::new(3)) && !set.contains(Pitch::new(4))
    }

    /// The base's notes as degrees: `1 b3 5 b7`. ASCII.
    #[must_use]
    pub const fn degrees(self) -> &'static str {
        match self {
            ChordBase::Major => "1 3 5",
            ChordBase::Minor => "1 b3 5",
            ChordBase::Diminished => "1 b3 b5",
            ChordBase::Augmented => "1 3 #5",
            ChordBase::Sus2 => "1 2 5",
            ChordBase::Sus4 => "1 4 5",
            ChordBase::Major6 => "1 3 5 6",
            ChordBase::Minor6 => "1 b3 5 6",
            ChordBase::Major7 => "1 3 5 7",
            ChordBase::Dominant7 => "1 3 5 b7",
            ChordBase::Minor7 => "1 b3 5 b7",
            ChordBase::MinorMajor7 => "1 b3 5 7",
            ChordBase::HalfDiminished7 => "1 b3 b5 b7",
            ChordBase::Diminished7 => "1 b3 b5 bb7",
            ChordBase::Augmented7 => "1 3 #5 b7",
            ChordBase::AugmentedMajor7 => "1 3 #5 7",
            ChordBase::Dominant7Flat5 => "1 3 b5 b7",
            ChordBase::Dominant7Sus4 => "1 4 5 b7",
            ChordBase::Power => "1 5",
        }
    }

    /// The base written out in words, lowercase.
    #[must_use]
    pub const fn name(self) -> &'static str {
        match self {
            ChordBase::Major => "major",
            ChordBase::Minor => "minor",
            ChordBase::Diminished => "diminished",
            ChordBase::Augmented => "augmented",
            ChordBase::Sus2 => "suspended 2",
            ChordBase::Sus4 => "suspended 4",
            ChordBase::Major6 => "major 6",
            ChordBase::Minor6 => "minor 6",
            ChordBase::Major7 => "major 7",
            ChordBase::Dominant7 => "dominant 7",
            ChordBase::Minor7 => "minor 7",
            ChordBase::MinorMajor7 => "minor-major 7",
            ChordBase::HalfDiminished7 => "half-diminished 7",
            ChordBase::Diminished7 => "diminished 7",
            ChordBase::Augmented7 => "augmented 7",
            ChordBase::AugmentedMajor7 => "augmented major 7",
            ChordBase::Dominant7Flat5 => "dominant 7 flat 5",
            ChordBase::Dominant7Sus4 => "7 suspended 4",
            ChordBase::Power => "power chord",
        }
    }
}

/// The chord families: the columns of the chord table. They are the headings
/// of The New Real Book's chord list (as Hutchinson reproduces it): major,
/// dominant (the altered dominants with it), minor, half-diminished,
/// diminished, suspended, and other.
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Debug)]
#[repr(u8)]
pub enum ChordFamily {
    /// Major third and a major seventh or sixth (maj7#5 too).
    Major,
    /// Major third and a minor seventh, the altered ones included.
    Dominant,
    /// Minor third, perfect fifth, minor seventh or sixth.
    Minor,
    /// Minor third, diminished fifth, minor seventh.
    HalfDiminished,
    /// Minor third, diminished fifth, diminished seventh.
    Diminished,
    /// No third: a second or a fourth instead.
    Suspended,
    /// The rest: the minor-major chords and the augmented triad.
    Other,
}

impl ChordFamily {
    /// Every family, in table order.
    pub const ALL: [ChordFamily; 7] = [
        ChordFamily::Major,
        ChordFamily::Dominant,
        ChordFamily::Minor,
        ChordFamily::HalfDiminished,
        ChordFamily::Diminished,
        ChordFamily::Suspended,
        ChordFamily::Other,
    ];

    /// The family's name.
    #[must_use]
    pub const fn name(self) -> &'static str {
        match self {
            ChordFamily::Major => "Major",
            ChordFamily::Dominant => "Dominant",
            ChordFamily::Minor => "Minor",
            ChordFamily::HalfDiminished => "Half-diminished",
            ChordFamily::Diminished => "Diminished",
            ChordFamily::Suspended => "Suspended",
            ChordFamily::Other => "Other",
        }
    }
}

/// A tension: a note above the base chord, named as a ninth, eleventh or
/// thirteenth.
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Debug)]
#[repr(u8)]
pub enum Tension {
    /// The minor ninth, a semitone above the root.
    FlatNine,
    /// The ninth.
    Nine,
    /// The augmented ninth, the minor third's pitch.
    SharpNine,
    /// The eleventh.
    Eleven,
    /// The augmented eleventh, the tritone.
    SharpEleven,
    /// The minor thirteenth, the augmented fifth's pitch.
    FlatThirteen,
    /// The thirteenth, the sixth's pitch.
    Thirteen,
}

impl Tension {
    /// Every tension, in the order a chord symbol lists them.
    pub const ALL: [Tension; 7] = [
        Tension::FlatNine,
        Tension::Nine,
        Tension::SharpNine,
        Tension::Eleven,
        Tension::SharpEleven,
        Tension::FlatThirteen,
        Tension::Thirteen,
    ];

    /// The pitch class above the root.
    #[must_use]
    pub const fn semitones(self) -> u8 {
        match self {
            Tension::FlatNine => 1,
            Tension::Nine => 2,
            Tension::SharpNine => 3,
            Tension::Eleven => 5,
            Tension::SharpEleven => 6,
            Tension::FlatThirteen => 8,
            Tension::Thirteen => 9,
        }
    }

    /// 9, 11 or 13.
    #[must_use]
    pub const fn number(self) -> u8 {
        match self {
            Tension::FlatNine | Tension::Nine | Tension::SharpNine => 9,
            Tension::Eleven | Tension::SharpEleven => 11,
            Tension::FlatThirteen | Tension::Thirteen => 13,
        }
    }

    /// Flattened (-1), natural (0) or raised (+1).
    #[must_use]
    pub const fn alteration(self) -> i8 {
        match self {
            Tension::FlatNine | Tension::FlatThirteen => -1,
            Tension::Nine | Tension::Eleven | Tension::Thirteen => 0,
            Tension::SharpNine | Tension::SharpEleven => 1,
        }
    }
}

/// A set of tensions.
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Debug, Default)]
pub struct Tensions(u8);

impl Tensions {
    /// No tensions.
    pub const NONE: Tensions = Tensions(0);

    /// The set with `tension` added.
    #[must_use]
    pub const fn with(self, tension: Tension) -> Self {
        Self(self.0 | 1 << tension as u8)
    }

    /// Whether `tension` is in the set.
    #[must_use]
    pub const fn contains(self, tension: Tension) -> bool {
        self.0 & (1 << tension as u8) != 0
    }

    /// How many tensions.
    #[must_use]
    pub const fn len(self) -> u32 {
        self.0.count_ones()
    }

    /// Whether there are none.
    #[must_use]
    pub const fn is_empty(self) -> bool {
        self.0 == 0
    }

    /// The tensions as pitch classes above the root.
    #[must_use]
    pub const fn pitch_set(self) -> PitchSet {
        let mut bits = 0u16;
        let mut i = 0;
        while i < Tension::ALL.len() {
            let t = Tension::ALL[i];
            if self.contains(t) {
                bits |= 1 << t.semitones();
            }
            i += 1;
        }
        PitchSet::from_bits_truncating(bits)
    }

    /// The tensions in symbol order.
    pub fn iter(self) -> impl Iterator<Item = Tension> {
        Tension::ALL.into_iter().filter(move |&t| self.contains(t))
    }

    /// The highest number among them: 9, 11, 13, or 0 for none.
    #[must_use]
    pub fn highest(self) -> u8 {
        self.iter().map(Tension::number).max().unwrap_or(0)
    }
}

/// How far a chord's stack reaches: the rows of the chord table.
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Debug)]
#[repr(u8)]
pub enum ChordRow {
    /// Root, third, fifth.
    Triad,
    /// A sixth or seventh on top.
    Seventh,
    /// Up to a ninth (an `add9` and a `6/9` too).
    Ninth,
    /// Up to an eleventh.
    Eleventh,
    /// Up to a thirteenth.
    Thirteenth,
}

impl ChordRow {
    /// Every row, top to bottom.
    pub const ALL: [ChordRow; 5] = [
        ChordRow::Triad,
        ChordRow::Seventh,
        ChordRow::Ninth,
        ChordRow::Eleventh,
        ChordRow::Thirteenth,
    ];

    /// The row's name.
    #[must_use]
    pub const fn name(self) -> &'static str {
        match self {
            ChordRow::Triad => "Triads",
            ChordRow::Seventh => "Sixths and sevenths",
            ChordRow::Ninth => "Ninths",
            ChordRow::Eleventh => "Elevenths",
            ChordRow::Thirteenth => "Thirteenths",
        }
    }
}

/// A chord type: a base chord and the tensions on it.
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Debug)]
pub struct ChordType {
    base: ChordBase,
    tensions: Tensions,
}

impl ChordType {
    /// `base` with `tensions`, or `None` when a tension doubles a note the
    /// base already has (a #9 on a minor chord is its third).
    #[must_use]
    pub const fn new(base: ChordBase, tensions: Tensions) -> Option<Self> {
        if base
            .interval_set()
            .intersection(tensions.pitch_set())
            .is_empty()
        {
            Some(Self { base, tensions })
        } else {
            None
        }
    }

    /// The base alone.
    #[must_use]
    pub const fn plain(base: ChordBase) -> Self {
        Self {
            base,
            tensions: Tensions::NONE,
        }
    }

    /// The base chord.
    #[must_use]
    pub const fn base(self) -> ChordBase {
        self.base
    }

    /// The tensions on it.
    #[must_use]
    pub const fn tensions(self) -> Tensions {
        self.tensions
    }

    /// Every note as a pitch class above the root.
    #[must_use]
    pub const fn interval_set(self) -> PitchSet {
        self.base.interval_set().union(self.tensions.pitch_set())
    }

    /// How many notes.
    #[must_use]
    pub const fn len(self) -> u32 {
        self.interval_set().len()
    }

    /// Whether the chord has no notes; never true, here for completeness.
    #[must_use]
    pub const fn is_empty(self) -> bool {
        self.interval_set().is_empty()
    }

    /// The tones that make the chord what it is: the base's root, third (or
    /// suspension), seventh or sixth, and altered fifth. Tensions are never
    /// guide tones. See [`ChordQuality::guide_tones`] for the rule.
    #[must_use]
    pub const fn guide_tones(self) -> PitchSet {
        guide_tones_of(self.base.interval_set())
    }

    /// The column of the chord table.
    #[must_use]
    pub const fn family(self) -> ChordFamily {
        self.base.family()
    }

    /// The row of the chord table: how far the stack reaches.
    #[must_use]
    pub fn row(self) -> ChordRow {
        match self.tensions.highest() {
            13 => ChordRow::Thirteenth,
            11 => ChordRow::Eleventh,
            9 => ChordRow::Ninth,
            _ if self.base.has_seventh_or_sixth() => ChordRow::Seventh,
            _ => ChordRow::Triad,
        }
    }

    /// The named quality this type is, if it is one.
    #[must_use]
    pub fn quality(self) -> Option<ChordQuality> {
        ChordQuality::ALL
            .into_iter()
            .find(|q| q.chord_type() == self)
    }
}

impl ChordType {
    /// Every chord type of the grammar, in base order then by tensions.
    ///
    /// A chord type is in the grammar when its tensions follow the
    /// chord-symbol rules (see [`ChordType::follows_symbol_rules`]) and at
    /// least one named scale of five or more notes holds its base on its
    /// root with every one of its tensions as a tension there, not an avoid
    /// note ([`ChordScale::tensions`]). docs/chords.md gives the rules and
    /// their sources.
    pub fn all() -> impl Iterator<Item = ChordType> {
        ChordBase::ALL
            .into_iter()
            .filter(|&b| b != ChordBase::Power)
            .flat_map(|base| {
                let available = Available::over(base);
                (0u8..128).filter_map(move |bits| {
                    let chord = ChordType::new(base, Tensions(bits))?;
                    (chord.follows_symbol_rules() && available.allows(chord.tensions))
                        .then_some(chord)
                })
            })
    }

    /// Whether the tensions are a combination chord symbols write:
    ///
    /// - a triad takes only a ninth (`add9`), and only a major or minor one;
    ///   a sixth chord only a ninth (`6/9`);
    /// - one ninth at most, except b9 with #9; never 13 with b13;
    /// - altered ninths (b9, #9) only on a dominant seventh, and #9 only
    ///   over a major third;
    /// - no natural 11 over a major third;
    /// - 11 implies the 9, and 13 implies a ninth (and, on a minor chord,
    ///   the 11 too); the power chord takes nothing.
    #[must_use]
    pub fn follows_symbol_rules(self) -> bool {
        use Tension as T;
        let t = self.tensions;
        let base = self.base;
        if base == ChordBase::Power {
            return t.is_empty();
        }
        if !base.has_seventh_or_sixth() {
            let triad_ninth = matches!(base, ChordBase::Major | ChordBase::Minor);
            return t.is_empty() || (triad_ninth && t == Tensions::NONE.with(T::Nine));
        }
        if matches!(base, ChordBase::Major6 | ChordBase::Minor6) {
            return t.is_empty() || t == Tensions::NONE.with(T::Nine);
        }
        let ninths = [T::FlatNine, T::Nine, T::SharpNine]
            .iter()
            .filter(|&&n| t.contains(n))
            .count();
        let any_ninth = ninths > 0;
        if ninths > 2 || (ninths == 2 && t.contains(T::Nine)) {
            return false;
        }
        if t.contains(T::Thirteen) && t.contains(T::FlatThirteen) {
            return false;
        }
        let major_third = base.interval_set().contains(Pitch::new(4));
        if (t.contains(T::FlatNine) || t.contains(T::SharpNine)) && !base.is_dominant() {
            return false;
        }
        if t.contains(T::SharpNine) && !major_third {
            return false;
        }
        if t.contains(T::Eleven) && (major_third || !t.contains(T::Nine)) {
            return false;
        }
        if t.contains(T::Thirteen) {
            if !any_ninth {
                return false;
            }
            if base.is_minor() && !t.contains(T::Eleven) {
                return false;
            }
        }
        true
    }

    /// Whether one named scale of five or more notes holds the base on its
    /// root with all the tensions as tensions (not avoid notes).
    #[must_use]
    pub fn is_available(self) -> bool {
        Available::over(self.base).allows(self.tensions)
    }

    /// The suffix written after the root: `m9`, `13b9#11`, `maj7#5`,
    /// `m(maj9)`, `7sus4b9`, `dim7(9)`. ASCII: `b` for flat, `#` for sharp.
    #[must_use]
    pub fn symbol(self) -> DisplayBuffer<32> {
        let mut out = DisplayBuffer::<32>::new();
        let _ = self.write_suffix(&mut out, false);
        out
    }

    /// The chord as a Roman numeral on scale degree `degree`: `ii7`, `bVII9`,
    /// `viiø7`, `V7b9`, `Isus4`. Case follows the third (lower for a minor
    /// third); the degree's accidental is relative to the major scale on the
    /// tonic, as the scale's degrees are spelled.
    #[must_use]
    pub fn numeral(self, degree: ScaleDegree) -> DisplayBuffer<40> {
        let mut out = DisplayBuffer::<40>::new();
        let mark = if degree.accidental() < 0 { "b" } else { "#" };
        for _ in 0..degree.accidental().unsigned_abs().min(2) {
            let _ = out.write_str(mark);
        }
        let upper = ["I", "II", "III", "IV", "V", "VI", "VII"];
        let lower = ["i", "ii", "iii", "iv", "v", "vi", "vii"];
        let index = (degree.number().clamp(1, 7) - 1) as usize;
        let _ = out.write_str(if self.base.is_minor() {
            lower[index]
        } else {
            upper[index]
        });
        let _ = self.write_suffix(&mut out, true);
        out
    }

    /// The chord's notes as degrees above the root, chord tones then
    /// tensions: `1 b3 5 b7 9`, `1 3 b5 b7 b9 #9`, `1 b3 b5 bb7`. ASCII.
    #[must_use]
    pub fn degrees(self) -> DisplayBuffer<40> {
        let mut out = DisplayBuffer::<40>::new();
        let _ = out.write_str(self.base.degrees());
        for tension in self.tensions.iter() {
            let _ = out.write_str(" ");
            let _ = write_tension(&mut out, tension);
        }
        out
    }

    /// The notes stacked in register: the base within the octave, each
    /// tension an octave up (a 9 is 14 semitones above the root).
    pub fn stacking(self) -> impl Iterator<Item = u8> {
        let base = self.base.intervals().iter().copied();
        base.chain(self.tensions.iter().map(|t| t.semitones() + 12))
    }

    /// The chord written out in words: `"dominant 7 with flat 9, sharp 11"`.
    #[must_use]
    pub fn name(self) -> DisplayBuffer<96> {
        let mut out = DisplayBuffer::<96>::new();
        let _ = out.write_str(self.base.name());
        let mut first = true;
        for tension in self.tensions.iter() {
            let _ = out.write_str(if first { " with " } else { ", " });
            first = false;
            let _ = out.write_str(match tension.alteration() {
                -1 => "flat ",
                1 => "sharp ",
                _ => "",
            });
            let _ = write!(out, "{}", tension.number());
        }
        out
    }

    /// Writes the symbol's suffix, or with `numeral` the numeral's (the
    /// quality the numeral's case already shows left out).
    fn write_suffix(self, out: &mut impl Write, numeral: bool) -> fmt::Result {
        use ChordBase as B;
        use Tension as T;
        let t = self.tensions;
        // The highest natural extension takes the seventh's place: 9, 11, 13.
        let top = if t.contains(T::Thirteen) {
            13
        } else if t.contains(T::Eleven) {
            11
        } else if t.contains(T::Nine) {
            9
        } else {
            7
        };
        let absorbed = |tension: Tension| tension.alteration() == 0 && tension.number() <= top;
        let (before, after) = match (self.base, numeral) {
            (B::Major, false) => (if t.is_empty() { "" } else { "add" }, ""),
            (B::Minor, false) => (if t.is_empty() { "m" } else { "madd" }, ""),
            (B::Major | B::Minor, true) => (if t.is_empty() { "" } else { "add" }, ""),
            (B::Diminished, false) => ("dim", ""),
            (B::Diminished, true) => ("°", ""),
            (B::Augmented, false) => ("aug", ""),
            (B::Augmented, true) => ("+", ""),
            (B::Sus2, _) => ("sus2", ""),
            (B::Sus4, _) => ("sus4", ""),
            (B::Major6, _) => ("6", ""),
            (B::Minor6, false) => ("m6", ""),
            (B::Minor6, true) => ("6", ""),
            (B::Power, _) => ("5", ""),
            (B::Major7, _) => ("maj", ""),
            (B::Dominant7, _) => ("", ""),
            (B::Minor7, false) => ("m", ""),
            (B::Minor7, true) => ("", ""),
            (B::MinorMajor7, false) => ("m(maj", ")"),
            (B::MinorMajor7, true) => ("maj", ""),
            (B::HalfDiminished7, false) => ("m", "b5"),
            (B::HalfDiminished7, true) => ("ø", ""),
            (B::Diminished7, false) => ("dim", ""),
            (B::Diminished7, true) => ("°", ""),
            (B::Augmented7, false) => ("", "#5"),
            (B::Augmented7, true) => ("+", ""),
            (B::AugmentedMajor7, false) => ("maj", "#5"),
            (B::AugmentedMajor7, true) => ("+maj", ""),
            (B::Dominant7Flat5, _) => ("", "b5"),
            (B::Dominant7Sus4, _) => ("", "sus4"),
        };
        out.write_str(before)?;
        match self.base {
            // A triad's added ninth, a sixth's 6/9: no seventh to replace.
            B::Major | B::Minor => {
                if !t.is_empty() {
                    out.write_str("9")?;
                }
                return Ok(());
            }
            B::Major6 | B::Minor6 => {
                if !t.is_empty() {
                    out.write_str("/9")?;
                }
                return Ok(());
            }
            B::Diminished | B::Augmented | B::Sus2 | B::Sus4 | B::Power => return Ok(()),
            // A diminished seventh keeps its 7; tensions go in parentheses.
            B::Diminished7 => {
                out.write_str("7")?;
                if !t.is_empty() {
                    out.write_str("(")?;
                    for (i, tension) in t.iter().enumerate() {
                        if i > 0 {
                            out.write_str(",")?;
                        }
                        write_tension(out, tension)?;
                    }
                    out.write_str(")")?;
                }
                return Ok(());
            }
            _ => {}
        }
        write!(out, "{top}")?;
        out.write_str(after)?;
        for tension in t.iter().filter(|&tension| !absorbed(tension)) {
            write_tension(out, tension)?;
        }
        Ok(())
    }
}

/// The tension sets the named scales of five or more notes offer over one
/// base: one per scale that holds it, without repeats.
struct Available {
    sets: [PitchSet; 128],
    len: usize,
}

impl Available {
    fn over(base: ChordBase) -> Self {
        let mut found = Self {
            sets: [PitchSet::EMPTY; 128],
            len: 0,
        };
        let chord = ChordType::plain(base);
        let pairs = ScaleFamily::all(5)
            .flat_map(ScaleFamily::modes)
            .filter(|scale| scale.name().is_some())
            .filter_map(|scale| ChordScale::from_chord(scale, chord));
        for pair in pairs {
            let set = pair.tensions();
            if !found.sets[..found.len].contains(&set) && found.len < found.sets.len() {
                found.sets[found.len] = set;
                found.len += 1;
            }
        }
        found
    }

    fn allows(&self, tensions: Tensions) -> bool {
        let wanted = tensions.pitch_set();
        self.sets[..self.len]
            .iter()
            .any(|&set| wanted.is_subset(set))
    }
}

fn write_tension(out: &mut impl Write, tension: Tension) -> fmt::Result {
    out.write_str(match tension.alteration() {
        -1 => "b",
        1 => "#",
        _ => "",
    })?;
    write!(out, "{}", tension.number())
}

impl ChordQuality {
    /// The quality as a chord type: its base and tensions.
    ///
    /// ```
    /// use music_core::{ChordBase, ChordQuality, Tension};
    ///
    /// let nine = ChordQuality::Dominant9.chord_type();
    /// assert_eq!(nine.base(), ChordBase::Dominant7);
    /// assert!(nine.tensions().contains(Tension::Nine));
    /// assert_eq!(nine.interval_set(), ChordQuality::Dominant9.interval_set());
    /// ```
    #[must_use]
    pub const fn chord_type(self) -> ChordType {
        use ChordBase as B;
        use Tension as T;
        let (base, tensions) = match self {
            ChordQuality::Major => (B::Major, Tensions::NONE),
            ChordQuality::Minor => (B::Minor, Tensions::NONE),
            ChordQuality::Diminished => (B::Diminished, Tensions::NONE),
            ChordQuality::Augmented => (B::Augmented, Tensions::NONE),
            ChordQuality::Sus2 => (B::Sus2, Tensions::NONE),
            ChordQuality::Sus4 => (B::Sus4, Tensions::NONE),
            ChordQuality::Major7 => (B::Major7, Tensions::NONE),
            ChordQuality::Dominant7 => (B::Dominant7, Tensions::NONE),
            ChordQuality::Minor7 => (B::Minor7, Tensions::NONE),
            ChordQuality::MinorMajor7 => (B::MinorMajor7, Tensions::NONE),
            ChordQuality::HalfDiminished7 => (B::HalfDiminished7, Tensions::NONE),
            ChordQuality::Diminished7 => (B::Diminished7, Tensions::NONE),
            ChordQuality::Sixth => (B::Major6, Tensions::NONE),
            ChordQuality::MinorSixth => (B::Minor6, Tensions::NONE),
            ChordQuality::Add9 => (B::Major, Tensions::NONE.with(T::Nine)),
            ChordQuality::MinorAdd9 => (B::Minor, Tensions::NONE.with(T::Nine)),
            ChordQuality::SixNine => (B::Major6, Tensions::NONE.with(T::Nine)),
            ChordQuality::Dominant9 => (B::Dominant7, Tensions::NONE.with(T::Nine)),
            ChordQuality::Major9 => (B::Major7, Tensions::NONE.with(T::Nine)),
            ChordQuality::Minor9 => (B::Minor7, Tensions::NONE.with(T::Nine)),
            ChordQuality::Dominant11 => {
                (B::Dominant7, Tensions::NONE.with(T::Nine).with(T::Eleven))
            }
            ChordQuality::Minor11 => (B::Minor7, Tensions::NONE.with(T::Nine).with(T::Eleven)),
            ChordQuality::Dominant13 => {
                (B::Dominant7, Tensions::NONE.with(T::Nine).with(T::Thirteen))
            }
            ChordQuality::Major13 => (B::Major7, Tensions::NONE.with(T::Nine).with(T::Thirteen)),
            ChordQuality::SevenFlatFive => (B::Dominant7Flat5, Tensions::NONE),
            ChordQuality::SevenSharpFive => (B::Augmented7, Tensions::NONE),
            ChordQuality::SevenFlatNine => (B::Dominant7, Tensions::NONE.with(T::FlatNine)),
            ChordQuality::SevenSharpNine => (B::Dominant7, Tensions::NONE.with(T::SharpNine)),
            ChordQuality::SevenSharpEleven => (B::Dominant7, Tensions::NONE.with(T::SharpEleven)),
            ChordQuality::Fifth => (B::Power, Tensions::NONE),
            ChordQuality::Dominant7Sus4 => (B::Dominant7Sus4, Tensions::NONE),
        };
        ChordType { base, tensions }
    }
}

const fn set_of(offsets: &[u8]) -> PitchSet {
    let mut bits = 0u16;
    let mut i = 0;
    while i < offsets.len() {
        bits |= 1 << offsets[i];
        i += 1;
    }
    PitchSet::from_bits_truncating(bits)
}

/// The guide tones of a chord with these notes: root, third (or
/// suspension), seventh or sixth, and an altered fifth. The rule is
/// [`ChordQuality::guide_tones`]'s.
pub(crate) const fn guide_tones_of(set: PitchSet) -> PitchSet {
    let mut guide = PitchSet::EMPTY.insert(Pitch::C);

    let third = if set.contains(Pitch::new(4)) {
        4
    } else if set.contains(Pitch::new(3)) {
        3
    } else if set.contains(Pitch::new(5)) {
        5
    } else if set.contains(Pitch::new(2)) {
        2
    } else {
        0
    };
    guide = guide.insert(Pitch::new(third));

    let seventh = set.contains(Pitch::new(10)) || set.contains(Pitch::new(11));
    if set.contains(Pitch::new(10)) {
        guide = guide.insert(Pitch::new(10));
    }
    if set.contains(Pitch::new(11)) {
        guide = guide.insert(Pitch::new(11));
    }
    if !seventh && set.contains(Pitch::new(9)) {
        guide = guide.insert(Pitch::new(9));
    }

    if !set.contains(Pitch::new(7)) {
        if set.contains(Pitch::new(6)) {
            guide = guide.insert(Pitch::new(6));
        }
        if set.contains(Pitch::new(8)) {
            guide = guide.insert(Pitch::new(8));
        }
    }
    guide
}

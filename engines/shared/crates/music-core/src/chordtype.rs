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

use crate::chord::ChordQuality;
use crate::pitch::Pitch;
use crate::pitchset::PitchSet;

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
            ChordBase::Major | ChordBase::Major6 | ChordBase::Major7 => ChordFamily::Major,
            ChordBase::Dominant7 | ChordBase::Augmented7 | ChordBase::Dominant7Flat5 => {
                ChordFamily::Dominant
            }
            ChordBase::Minor | ChordBase::Minor6 | ChordBase::Minor7 => ChordFamily::Minor,
            ChordBase::MinorMajor7 => ChordFamily::MinorMajor,
            ChordBase::HalfDiminished7 => ChordFamily::HalfDiminished,
            ChordBase::Diminished | ChordBase::Diminished7 => ChordFamily::Diminished,
            ChordBase::Augmented | ChordBase::AugmentedMajor7 => ChordFamily::Augmented,
            ChordBase::Sus2 | ChordBase::Sus4 | ChordBase::Dominant7Sus4 | ChordBase::Power => {
                ChordFamily::Suspended
            }
        }
    }
}

/// The chord families: the columns of the chord table, by the quality of the
/// third, fifth and seventh.
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Debug)]
#[repr(u8)]
pub enum ChordFamily {
    /// Major third, perfect fifth, major seventh or sixth.
    Major,
    /// Major third, minor seventh.
    Dominant,
    /// Minor third, perfect fifth, minor seventh or sixth.
    Minor,
    /// Minor third, perfect fifth, major seventh.
    MinorMajor,
    /// Minor third, diminished fifth, minor seventh.
    HalfDiminished,
    /// Minor third, diminished fifth, diminished seventh.
    Diminished,
    /// Major third, augmented fifth.
    Augmented,
    /// No third: a second or a fourth instead.
    Suspended,
}

impl ChordFamily {
    /// Every family, in table order.
    pub const ALL: [ChordFamily; 8] = [
        ChordFamily::Major,
        ChordFamily::Dominant,
        ChordFamily::Minor,
        ChordFamily::MinorMajor,
        ChordFamily::HalfDiminished,
        ChordFamily::Diminished,
        ChordFamily::Augmented,
        ChordFamily::Suspended,
    ];

    /// The family's name.
    #[must_use]
    pub const fn name(self) -> &'static str {
        match self {
            ChordFamily::Major => "Major",
            ChordFamily::Dominant => "Dominant",
            ChordFamily::Minor => "Minor",
            ChordFamily::MinorMajor => "Minor-major",
            ChordFamily::HalfDiminished => "Half-diminished",
            ChordFamily::Diminished => "Diminished",
            ChordFamily::Augmented => "Augmented",
            ChordFamily::Suspended => "Suspended",
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

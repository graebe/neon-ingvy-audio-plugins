//! Chords: a root plus any set of pitches, and the vocabulary for naming them.

use core::fmt::{self, Write as _};
use core::str::FromStr;

use crate::notes::Notes;
use crate::pitch::{Interval, Pitch, Spelling, parse_pitch_prefix};
use crate::pitchset::PitchSet;
use crate::{DisplayBuffer, Harmony, ParseError, padded};

/// A named chord quality: a recognisable pattern of intervals above a root.
///
/// This is not how a [`Chord`] stores itself. A chord holds a root and an
/// arbitrary set of pitches, and a quality is what [`Chord::quality`] gives back
/// when that set happens to match a name. Plenty of real chords match none of
/// these, and they are still perfectly good chords.
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Debug, Default)]
#[repr(u8)]
pub enum ChordQuality {
    /// Root, major third, perfect fifth.
    #[default]
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
    /// Major triad plus a major seventh.
    Major7,
    /// Major triad plus a minor seventh.
    Dominant7,
    /// Minor triad plus a minor seventh.
    Minor7,
    /// Minor triad plus a major seventh.
    MinorMajor7,
    /// Diminished triad plus a minor seventh.
    HalfDiminished7,
    /// Diminished triad plus a diminished seventh.
    Diminished7,
    /// Major triad plus a major sixth.
    Sixth,
    /// Minor triad plus a major sixth.
    MinorSixth,
    /// Major triad plus a ninth, with no seventh.
    Add9,
    /// Minor triad plus a ninth, with no seventh.
    MinorAdd9,
    /// Major triad with both a sixth and a ninth.
    SixNine,
    /// Dominant seventh plus a ninth.
    Dominant9,
    /// Major seventh plus a ninth.
    Major9,
    /// Minor seventh plus a ninth.
    Minor9,
    /// Dominant ninth plus an eleventh.
    Dominant11,
    /// Minor ninth plus an eleventh.
    Minor11,
    /// Dominant ninth plus a thirteenth.
    Dominant13,
    /// Major ninth plus a thirteenth.
    Major13,
    /// Dominant seventh with a flattened fifth.
    SevenFlatFive,
    /// Dominant seventh with a raised fifth.
    SevenSharpFive,
    /// Dominant seventh with a flattened ninth.
    SevenFlatNine,
    /// Dominant seventh with a raised ninth.
    SevenSharpNine,
    /// Dominant seventh with a raised eleventh.
    SevenSharpEleven,
}

impl ChordQuality {
    /// Every quality, in declaration order.
    pub const ALL: [ChordQuality; 29] = [
        ChordQuality::Major,
        ChordQuality::Minor,
        ChordQuality::Diminished,
        ChordQuality::Augmented,
        ChordQuality::Sus2,
        ChordQuality::Sus4,
        ChordQuality::Major7,
        ChordQuality::Dominant7,
        ChordQuality::Minor7,
        ChordQuality::MinorMajor7,
        ChordQuality::HalfDiminished7,
        ChordQuality::Diminished7,
        ChordQuality::Sixth,
        ChordQuality::MinorSixth,
        ChordQuality::Add9,
        ChordQuality::MinorAdd9,
        ChordQuality::SixNine,
        ChordQuality::Dominant9,
        ChordQuality::Major9,
        ChordQuality::Minor9,
        ChordQuality::Dominant11,
        ChordQuality::Minor11,
        ChordQuality::Dominant13,
        ChordQuality::Major13,
        ChordQuality::SevenFlatFive,
        ChordQuality::SevenSharpFive,
        ChordQuality::SevenFlatNine,
        ChordQuality::SevenSharpNine,
        ChordQuality::SevenSharpEleven,
    ];

    /// The semitone offsets from the root, ascending.
    #[inline]
    #[must_use]
    pub const fn intervals(self) -> &'static [u8] {
        match self {
            ChordQuality::Major => &[0, 4, 7],
            ChordQuality::Minor => &[0, 3, 7],
            ChordQuality::Diminished => &[0, 3, 6],
            ChordQuality::Augmented => &[0, 4, 8],
            ChordQuality::Sus2 => &[0, 2, 7],
            ChordQuality::Sus4 => &[0, 5, 7],
            ChordQuality::Major7 => &[0, 4, 7, 11],
            ChordQuality::Dominant7 => &[0, 4, 7, 10],
            ChordQuality::Minor7 => &[0, 3, 7, 10],
            ChordQuality::MinorMajor7 => &[0, 3, 7, 11],
            ChordQuality::HalfDiminished7 => &[0, 3, 6, 10],
            ChordQuality::Diminished7 => &[0, 3, 6, 9],
            ChordQuality::Sixth => &[0, 4, 7, 9],
            ChordQuality::MinorSixth => &[0, 3, 7, 9],
            ChordQuality::Add9 => &[0, 2, 4, 7],
            ChordQuality::MinorAdd9 => &[0, 2, 3, 7],
            ChordQuality::SixNine => &[0, 2, 4, 7, 9],
            ChordQuality::Dominant9 => &[0, 2, 4, 7, 10],
            ChordQuality::Major9 => &[0, 2, 4, 7, 11],
            ChordQuality::Minor9 => &[0, 2, 3, 7, 10],
            ChordQuality::Dominant11 => &[0, 2, 4, 5, 7, 10],
            ChordQuality::Minor11 => &[0, 2, 3, 5, 7, 10],
            ChordQuality::Dominant13 => &[0, 2, 4, 7, 9, 10],
            ChordQuality::Major13 => &[0, 2, 4, 7, 9, 11],
            ChordQuality::SevenFlatFive => &[0, 4, 6, 10],
            ChordQuality::SevenSharpFive => &[0, 4, 8, 10],
            ChordQuality::SevenFlatNine => &[0, 1, 4, 7, 10],
            ChordQuality::SevenSharpNine => &[0, 3, 4, 7, 10],
            ChordQuality::SevenSharpEleven => &[0, 4, 6, 7, 10],
        }
    }

    /// The offsets as a set rooted at pitch class 0.
    ///
    /// No two qualities share one, which is what makes [`Chord::quality`]
    /// unambiguous once the root is known. A test asserts it.
    #[inline]
    #[must_use]
    pub const fn interval_set(self) -> PitchSet {
        let offsets = self.intervals();
        let mut bits = 0u16;
        let mut i = 0;
        while i < offsets.len() {
            bits |= 1 << offsets[i];
            i += 1;
        }
        PitchSet::from_bits_truncating(bits)
    }

    /// How many notes this quality has, three to six.
    #[inline]
    #[must_use]
    pub const fn size(self) -> usize {
        self.intervals().len()
    }

    /// How strongly to prefer this reading when a set of pitches has several.
    ///
    /// Lower wins. Plain triads first, then altered triads and suspensions,
    /// then sevenths, sixths, added notes, extensions and finally the altered
    /// dominants. This is a heuristic, not a fact of music: see
    /// [`PitchSet::identify`].
    #[inline]
    #[must_use]
    pub const fn rank(self) -> u8 {
        match self {
            ChordQuality::Major | ChordQuality::Minor => 0,
            ChordQuality::Diminished | ChordQuality::Augmented => 1,
            ChordQuality::Sus2 | ChordQuality::Sus4 => 2,
            ChordQuality::Major7
            | ChordQuality::Dominant7
            | ChordQuality::Minor7
            | ChordQuality::MinorMajor7
            | ChordQuality::HalfDiminished7
            | ChordQuality::Diminished7 => 3,
            ChordQuality::Sixth | ChordQuality::MinorSixth => 4,
            ChordQuality::Add9 | ChordQuality::MinorAdd9 => 5,
            ChordQuality::SixNine
            | ChordQuality::Dominant9
            | ChordQuality::Major9
            | ChordQuality::Minor9 => 6,
            ChordQuality::Dominant11
            | ChordQuality::Minor11
            | ChordQuality::Dominant13
            | ChordQuality::Major13 => 7,
            ChordQuality::SevenFlatFive
            | ChordQuality::SevenSharpFive
            | ChordQuality::SevenFlatNine
            | ChordQuality::SevenSharpNine
            | ChordQuality::SevenSharpEleven => 8,
        }
    }

    /// The suffix written after the root, such as `m7b5`.
    #[inline]
    #[must_use]
    pub const fn symbol(self) -> &'static str {
        match self {
            ChordQuality::Major => "",
            ChordQuality::Minor => "m",
            ChordQuality::Diminished => "dim",
            ChordQuality::Augmented => "aug",
            ChordQuality::Sus2 => "sus2",
            ChordQuality::Sus4 => "sus4",
            ChordQuality::Major7 => "maj7",
            ChordQuality::Dominant7 => "7",
            ChordQuality::Minor7 => "m7",
            ChordQuality::MinorMajor7 => "mmaj7",
            ChordQuality::HalfDiminished7 => "m7b5",
            ChordQuality::Diminished7 => "dim7",
            ChordQuality::Sixth => "6",
            ChordQuality::MinorSixth => "m6",
            ChordQuality::Add9 => "add9",
            ChordQuality::MinorAdd9 => "madd9",
            ChordQuality::SixNine => "6/9",
            ChordQuality::Dominant9 => "9",
            ChordQuality::Major9 => "maj9",
            ChordQuality::Minor9 => "m9",
            ChordQuality::Dominant11 => "11",
            ChordQuality::Minor11 => "m11",
            ChordQuality::Dominant13 => "13",
            ChordQuality::Major13 => "maj13",
            ChordQuality::SevenFlatFive => "7b5",
            ChordQuality::SevenSharpFive => "7#5",
            ChordQuality::SevenFlatNine => "7b9",
            ChordQuality::SevenSharpNine => "7#9",
            ChordQuality::SevenSharpEleven => "7#11",
        }
    }

    /// Recognises a quality from a set of offsets already rooted at 0.
    #[inline]
    #[must_use]
    pub const fn from_interval_set(intervals: PitchSet) -> Option<Self> {
        let mut i = 0;
        while i < Self::ALL.len() {
            let candidate = Self::ALL[i];
            if candidate.interval_set().bits() == intervals.bits() {
                return Some(candidate);
            }
            i += 1;
        }
        None
    }
}

/// Spellings accepted when parsing, checked against the whole suffix.
const SPELLINGS: [(&str, ChordQuality); 38] = [
    ("", ChordQuality::Major),
    ("maj", ChordQuality::Major),
    ("M", ChordQuality::Major),
    ("m", ChordQuality::Minor),
    ("min", ChordQuality::Minor),
    ("-", ChordQuality::Minor),
    ("dim", ChordQuality::Diminished),
    ("o", ChordQuality::Diminished),
    ("aug", ChordQuality::Augmented),
    ("+", ChordQuality::Augmented),
    ("sus2", ChordQuality::Sus2),
    ("sus4", ChordQuality::Sus4),
    ("sus", ChordQuality::Sus4),
    ("maj7", ChordQuality::Major7),
    ("7", ChordQuality::Dominant7),
    ("dom7", ChordQuality::Dominant7),
    ("m7", ChordQuality::Minor7),
    ("min7", ChordQuality::Minor7),
    ("mmaj7", ChordQuality::MinorMajor7),
    ("m7b5", ChordQuality::HalfDiminished7),
    ("dim7", ChordQuality::Diminished7),
    ("6", ChordQuality::Sixth),
    ("m6", ChordQuality::MinorSixth),
    ("add9", ChordQuality::Add9),
    ("madd9", ChordQuality::MinorAdd9),
    ("m(add9)", ChordQuality::MinorAdd9),
    ("6/9", ChordQuality::SixNine),
    ("69", ChordQuality::SixNine),
    ("9", ChordQuality::Dominant9),
    ("maj9", ChordQuality::Major9),
    ("m9", ChordQuality::Minor9),
    ("11", ChordQuality::Dominant11),
    ("m11", ChordQuality::Minor11),
    ("13", ChordQuality::Dominant13),
    ("maj13", ChordQuality::Major13),
    ("7b5", ChordQuality::SevenFlatFive),
    ("7#5", ChordQuality::SevenSharpFive),
    ("7b9", ChordQuality::SevenFlatNine),
];

/// Extra spellings that do not fit the fixed-size table above.
const EXTRA_SPELLINGS: [(&str, ChordQuality); 2] = [
    ("7#9", ChordQuality::SevenSharpNine),
    ("7#11", ChordQuality::SevenSharpEleven),
];

/// A chord: a root, plus any set of pitches.
///
/// Not limited to a fixed vocabulary. A chord can hold anything from one note
/// to all twelve, so a thirteenth, a cluster or a chord nobody has named is
/// just as representable as a triad. [`Chord::quality`] tells you which name it
/// answers to, if any.
///
/// The root is part of the chord, not a guess about it. The same pitches with a
/// different root are a different chord, which is exactly what distinguishes a
/// C6 from an A minor 7.
///
/// ```
/// use music_core::{Chord, ChordQuality, Pitch};
///
/// let dm7 = Chord::min7(Pitch::D);
/// assert_eq!(dm7.to_string(), "Dm7");
/// assert_eq!(dm7.quality(), Some(ChordQuality::Minor7));
/// assert_eq!(dm7.size(), 4);
///
/// // Anything at all is a chord, named or not.
/// let cluster = Chord::new(Pitch::C, dm7.pitches().insert(Pitch::C_SHARP));
/// assert_eq!(cluster.quality(), None);
/// ```
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
pub struct Chord {
    root: Pitch,
    pitches: PitchSet,
}

macro_rules! chord_constructors {
    ($($(#[$doc:meta])* $name:ident => $quality:ident),* $(,)?) => {
        $(
            $(#[$doc])*
            #[inline]
            #[must_use]
            pub const fn $name(root: Pitch) -> Self {
                Self::from_quality(root, ChordQuality::$quality)
            }
        )*
    };
}

impl Chord {
    /// Builds a chord from a root and a set of pitches.
    ///
    /// Total. If the set omits the root it is inserted, because a chord
    /// contains its root by definition here.
    #[inline]
    #[must_use]
    pub const fn new(root: Pitch, pitches: PitchSet) -> Self {
        Self {
            root,
            pitches: pitches.insert(root),
        }
    }

    /// Builds a chord from a root and a named quality.
    #[inline]
    #[must_use]
    pub const fn from_quality(root: Pitch, quality: ChordQuality) -> Self {
        Self {
            root,
            pitches: quality
                .interval_set()
                .transpose(Interval::new(root.value() as i16)),
        }
    }

    chord_constructors! {
        /// A major triad on `root`.
        major => Major,
        /// A minor triad on `root`.
        minor => Minor,
        /// A diminished triad on `root`.
        dim => Diminished,
        /// An augmented triad on `root`.
        aug => Augmented,
        /// A suspended-second chord on `root`.
        sus2 => Sus2,
        /// A suspended-fourth chord on `root`.
        sus4 => Sus4,
        /// A major seventh chord on `root`.
        maj7 => Major7,
        /// A dominant seventh chord on `root`.
        dom7 => Dominant7,
        /// A minor seventh chord on `root`.
        min7 => Minor7,
        /// A minor-major seventh chord on `root`.
        min_maj7 => MinorMajor7,
        /// A half-diminished seventh chord on `root`.
        half_dim7 => HalfDiminished7,
        /// A fully diminished seventh chord on `root`.
        dim7 => Diminished7,
        /// A major sixth chord on `root`.
        sixth => Sixth,
        /// A minor sixth chord on `root`.
        min6 => MinorSixth,
        /// A major triad with an added ninth, and no seventh.
        add9 => Add9,
        /// A minor triad with an added ninth, and no seventh.
        min_add9 => MinorAdd9,
        /// A six-nine chord on `root`.
        six_nine => SixNine,
        /// A dominant ninth chord on `root`.
        dom9 => Dominant9,
        /// A major ninth chord on `root`.
        maj9 => Major9,
        /// A minor ninth chord on `root`.
        min9 => Minor9,
        /// A dominant eleventh chord on `root`.
        dom11 => Dominant11,
        /// A minor eleventh chord on `root`.
        min11 => Minor11,
        /// A dominant thirteenth chord on `root`.
        dom13 => Dominant13,
        /// A major thirteenth chord on `root`.
        maj13 => Major13,
        /// A dominant seventh with a flattened fifth.
        dom7_flat5 => SevenFlatFive,
        /// A dominant seventh with a raised fifth.
        dom7_sharp5 => SevenSharpFive,
        /// A dominant seventh with a flattened ninth.
        dom7_flat9 => SevenFlatNine,
        /// A dominant seventh with a raised ninth.
        dom7_sharp9 => SevenSharpNine,
        /// A dominant seventh with a raised eleventh.
        dom7_sharp11 => SevenSharpEleven,
    }

    /// The root.
    #[inline]
    #[must_use]
    pub const fn root(self) -> Pitch {
        self.root
    }

    /// The pitches, including the root, with no order and no repeats.
    #[inline]
    #[must_use]
    pub const fn pitches(self) -> PitchSet {
        self.pitches
    }

    /// The pitches rotated so the root sits at 0.
    ///
    /// This is the shape of the chord with its root forgotten, and the key that
    /// [`Chord::quality`] looks up.
    #[inline]
    #[must_use]
    pub const fn intervals(self) -> PitchSet {
        self.pitches
            .transpose(Interval::new(-(self.root.value() as i16)))
    }

    /// The name this chord answers to, if any.
    ///
    /// Unambiguous, because the root is already known. A set of pitches without
    /// a root can have several readings: see [`PitchSet::interpretations`].
    ///
    /// ```
    /// use music_core::{Chord, ChordQuality, Pitch};
    ///
    /// // The same four pitches, read two ways.
    /// let pitches = Chord::sixth(Pitch::C).pitches();
    ///
    /// assert_eq!(Chord::new(Pitch::C, pitches).quality(), Some(ChordQuality::Sixth));
    /// assert_eq!(Chord::new(Pitch::A, pitches).quality(), Some(ChordQuality::Minor7));
    /// ```
    #[inline]
    #[must_use]
    pub const fn quality(self) -> Option<ChordQuality> {
        ChordQuality::from_interval_set(self.intervals())
    }

    /// How many notes this chord has, 1 to 12.
    #[inline]
    #[must_use]
    pub const fn size(self) -> usize {
        self.pitches.len() as usize
    }

    /// Whether a pitch is in this chord.
    #[inline]
    #[must_use]
    pub const fn contains(self, pitch: Pitch) -> bool {
        self.pitches.contains(pitch)
    }

    /// Moves the chord by an interval, keeping its shape.
    #[inline]
    #[must_use]
    pub const fn transpose(self, by: Interval) -> Self {
        Self {
            root: self.root.transpose(by),
            pitches: self.pitches.transpose(by),
        }
    }

    /// Adds a pitch, keeping the root.
    #[inline]
    #[must_use]
    pub const fn with(self, pitch: Pitch) -> Self {
        Self {
            root: self.root,
            pitches: self.pitches.insert(pitch),
        }
    }

    /// Removes a pitch. Removing the root is refused, since a chord contains
    /// its root by definition.
    #[inline]
    #[must_use]
    pub const fn without(self, pitch: Pitch) -> Self {
        if pitch.value() == self.root.value() {
            return self;
        }
        Self {
            root: self.root,
            pitches: self.pitches.remove(pitch),
        }
    }

    /// Gives the chord a register, stacked upward from the root at `octave`.
    #[must_use]
    pub fn voice(self, octave: i16) -> Notes {
        let mut out = Notes::EMPTY;
        let root_midi = self.root.at(octave).midi();
        for offset in self.intervals().iter() {
            let _ = out.push(crate::voiced::Note::from_midi(
                root_midi + i16::from(offset.value()),
            ));
        }
        out
    }
}

impl Harmony for Chord {
    #[inline]
    fn pitch_set(&self) -> PitchSet {
        self.pitches
    }
}

impl fmt::Display for Chord {
    /// A named chord prints its symbol, `Cmaj7`. Anything else prints the root
    /// and its semitone offsets, `C[0,1,4,6]`. Both forms parse back.
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if let Some(quality) = self.quality() {
            return padded!(
                f,
                16,
                "{}{}",
                self.root.name(Spelling::Sharps),
                quality.symbol()
            );
        }

        let mut buffer = DisplayBuffer::<64>::new();
        let _ = write!(buffer, "{}[", self.root.name(Spelling::Sharps));
        for (index, offset) in self.intervals().iter().enumerate() {
            let separator = if index > 0 { "," } else { "" };
            let _ = write!(buffer, "{}{}", separator, offset.value());
        }
        let _ = write!(buffer, "]");
        f.pad(buffer.as_str())
    }
}

impl fmt::Debug for Chord {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt::Display::fmt(self, f)
    }
}

impl FromStr for Chord {
    type Err = ParseError;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let (root, used) = parse_pitch_prefix(s.as_bytes())?;
        let suffix = &s[used..];

        // The bracket form, for chords with no name.
        if let Some(body) = suffix.strip_prefix('[').and_then(|r| r.strip_suffix(']')) {
            let mut pitches = PitchSet::EMPTY;
            for piece in body.split(',') {
                let offset: i32 = piece
                    .trim()
                    .parse()
                    .map_err(|_| ParseError::InvalidQuality)?;
                pitches = pitches.insert(Pitch::new(offset + i32::from(root.value())));
            }
            return Ok(Chord::new(root, pitches));
        }

        for (spelling, quality) in SPELLINGS {
            if suffix == spelling {
                return Ok(Chord::from_quality(root, quality));
            }
        }
        for (spelling, quality) in EXTRA_SPELLINGS {
            if suffix == spelling {
                return Ok(Chord::from_quality(root, quality));
            }
        }
        Err(ParseError::InvalidQuality)
    }
}

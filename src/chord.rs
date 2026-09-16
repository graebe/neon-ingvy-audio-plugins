//! Named chord symbols, including the qualities the PLR group cannot touch.

use core::fmt::{self, Write as _};
use core::str::FromStr;

use crate::notes::Notes;
use crate::pitch::{Interval, Pitch, Spelling, parse_pitch_prefix};
use crate::pitchset::PitchSet;
use crate::{Harmony, ParseError, padded};

/// The quality of a named [`Chord`].
///
/// Covers the ordinary chord vocabulary: triads, suspended chords and the six
/// common sevenths. The `neo-riemann` crate narrows major and minor into its
/// own `Triad`, because only those two are in the PLR group's domain.
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
}

impl ChordQuality {
    /// Every quality, in declaration order.
    pub const ALL: [ChordQuality; 12] = [
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
        }
    }

    /// The offsets as a bit mask rooted at pitch class 0.
    #[inline]
    #[must_use]
    pub const fn mask(self) -> u16 {
        let offsets = self.intervals();
        let mut bits = 0u16;
        let mut i = 0;
        while i < offsets.len() {
            bits |= 1 << offsets[i];
            i += 1;
        }
        bits
    }

    /// How many notes this quality has, three or four.
    #[inline]
    #[must_use]
    pub const fn size(self) -> usize {
        self.intervals().len()
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
        }
    }
}

/// Spellings accepted when parsing, longest-first within each quality.
const SPELLINGS: [(&str, ChordQuality); 21] = [
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
];

/// A named chord: a root plus a quality, in two bytes.
///
/// The general chord type: any root with any of the twelve qualities. The
/// `neo-riemann` crate converts the major and minor ones into its own `Triad`
/// when it needs the transformation group, and rejects the rest.
///
/// ```
/// use music_core::{Chord, Pitch};
///
/// let dm7 = Chord::min7(Pitch::D);
/// assert_eq!(dm7.to_string(), "Dm7");
/// assert_eq!(dm7.pitch_set().len(), 4);
/// ```
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
pub struct Chord {
    /// The root.
    pub root: Pitch,
    /// The quality.
    pub quality: ChordQuality,
}

macro_rules! chord_constructors {
    ($($(#[$doc:meta])* $name:ident => $quality:ident),* $(,)?) => {
        $(
            $(#[$doc])*
            #[inline]
            #[must_use]
            pub const fn $name(root: Pitch) -> Self {
                Self { root, quality: ChordQuality::$quality }
            }
        )*
    };
}

impl Chord {
    /// Builds a chord from a root and a quality.
    #[inline]
    #[must_use]
    pub const fn new(root: Pitch, quality: ChordQuality) -> Self {
        Self { root, quality }
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
    }

    /// The pitch classes, losing which one is the root.
    #[inline]
    #[must_use]
    pub const fn pitch_set(self) -> PitchSet {
        PitchSet::from_bits_truncating(rotate12(self.quality.mask(), self.root.value()))
    }

    /// How many notes this chord has.
    #[inline]
    #[must_use]
    pub const fn size(self) -> usize {
        self.quality.size()
    }

    /// Whether a pitch class is in this chord.
    #[inline]
    #[must_use]
    pub const fn contains(self, pitch: Pitch) -> bool {
        self.pitch_set().contains(pitch)
    }

    /// Moves the chord by an interval, keeping its quality.
    #[inline]
    #[must_use]
    pub const fn transpose(self, by: Interval) -> Self {
        Self {
            root: self.root.transpose(by),
            quality: self.quality,
        }
    }

    /// Gives the chord a register, in close root position at `octave`.
    ///
    /// Produces a [`Notes`] list rather than a [`crate::Voiced`], because a
    /// chord's note count varies and so it has no fixed voice count.
    #[must_use]
    pub fn voice(self, octave: i16) -> Notes {
        let mut out = Notes::EMPTY;
        let root_midi = self.root.at(octave).midi();
        for offset in self.quality.intervals() {
            let _ = out.push(crate::voiced::Note::from_midi(root_midi + *offset as i16));
        }
        out
    }
}

/// Rotates a 12-bit mask upward by `steps`, wrapping at the octave.
#[inline]
const fn rotate12(bits: u16, steps: u8) -> u16 {
    let n = (steps % 12) as u32;
    let wide = bits as u32;
    (((wide << n) | (wide >> (12 - n))) & 0x0FFF) as u16
}

impl Harmony for Chord {
    #[inline]
    fn pitch_set(&self) -> PitchSet {
        Chord::pitch_set(*self)
    }
}

impl fmt::Display for Chord {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        padded!(
            f,
            16,
            "{}{}",
            self.root.name(Spelling::Sharps),
            self.quality.symbol()
        )
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
        for (spelling, quality) in SPELLINGS {
            if suffix == spelling {
                return Ok(Chord { root, quality });
            }
        }
        Err(ParseError::InvalidQuality)
    }
}

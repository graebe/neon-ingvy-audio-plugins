// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! Keys: a tonic and one of the seven church modes, and what a chord is in one.
//!
//! A [`Key`] answers three questions a chord display asks: which pitch classes
//! belong ([`Key::pitch_set`]), how to spell the black keys
//! ([`Key::spelling`]), and what a chord is called relative to the tonic
//! ([`Key::degree_of`]). Where it sits on the circle of fifths is its
//! [`Key::signature`].

use core::fmt::{self, Write as _};

use crate::DisplayBuffer;
use crate::chord::{Chord, ChordQuality};
use crate::name::NoteName;
use crate::pitch::{Pitch, Spelling};
use crate::pitchset::PitchSet;

/// One of the seven diatonic modes, the rotations of the major scale.
///
/// Each is named by where it starts on the white keys: Ionian on C is the
/// major scale, Aeolian on A the natural minor.
///
/// ```
/// use music_core::Mode;
///
/// assert_eq!(Mode::Dorian.steps(), [0, 2, 3, 5, 7, 9, 10]);
/// assert_eq!(Mode::Aeolian.to_string(), "Aeolian");
/// ```
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
#[repr(u8)]
pub enum Mode {
    /// The major scale.
    #[default]
    Ionian,
    /// Minor with a major sixth.
    Dorian,
    /// Minor with a minor second.
    Phrygian,
    /// Major with an augmented fourth.
    Lydian,
    /// Major with a minor seventh.
    Mixolydian,
    /// The natural minor scale.
    Aeolian,
    /// Diminished fifth and minor second: the one mode without a perfect fifth.
    Locrian,
}

/// The major scale's steps, which every mode rotates.
const IONIAN: [u8; 7] = [0, 2, 4, 5, 7, 9, 11];

impl Mode {
    /// All seven, in the order of the major scale's degrees they start on.
    pub const ALL: [Mode; 7] = [
        Mode::Ionian,
        Mode::Dorian,
        Mode::Phrygian,
        Mode::Lydian,
        Mode::Mixolydian,
        Mode::Aeolian,
        Mode::Locrian,
    ];

    /// The mode by its position in [`Mode::ALL`], `None` above 6.
    #[must_use]
    pub const fn from_index(index: u8) -> Option<Self> {
        if index < 7 {
            Some(Self::ALL[index as usize])
        } else {
            None
        }
    }

    /// Its position in [`Mode::ALL`], 0 to 6.
    #[inline]
    #[must_use]
    pub const fn index(self) -> u8 {
        self as u8
    }

    /// The seven scale steps in semitones above the tonic, ascending from 0.
    #[must_use]
    pub const fn steps(self) -> [u8; 7] {
        let start = self.index() as usize;
        let base = IONIAN[start];
        let mut out = [0u8; 7];
        let mut i = 0;
        while i < 7 {
            let step = IONIAN[(start + i) % 7];
            out[i] = (step + 12 - base) % 12;
            i += 1;
        }
        out
    }

    /// How far above the tonic of its parent major scale this mode starts.
    ///
    /// D Dorian has 2: its notes are C major's, starting on D.
    #[inline]
    #[must_use]
    pub const fn above_parent(self) -> u8 {
        IONIAN[self.index() as usize]
    }

    /// The mode's name, capitalised as a proper noun.
    #[must_use]
    pub const fn name(self) -> &'static str {
        match self {
            Mode::Ionian => "Ionian",
            Mode::Dorian => "Dorian",
            Mode::Phrygian => "Phrygian",
            Mode::Lydian => "Lydian",
            Mode::Mixolydian => "Mixolydian",
            Mode::Aeolian => "Aeolian",
            Mode::Locrian => "Locrian",
        }
    }
}

impl fmt::Display for Mode {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.pad(self.name())
    }
}

impl fmt::Debug for Mode {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(self.name())
    }
}

/// A key: a tonic and a mode.
///
/// Two bytes, `Copy`, and total: every tonic in every mode is a key.
///
/// ```
/// use music_core::{Chord, Key, Mode, Pitch, Spelling};
///
/// let d_dorian = Key::new(Pitch::D, Mode::Dorian);
///
/// assert_eq!(d_dorian.parent(), Pitch::C);          // C major's notes
/// assert_eq!(d_dorian.signature(), 0);              // so no sharps or flats
/// assert!(d_dorian.pitch_set().contains(Pitch::B)); // the major sixth
///
/// let c_aeolian = Key::new(Pitch::C, Mode::Aeolian);
/// assert_eq!(c_aeolian.signature(), -3);            // Eb major's three flats
/// assert_eq!(c_aeolian.spelling(), Spelling::Flats);
///
/// let c_major = Key::new(Pitch::C, Mode::Ionian);
/// assert_eq!(c_major.degree_of(Chord::min7(Pitch::A)).to_string(), "vi7");
/// assert_eq!(c_major.degree_of(Chord::major(Pitch::B_FLAT)).to_string(), "bVII");
/// ```
#[derive(Clone, Copy, PartialEq, Eq, Hash, Default)]
pub struct Key {
    tonic: Pitch,
    mode: Mode,
}

impl Key {
    /// The key of `mode` on `tonic`.
    #[inline]
    #[must_use]
    pub const fn new(tonic: Pitch, mode: Mode) -> Self {
        Self { tonic, mode }
    }

    /// The tonic.
    #[inline]
    #[must_use]
    pub const fn tonic(self) -> Pitch {
        self.tonic
    }

    /// The mode.
    #[inline]
    #[must_use]
    pub const fn mode(self) -> Mode {
        self.mode
    }

    /// The tonic of the major scale whose notes this key uses.
    #[inline]
    #[must_use]
    pub const fn parent(self) -> Pitch {
        Pitch::new(self.tonic.value() as i32 - self.mode.above_parent() as i32)
    }

    /// The seven pitch classes of the key.
    #[must_use]
    pub const fn pitch_set(self) -> PitchSet {
        let steps = self.mode.steps();
        let mut set = PitchSet::EMPTY;
        let mut i = 0;
        while i < 7 {
            set = set.insert(Pitch::new(self.tonic.value() as i32 + steps[i] as i32));
            i += 1;
        }
        set
    }

    /// The key signature, as sharps (positive) or flats (negative), -5 to 6.
    ///
    /// It is the parent major scale's, so it is also the key's place on the
    /// circle of fifths. Six is written as six sharps (F# major), never six
    /// flats: a pitch class does not know which of the two was meant.
    #[must_use]
    pub const fn signature(self) -> i8 {
        let fifths = self.parent().fifths() as i8;
        if fifths > 6 { fifths - 12 } else { fifths }
    }

    /// How this key spells its black keys: flats in a flat key, sharps
    /// otherwise.
    #[inline]
    #[must_use]
    pub const fn spelling(self) -> Spelling {
        if self.signature() < 0 {
            Spelling::Flats
        } else {
            Spelling::Sharps
        }
    }

    /// What `chord` is in this key, as a roman numeral on its root.
    ///
    /// Total: a chord whose root lies outside the key still gets a degree, its
    /// root marked flat or sharp against the mode's own step. A root in the key
    /// is never altered. Otherwise the step is the one the raised or lowered
    /// note names in the parallel major — so the leading tone in A Aeolian is
    /// `#vii°`, and B flat in C Ionian is `bVII` — with the one convention that
    /// the tritone above the tonic is a raised fourth, never a lowered fifth.
    #[must_use]
    pub fn degree_of(self, chord: Chord) -> Degree {
        let (step, accidental) = self.step_of(chord.root());
        Degree {
            step,
            accidental,
            chord,
        }
    }

    /// How a score in this key writes `pitch`: its letter and accidentals.
    ///
    /// The seven notes of the key take the scale's letters, one each, upward
    /// from the tonic's -- so F# major has its E sharp, not an F. Every other
    /// pitch takes the letter of the step [`Key::degree_of`] reads it as: the
    /// lowered seventh of C major is B flat, never A sharp, and the leading
    /// tone of A minor is G sharp.
    ///
    /// ```
    /// use music_core::{Key, Mode, Pitch};
    ///
    /// let c_major = Key::new(Pitch::C, Mode::Ionian);
    /// assert_eq!(c_major.name_of(Pitch::B_FLAT).to_string(), "Bb");
    /// assert_eq!(c_major.name_of(Pitch::F_SHARP).to_string(), "F#");
    ///
    /// let f_sharp_major = Key::new(Pitch::F_SHARP, Mode::Ionian);
    /// assert_eq!(f_sharp_major.name_of(Pitch::F).to_string(), "E#");
    /// ```
    #[must_use]
    pub const fn name_of(self, pitch: Pitch) -> NoteName {
        let tonic = NoteName::of(self.tonic, self.spelling());
        let (step, _) = self.step_of(pitch);
        let letter = tonic.letter().up(step as i32);
        let from_natural = (pitch.value() as i32 - letter.natural().value() as i32 + 18) % 12 - 6;
        NoteName::new(letter, from_natural as i8)
    }

    /// The scale step `pitch` is read as, 0 to 6, and how it is altered
    /// against the mode's own note on that step: -1, 0 or +1.
    const fn step_of(self, pitch: Pitch) -> (u8, i8) {
        let above = self.tonic.interval_to(pitch).semitones() as u8;
        let steps = self.mode.steps();
        let mut step = 0;
        while step < 7 {
            if steps[step] == above {
                return (step as u8, 0);
            }
            step += 1;
        }
        let step = CHROMATIC_STEP[above as usize] as usize;
        (step as u8, above as i8 - steps[step] as i8)
    }
}

/// For each semitone above the tonic, the scale step it is read as when it
/// is not in the key: flats, as `bII`, `bIII`, `bVI` and `bVII` are written,
/// except the tritone, which is the raised fourth.
const CHROMATIC_STEP: [u8; 12] = [0, 1, 1, 2, 2, 3, 3, 4, 5, 5, 6, 6];

impl fmt::Display for Key {
    /// The tonic in the key's own spelling, then the mode: `"Eb Dorian"`.
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        crate::padded!(
            f,
            24,
            "{} {}",
            self.tonic.name(self.spelling()),
            self.mode.name()
        )
    }
}

impl fmt::Debug for Key {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt::Display::fmt(self, f)
    }
}

/// A chord read as a roman numeral in a key.
///
/// Comes from [`Key::degree_of`]. It prints the way chord charts write it: the
/// numeral's case follows the chord's third (upper for major, lower for minor
/// and diminished), an alteration of the root comes first, and the quality
/// follows as a suffix — `V7`, `ii7`, `viiø7`, `bVII`, `#iv°`.
///
/// The bass is not part of it. A slash chord is the same degree as its root
/// position; the chord symbol says what is underneath.
#[derive(Clone, Copy, PartialEq, Eq, Hash)]
pub struct Degree {
    step: u8,
    accidental: i8,
    chord: Chord,
}

impl Degree {
    /// The scale step of the root, 1 to 7.
    #[inline]
    #[must_use]
    pub const fn step(self) -> u8 {
        self.step + 1
    }

    /// How the root is altered against that step: -1 flat, 0, or +1 sharp.
    #[inline]
    #[must_use]
    pub const fn accidental(self) -> i8 {
        self.accidental
    }

    /// The chord this degree names.
    #[inline]
    #[must_use]
    pub const fn chord(self) -> Chord {
        self.chord
    }
}

/// Upper- and lowercase numerals for the seven steps.
const NUMERALS: [[&str; 7]; 2] = [
    ["I", "II", "III", "IV", "V", "VI", "VII"],
    ["i", "ii", "iii", "iv", "v", "vi", "vii"],
];

/// Whether a quality's numeral is written lowercase, and the suffix after it.
const fn numeral_form(quality: ChordQuality) -> (bool, &'static str) {
    match quality {
        ChordQuality::Major => (false, ""),
        ChordQuality::Minor => (true, ""),
        ChordQuality::Diminished => (true, "°"),
        ChordQuality::Augmented => (false, "+"),
        ChordQuality::Sus2 => (false, "sus2"),
        ChordQuality::Sus4 => (false, "sus4"),
        ChordQuality::Major7 => (false, "maj7"),
        ChordQuality::Dominant7 => (false, "7"),
        ChordQuality::Minor7 => (true, "7"),
        ChordQuality::MinorMajor7 => (true, "maj7"),
        ChordQuality::HalfDiminished7 => (true, "ø7"),
        ChordQuality::Diminished7 => (true, "°7"),
        ChordQuality::Sixth => (false, "6"),
        ChordQuality::MinorSixth => (true, "6"),
        ChordQuality::Add9 => (false, "add9"),
        ChordQuality::MinorAdd9 => (true, "add9"),
        ChordQuality::SixNine => (false, "6/9"),
        ChordQuality::Dominant9 => (false, "9"),
        ChordQuality::Major9 => (false, "maj9"),
        ChordQuality::Minor9 => (true, "9"),
        ChordQuality::Dominant11 => (false, "11"),
        ChordQuality::Minor11 => (true, "11"),
        ChordQuality::Dominant13 => (false, "13"),
        ChordQuality::Major13 => (false, "maj13"),
        ChordQuality::SevenFlatFive => (false, "7b5"),
        ChordQuality::SevenSharpFive => (false, "7#5"),
        ChordQuality::SevenFlatNine => (false, "7b9"),
        ChordQuality::SevenSharpNine => (false, "7#9"),
        ChordQuality::SevenSharpEleven => (false, "7#11"),
        ChordQuality::Fifth => (false, "5"),
        ChordQuality::Dominant7Sus4 => (false, "7sus4"),
    }
}

impl fmt::Display for Degree {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let (lower, suffix) = match self.chord.quality() {
            Some(quality) => numeral_form(quality),
            // An unnamed shape: lowercase when it has a minor third and no
            // major one, which is all the numeral can say about it.
            None => {
                let intervals = self.chord.intervals();
                let minor = intervals.contains(Pitch::new(3)) && !intervals.contains(Pitch::new(4));
                (minor, "")
            }
        };
        let accidental = match self.accidental {
            a if a < 0 => "b",
            a if a > 0 => "#",
            _ => "",
        };
        let mut buffer = DisplayBuffer::<24>::new();
        let _ = write!(
            buffer,
            "{}{}{}",
            accidental, NUMERALS[lower as usize][self.step as usize], suffix
        );
        f.pad(buffer.as_str())
    }
}

impl fmt::Debug for Degree {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt::Display::fmt(self, f)
    }
}

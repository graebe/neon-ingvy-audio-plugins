// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! Pitch primitives: pitch classes, intervals, and interval classes.

use core::fmt::{self, Write as _};
use core::ops::{Add, Neg, Sub};
use core::str::FromStr;

use crate::voiced::Note;
use crate::{ParseError, padded};

/// Pitches per octave in twelve-tone equal temperament.
pub const EDO: u8 = 12;

const SHARP_NAMES: [&str; 12] = [
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
];
const FLAT_NAMES: [&str; 12] = [
    "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B",
];

/// Which accidental to prefer when naming a [`Pitch`].
///
/// This crate makes no enharmonic distinction: C sharp and D flat are one
/// value. Spelling is a display choice, applied at the point of naming.
#[derive(Clone, Copy, PartialEq, Eq, Hash, Debug, Default)]
pub enum Spelling {
    /// Name the black keys with sharps: `C#`, `D#`, `F#`, `G#`, `A#`.
    #[default]
    Sharps,
    /// Name the black keys with flats: `Db`, `Eb`, `Gb`, `Ab`, `Bb`.
    Flats,
}

/// A pitch class: a note name with the octave discarded.
///
/// Every C is the same C. There are exactly twelve values, numbered 0 to 11
/// upward from C, and one fits in a single byte. Neo-Riemannian theory lives
/// entirely at this level.
///
/// Give one a register with [`Pitch::at`] to get a [`Note`].
///
/// ```
/// use music_core::{Interval, Pitch};
///
/// assert_eq!(Pitch::C.transpose(Interval::MAJOR_THIRD), Pitch::E);
/// assert_eq!(Pitch::C_SHARP, Pitch::D_FLAT);   // one value, two names
/// ```
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
#[repr(transparent)]
pub struct Pitch(u8);

impl Pitch {
    /// C, pitch class 0.
    pub const C: Self = Self(0);
    /// C sharp, pitch class 1. Identical to [`Pitch::D_FLAT`].
    pub const C_SHARP: Self = Self(1);
    /// D flat, pitch class 1. Identical to [`Pitch::C_SHARP`].
    pub const D_FLAT: Self = Self(1);
    /// D, pitch class 2.
    pub const D: Self = Self(2);
    /// D sharp, pitch class 3. Identical to [`Pitch::E_FLAT`].
    pub const D_SHARP: Self = Self(3);
    /// E flat, pitch class 3. Identical to [`Pitch::D_SHARP`].
    pub const E_FLAT: Self = Self(3);
    /// E, pitch class 4.
    pub const E: Self = Self(4);
    /// F, pitch class 5.
    pub const F: Self = Self(5);
    /// F sharp, pitch class 6. Identical to [`Pitch::G_FLAT`].
    pub const F_SHARP: Self = Self(6);
    /// G flat, pitch class 6. Identical to [`Pitch::F_SHARP`].
    pub const G_FLAT: Self = Self(6);
    /// G, pitch class 7.
    pub const G: Self = Self(7);
    /// G sharp, pitch class 8. Identical to [`Pitch::A_FLAT`].
    pub const G_SHARP: Self = Self(8);
    /// A flat, pitch class 8. Identical to [`Pitch::G_SHARP`].
    pub const A_FLAT: Self = Self(8);
    /// A, pitch class 9.
    pub const A: Self = Self(9);
    /// A sharp, pitch class 10. Identical to [`Pitch::B_FLAT`].
    pub const A_SHARP: Self = Self(10);
    /// B flat, pitch class 10. Identical to [`Pitch::A_SHARP`].
    pub const B_FLAT: Self = Self(10);
    /// B, pitch class 11.
    pub const B: Self = Self(11);

    /// All twelve pitch classes in ascending order from C.
    pub const ALL: [Self; 12] = [
        Self(0),
        Self(1),
        Self(2),
        Self(3),
        Self(4),
        Self(5),
        Self(6),
        Self(7),
        Self(8),
        Self(9),
        Self(10),
        Self(11),
    ];

    /// Builds a pitch class from any integer, wrapping into 0 to 11.
    ///
    /// This cannot fail, and there is nothing to validate: a pitch class *is*
    /// an integer modulo twelve, so twelve is C and `-1` is B. If you are
    /// checking a byte that claims to encode a pitch class, range-check it
    /// yourself before calling; a value outside 0 to 11 means your input is
    /// wrong, which is a different question from what note it denotes.
    ///
    /// ```
    /// use music_core::Pitch;
    ///
    /// assert_eq!(Pitch::new(0), Pitch::C);
    /// assert_eq!(Pitch::new(13), Pitch::C_SHARP);
    /// assert_eq!(Pitch::new(-1), Pitch::B);
    /// ```
    #[inline]
    #[must_use]
    pub const fn new(value: i32) -> Self {
        let m = value % 12;
        Self(if m < 0 { (m + 12) as u8 } else { m as u8 })
    }

    /// The raw value, 0 to 11 upward from C.
    #[inline]
    #[must_use]
    pub const fn value(self) -> u8 {
        self.0
    }

    /// Moves this pitch class by an interval, wrapping within the octave.
    #[inline]
    #[must_use]
    pub const fn transpose(self, by: Interval) -> Self {
        Self::new(self.0 as i32 + by.0 as i32)
    }

    /// Reflects this pitch class about an inversion axis: `x` becomes `axis - x`.
    ///
    /// Together with [`Pitch::transpose`] this generates the T/I group, which
    /// the PLR group commutes with.
    #[inline]
    #[must_use]
    pub const fn invert(self, axis: i32) -> Self {
        Self::new(axis - self.0 as i32)
    }

    /// The ascending interval from this pitch class to another, 0 to 11.
    ///
    /// Directed and asymmetric: C to G is a fifth, G to C is a fourth.
    #[inline]
    #[must_use]
    pub const fn interval_to(self, other: Self) -> Interval {
        let d = other.0 as i16 - self.0 as i16;
        Interval(if d < 0 { d + 12 } else { d })
    }

    /// The shortest distance to another pitch class, 0 to 6.
    ///
    /// Symmetric, and the natural measure of voice-leading smoothness.
    #[inline]
    #[must_use]
    pub const fn distance_to(self, other: Self) -> IntervalClass {
        let d = self.interval_to(other).0;
        IntervalClass(if d > 6 { (12 - d) as u8 } else { d as u8 })
    }

    /// Gives this pitch class a register, producing a [`Note`].
    ///
    /// Octaves follow scientific pitch notation, so `Pitch::C.at(4)` is middle
    /// C at MIDI 60.
    #[inline]
    #[must_use]
    pub const fn at(self, octave: i16) -> Note {
        Note::from_parts(self, octave)
    }

    /// How many fifths above C this pitch class is, 0 to 11: its place on
    /// the circle of fifths, clockwise from C at the top.
    ///
    /// ```
    /// use music_core::Pitch;
    ///
    /// assert_eq!(Pitch::G.fifths(), 1);
    /// assert_eq!(Pitch::F.fifths(), 11);    // a fifth below C
    /// ```
    #[inline]
    #[must_use]
    pub const fn fifths(self) -> u8 {
        (self.0 * 7) % 12
    }

    /// The pitch class `count` fifths above C: the inverse of
    /// [`Pitch::fifths`].
    #[inline]
    #[must_use]
    pub const fn from_fifths(count: i32) -> Self {
        Self::new(count * 7)
    }

    /// Names this pitch class with the requested accidentals.
    #[inline]
    #[must_use]
    pub const fn name(self, spelling: Spelling) -> &'static str {
        match spelling {
            Spelling::Sharps => SHARP_NAMES[self.0 as usize],
            Spelling::Flats => FLAT_NAMES[self.0 as usize],
        }
    }
}

/// Reads a pitch class from the front of `bytes`, returning it and how many
/// bytes it consumed.
///
/// Public because chord-symbol parsers all start this way and then continue
/// where it stops, including ones in other crates.
///
/// ```
/// use music_core::{parse_pitch_prefix, Pitch};
///
/// let (root, used) = parse_pitch_prefix(b"Ebm7").unwrap();
/// assert_eq!(root, Pitch::E_FLAT);
/// assert_eq!(used, 2);
/// ```
pub fn parse_pitch_prefix(bytes: &[u8]) -> Result<(Pitch, usize), ParseError> {
    if bytes.is_empty() {
        return Err(ParseError::Empty);
    }
    let base: i32 = match bytes[0] {
        b'C' | b'c' => 0,
        b'D' | b'd' => 2,
        b'E' | b'e' => 4,
        b'F' | b'f' => 5,
        b'G' | b'g' => 7,
        b'A' | b'a' => 9,
        b'B' => 11,
        _ => return Err(ParseError::InvalidPitch),
    };
    // A leading lowercase `b` is the note B; every later `b` is a flat.
    let mut used = 1;
    let mut accidental = 0i32;
    while used < bytes.len() {
        match bytes[used] {
            b'#' => accidental += 1,
            b'b' => accidental -= 1,
            _ => break,
        }
        used += 1;
    }
    Ok((Pitch::new(base + accidental), used))
}

impl FromStr for Pitch {
    type Err = ParseError;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let bytes = s.as_bytes();
        let (pitch, used) = parse_pitch_prefix(bytes)?;
        if used == bytes.len() {
            Ok(pitch)
        } else {
            Err(ParseError::Trailing)
        }
    }
}

impl fmt::Display for Pitch {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        // `pad`, not `write_str`, so that `{:>4}` and friends are honoured.
        f.pad(self.name(Spelling::Sharps))
    }
}

impl fmt::Debug for Pitch {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(self.name(Spelling::Sharps))
    }
}

impl Add<Interval> for Pitch {
    type Output = Pitch;
    #[inline]
    fn add(self, rhs: Interval) -> Pitch {
        self.transpose(rhs)
    }
}

impl Sub<Interval> for Pitch {
    type Output = Pitch;
    #[inline]
    fn sub(self, rhs: Interval) -> Pitch {
        self.transpose(-rhs)
    }
}

impl Sub<Pitch> for Pitch {
    type Output = Interval;

    /// The shortest signed path from `rhs` up or down to `self`.
    ///
    /// This is the signed companion of [`Pitch::distance_to`]: the magnitude of
    /// the result always equals that distance. It is therefore antisymmetric,
    /// `a - b == -(b - a)`, with exactly one exception. At the tritone both
    /// directions are equally short, so the tie resolves upward and both
    /// differences come out as `+6`. That is the same tie-break the voice
    /// leading in [`crate::Triad`] uses.
    ///
    /// Note this is *not* [`Pitch::interval_to`], which always reports the
    /// ascending distance and so could not satisfy the negation law.
    ///
    /// ```
    /// use music_core::Pitch;
    ///
    /// assert_eq!((Pitch::G - Pitch::C).semitones(), -5);   // down a fourth
    /// assert_eq!((Pitch::C - Pitch::G).semitones(), 5);
    /// assert_eq!((Pitch::E - Pitch::C).semitones(), 4);
    /// ```
    #[inline]
    fn sub(self, rhs: Pitch) -> Interval {
        let ascending = rhs.interval_to(self).0;
        Interval(if ascending > 6 {
            ascending - 12
        } else {
            ascending
        })
    }
}

/// A directed distance in semitones, which may exceed an octave.
///
/// Unlike [`Pitch`] this is not wrapped, so it can express "up two octaves and
/// a fifth" as well as "down a semitone".
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
#[repr(transparent)]
pub struct Interval(i16);

impl Interval {
    /// Zero semitones.
    pub const UNISON: Self = Self(0);
    /// One semitone.
    pub const MINOR_SECOND: Self = Self(1);
    /// Two semitones.
    pub const MAJOR_SECOND: Self = Self(2);
    /// Three semitones.
    pub const MINOR_THIRD: Self = Self(3);
    /// Four semitones.
    pub const MAJOR_THIRD: Self = Self(4);
    /// Five semitones.
    pub const PERFECT_FOURTH: Self = Self(5);
    /// Six semitones.
    pub const TRITONE: Self = Self(6);
    /// Seven semitones.
    pub const PERFECT_FIFTH: Self = Self(7);
    /// Eight semitones.
    pub const MINOR_SIXTH: Self = Self(8);
    /// Nine semitones.
    pub const MAJOR_SIXTH: Self = Self(9);
    /// Ten semitones.
    pub const MINOR_SEVENTH: Self = Self(10);
    /// Eleven semitones.
    pub const MAJOR_SEVENTH: Self = Self(11);
    /// Twelve semitones.
    pub const OCTAVE: Self = Self(12);

    /// Builds an interval from a signed semitone count.
    #[inline]
    #[must_use]
    pub const fn new(semitones: i16) -> Self {
        Self(semitones)
    }

    /// The signed semitone count.
    #[inline]
    #[must_use]
    pub const fn semitones(self) -> i16 {
        self.0
    }

    /// The magnitude in semitones, discarding direction.
    #[inline]
    #[must_use]
    pub const fn abs(self) -> u16 {
        self.0.unsigned_abs()
    }

    /// Folds this interval into an [`IntervalClass`], 0 to 6.
    #[inline]
    #[must_use]
    pub const fn class(self) -> IntervalClass {
        IntervalClass::new(self.0 as i32)
    }

    /// Whole octaves beyond the simple interval, ignoring direction.
    ///
    /// A major tenth is a major third and one octave more. An octave itself is
    /// simple, so it has none: every size from a unison to an octave counts
    /// zero, and the count goes up after each further octave.
    ///
    /// ```
    /// use music_core::Interval;
    ///
    /// assert_eq!(Interval::new(16).compound_octaves(), 1);   // a major tenth
    /// assert_eq!(Interval::OCTAVE.compound_octaves(), 0);
    /// ```
    #[inline]
    #[must_use]
    pub const fn compound_octaves(self) -> u16 {
        let size = self.abs();
        if size == 0 { 0 } else { (size - 1) / 12 }
    }

    /// The name of the simple interval, ignoring direction: `"major 3rd"`.
    ///
    /// A compound interval is named by what is left once its
    /// [`Interval::compound_octaves`] are taken away, so a major tenth reads
    /// `"major 3rd"`. The tritone is called that, since a pitch class cannot
    /// say whether it was an augmented fourth or a diminished fifth.
    ///
    /// ```
    /// use music_core::Interval;
    ///
    /// assert_eq!(Interval::MINOR_THIRD.name(), "minor 3rd");
    /// assert_eq!(Interval::new(-7).name(), "perfect 5th");
    /// assert_eq!(Interval::new(24).name(), "octave");
    /// ```
    #[must_use]
    pub const fn name(self) -> &'static str {
        let size = self.abs();
        let simple = if size == 0 { 0 } else { (size - 1) % 12 + 1 };
        INTERVAL_NAMES[simple as usize]
    }
}

/// The simple intervals' names, unison to octave.
const INTERVAL_NAMES: [&str; 13] = [
    "unison",
    "minor 2nd",
    "major 2nd",
    "minor 3rd",
    "major 3rd",
    "perfect 4th",
    "tritone",
    "perfect 5th",
    "minor 6th",
    "major 6th",
    "minor 7th",
    "major 7th",
    "octave",
];

impl Neg for Interval {
    type Output = Interval;
    #[inline]
    fn neg(self) -> Interval {
        Interval(-self.0)
    }
}

impl Add for Interval {
    type Output = Interval;
    #[inline]
    fn add(self, rhs: Interval) -> Interval {
        Interval(self.0 + rhs.0)
    }
}

impl Sub for Interval {
    type Output = Interval;
    #[inline]
    fn sub(self, rhs: Interval) -> Interval {
        Interval(self.0 - rhs.0)
    }
}

impl fmt::Display for Interval {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        padded!(f, 8, "{:+}", self.0)
    }
}

impl fmt::Debug for Interval {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{:+}", self.0)
    }
}

/// An undirected interval folded into 0 to 6.
///
/// A fifth up and a fourth down are the same interval class, which is why this
/// is the right measure for how far a voice has to move.
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
#[repr(transparent)]
pub struct IntervalClass(pub(crate) u8);

impl IntervalClass {
    /// Folds any semitone count into an interval class, 0 to 6.
    ///
    /// Cannot fail. Direction and octaves are both discarded, so a fifth up and
    /// a fourth down give the same answer.
    ///
    /// ```
    /// use music_core::IntervalClass;
    ///
    /// assert_eq!(IntervalClass::new(7).value(), 5);
    /// assert_eq!(IntervalClass::new(-5).value(), 5);
    /// ```
    #[inline]
    #[must_use]
    pub const fn new(semitones: i32) -> Self {
        let m = semitones % 12;
        let m = if m < 0 { m + 12 } else { m };
        Self(if m > 6 { (12 - m) as u8 } else { m as u8 })
    }

    /// The raw value, 0 to 6.
    #[inline]
    #[must_use]
    pub const fn value(self) -> u8 {
        self.0
    }
}

impl fmt::Display for IntervalClass {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        padded!(f, 8, "ic{}", self.0)
    }
}

impl fmt::Debug for IntervalClass {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "ic{}", self.0)
    }
}

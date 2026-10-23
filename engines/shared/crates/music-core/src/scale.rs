// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! Scales as shapes above a root, their modes, and the families they form.
//!
//! A [`Scale`] is a set of pitch classes measured from its root, so the root
//! is always pitch class 0. The major scale is one scale whether it starts on
//! C or on F sharp; [`Scale::on`] puts it on a tonic.
//!
//! Starting the same notes on another of its degrees gives a **mode**:
//! [`Scale::mode`]. All the modes of a scale form its **family**,
//! [`ScaleFamily`]. Counting them is how a periodic table of scales is built:
//!
//! - **N**, [`Scale::len`]: how many notes.
//! - **M**, [`Scale::mode_count`]: how many *different* modes. The major scale
//!   has seven, the whole-tone scale one, because every one of its rotations
//!   is the same shape.
//! - **s**, [`Scale::symmetry`]: how many times the shape repeats around the
//!   octave, N divided by M.
//! - **T**, [`Scale::transpositions`]: how many different pitch-class sets the
//!   shape makes on the twelve tonics, 12 divided by s.

use core::fmt::{self, Write as _};

use crate::DisplayBuffer;
use crate::chord::{Chord, ChordQuality};
use crate::pitch::{EDO, Interval, Pitch};
use crate::pitchset::PitchSet;

/// The semitones of the major scale, which every degree number is read
/// against: degree 3 is a major third, `b3` a semitone lower.
const MAJOR: [u8; 7] = [0, 2, 4, 5, 7, 9, 11];

/// The shape of a scale: its pitch classes counted upward from the root.
///
/// The root is pitch class 0 and is always present, so a `Scale` says how a
/// scale is built, never where it starts. [`Scale::on`] puts it on a tonic.
///
/// Two bytes, `Copy`.
///
/// ```
/// use music_core::{Pitch, Scale};
///
/// let major = Scale::MAJOR;
/// assert_eq!(major.len(), 7);
/// assert_eq!(major.to_string(), "1 2 3 4 5 6 7");
///
/// // The same notes from the second degree are D Dorian's shape.
/// let dorian = major.mode(1);
/// assert_eq!(dorian.to_string(), "1 2 b3 4 5 6 b7");
/// assert_eq!(dorian.name(), Some("Dorian"));
///
/// assert!(major.on(Pitch::G).contains(Pitch::F_SHARP));
/// ```
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
#[repr(transparent)]
pub struct Scale(PitchSet);

impl Default for Scale {
    /// The major scale.
    fn default() -> Self {
        Self::MAJOR
    }
}

impl Scale {
    /// The major scale, Ionian: `1 2 3 4 5 6 7`.
    pub const MAJOR: Self = Self(PitchSet::from_bits_truncating(0b1010_1011_0101));
    /// The natural minor scale, Aeolian: `1 2 b3 4 5 b6 b7`.
    pub const NATURAL_MINOR: Self = Self(PitchSet::from_bits_truncating(0b0101_1010_1101));
    /// The harmonic minor scale: `1 2 b3 4 5 b6 7`.
    pub const HARMONIC_MINOR: Self = Self(PitchSet::from_bits_truncating(0b1001_1010_1101));
    /// The ascending melodic minor scale: `1 2 b3 4 5 6 7`.
    pub const MELODIC_MINOR: Self = Self(PitchSet::from_bits_truncating(0b1010_1010_1101));
    /// The major pentatonic scale: `1 2 3 5 6`.
    pub const MAJOR_PENTATONIC: Self = Self(PitchSet::from_bits_truncating(0b0010_1001_0101));
    /// The minor pentatonic scale: `1 b3 4 5 b7`.
    pub const MINOR_PENTATONIC: Self = Self(PitchSet::from_bits_truncating(0b0100_1010_1001));
    /// The whole-tone scale: six whole steps, one mode.
    pub const WHOLE_TONE: Self = Self(PitchSet::from_bits_truncating(0b0101_0101_0101));
    /// The octatonic scale, half step first: half-whole diminished.
    pub const OCTATONIC: Self = Self(PitchSet::from_bits_truncating(0b0110_1101_1011));
    /// All twelve pitch classes.
    pub const CHROMATIC: Self = Self(PitchSet::CHROMATIC);

    /// The scale whose pitch classes above the root are `set`, `None` when
    /// `set` does not contain the root, pitch class 0.
    #[inline]
    #[must_use]
    pub const fn from_pitch_set(set: PitchSet) -> Option<Self> {
        if set.contains(Pitch::C) {
            Some(Self(set))
        } else {
            None
        }
    }

    /// The scale with this raw mask, bit 0 the root. `None` when bit 0 is
    /// clear or a bit above 11 is set.
    #[inline]
    #[must_use]
    pub const fn from_bits(bits: u16) -> Option<Self> {
        match PitchSet::from_bits(bits) {
            Some(set) => Self::from_pitch_set(set),
            None => None,
        }
    }

    /// The scale made of `semitones` above the root. The root is added when
    /// it is missing, and every value wraps into the octave, so this is total.
    ///
    /// ```
    /// use music_core::Scale;
    ///
    /// assert_eq!(Scale::new(&[0, 2, 4, 5, 7, 9, 11]), Scale::MAJOR);
    /// ```
    #[must_use]
    pub const fn new(semitones: &[u8]) -> Self {
        let mut set = PitchSet::EMPTY.insert(Pitch::C);
        let mut i = 0;
        while i < semitones.len() {
            set = set.insert(Pitch::new(semitones[i] as i32));
            i += 1;
        }
        Self(set)
    }

    /// The pitch classes above the root, the root itself at 0.
    #[inline]
    #[must_use]
    pub const fn pitch_set(self) -> PitchSet {
        self.0
    }

    /// The raw mask: bit 0 is the root, bit 11 the major seventh.
    #[inline]
    #[must_use]
    pub const fn bits(self) -> u16 {
        self.0.bits()
    }

    /// How many notes it has, 1 to 12. N on a scale portrait.
    #[inline]
    #[must_use]
    #[allow(clippy::len_without_is_empty)] // a scale always holds its root
    pub const fn len(self) -> u32 {
        self.0.len()
    }

    /// The pitch classes of this scale started on `tonic`.
    #[inline]
    #[must_use]
    pub const fn on(self, tonic: Pitch) -> PitchSet {
        self.0.transpose(Interval::new(tonic.value() as i16))
    }

    /// How many semitones above the root the note on `degree` lies, counting
    /// the root as degree 0 and wrapping past the last note.
    #[must_use]
    pub const fn semitones_of(self, degree: u32) -> u8 {
        let target = degree % self.len();
        let mut bits = self.bits();
        let mut seen = 0;
        while bits != 0 {
            let semitone = bits.trailing_zeros();
            if seen == target {
                return semitone as u8;
            }
            seen += 1;
            bits &= bits - 1;
        }
        0
    }

    /// The same notes started on `degree`, counting the root as degree 0.
    /// Degrees wrap, so `mode(len)` is the scale itself.
    #[inline]
    #[must_use]
    pub const fn mode(self, degree: u32) -> Self {
        let semitones = self.semitones_of(degree);
        Self(self.0.transpose(Interval::new(-(semitones as i16))))
    }

    /// Its different modes, one per distinct shape, starting with itself.
    ///
    /// Yields [`Scale::mode_count`] scales: the modes on degrees 0, 1, 2 and
    /// so on, stopping before the first that repeats the scale. Allocation-free.
    #[inline]
    #[must_use]
    pub const fn modes(self) -> Modes {
        Modes {
            scale: self,
            next: 0,
            count: self.mode_count(),
        }
    }

    /// How many of the twelve rotations by a semitone map the scale onto
    /// itself: 1 for most scales, 6 for the whole-tone scale, 12 for the
    /// chromatic. s on a scale portrait, always N divided by M.
    #[must_use]
    pub const fn symmetry(self) -> u32 {
        let mut count = 0;
        let mut t = 0;
        while t < EDO {
            if self.0.transpose(Interval::new(t as i16)).bits() == self.bits() {
                count += 1;
            }
            t += 1;
        }
        count
    }

    /// How many different modes it has: N divided by s. M on a scale portrait.
    #[inline]
    #[must_use]
    pub const fn mode_count(self) -> u32 {
        self.len() / self.symmetry()
    }

    /// How many different pitch-class sets it makes on the twelve tonics: 12
    /// divided by s. T on a scale portrait.
    #[inline]
    #[must_use]
    pub const fn transpositions(self) -> u32 {
        EDO as u32 / self.symmetry()
    }

    /// The family of all its modes.
    #[inline]
    #[must_use]
    pub const fn family(self) -> ScaleFamily {
        ScaleFamily::new(self)
    }

    /// Whether `other` is one of its modes.
    #[inline]
    #[must_use]
    pub const fn is_mode_of(self, other: Self) -> bool {
        self.family().canonical().bits() == other.family().canonical().bits()
    }

    /// The scale mirrored about its root: every interval above becomes the
    /// same interval below. Lydian mirrors to Locrian, Dorian to itself.
    #[inline]
    #[must_use]
    pub const fn inverted(self) -> Self {
        Self(self.0.invert(0))
    }

    /// The semitone steps from each note to the next, ending with the step
    /// back up to the octave. They always add up to twelve.
    #[inline]
    #[must_use]
    pub const fn steps(self) -> Steps {
        Steps {
            scale: self,
            degree: 0,
        }
    }

    /// Each note as a degree number with its accidental: `1 2 b3 4 5 6 b7`.
    ///
    /// Degree numbers never go down, and every letter is used once where it
    /// can be, as a score would write the scale. A note moves to a free
    /// number rather than share one: a dominant chord with both thirds reads
    /// `1 #2 3 5 b7`, because the 2 is free. When a number must be shared, a
    /// flat is preferred to a sharp, so the major blues scale is
    /// `1 2 b3 3 5 6` and the whole-tone scale `1 2 3 b5 b6 b7`.
    #[must_use]
    pub const fn degrees(self) -> Degrees {
        Degrees {
            spelled: spell(self),
            len: self.len() as u8,
            next: 0,
        }
    }

    /// The sum of every note's place on the circle of fifths, counted from
    /// the root: G is +1, D +2, F -1, B flat -2, and the tritone 0.
    ///
    /// The tritone counts zero because it is its own mirror image: it lies as
    /// far round one way as the other. That makes the sum change sign when the
    /// scale is [inverted](Scale::inverted).
    #[must_use]
    pub const fn fifths_sum(self) -> i8 {
        let mut sum = 0i8;
        let mut bits = self.bits();
        while bits != 0 {
            let semitone = bits.trailing_zeros();
            sum += signed_fifths(Pitch::new(semitone as i32));
            bits &= bits - 1;
        }
        sum
    }

    /// How far the scale leans to the sharp side of the circle of fifths,
    /// from -1.0 to 1.0.
    ///
    /// [`Scale::fifths_sum`] divided by the largest sum any scale with this
    /// many notes can reach. Lydian is 1.0, the brightest seven-note scale;
    /// Locrian is -1.0; Dorian, its own mirror image, is 0.0. A scale and its
    /// inversion always have opposite brightness. The chromatic scale is 0.0.
    ///
    /// ```
    /// use music_core::Scale;
    ///
    /// let lydian = Scale::MAJOR.mode(3);
    /// let dorian = Scale::MAJOR.mode(1);
    ///
    /// assert_eq!(lydian.brightness(), 1.0);
    /// assert_eq!(dorian.brightness(), 0.0);
    /// assert_eq!(lydian.inverted().brightness(), -1.0);
    /// ```
    #[must_use]
    pub fn brightness(self) -> f32 {
        let most = MOST_FIFTHS[self.len() as usize];
        if most == 0 {
            0.0
        } else {
            f32::from(self.fifths_sum()) / f32::from(most)
        }
    }

    /// The two triads the scale splits into, when it has six notes and
    /// splits that way: the whole-tone scale is two augmented triads a whole
    /// step apart.
    ///
    /// Only major, minor, diminished and augmented triads count. The first
    /// triad holds the root; when several splits exist, the first by
    /// quality in that order wins, then the lower second root.
    #[must_use]
    pub fn component_triads(self) -> Option<[Chord; 2]> {
        if self.len() != 6 {
            return None;
        }
        const TRIADS: [ChordQuality; 4] = [
            ChordQuality::Major,
            ChordQuality::Minor,
            ChordQuality::Diminished,
            ChordQuality::Augmented,
        ];
        for first_quality in TRIADS {
            for root in self.0.iter() {
                let first = Chord::from_quality(root, first_quality);
                if !first.pitches().contains(Pitch::C) || !first.pitches().is_subset(self.0) {
                    continue;
                }
                let rest = self.0.difference(first.pitches());
                for second_root in rest.iter() {
                    for second_quality in TRIADS {
                        let second = Chord::from_quality(second_root, second_quality);
                        if second.pitches() == rest {
                            return Some([first, second]);
                        }
                    }
                }
            }
        }
        None
    }

    /// The chord qualities whose every note is in the scale, built on its
    /// root, in [`ChordQuality::ALL`] order.
    ///
    /// For a chord on another degree, ask the [mode](Scale::mode) on that
    /// degree: a D minor 7 in C major is the minor 7 on D Dorian's root.
    #[inline]
    #[must_use]
    pub const fn chords(self) -> Chords {
        Chords {
            scale: self,
            next: 0,
        }
    }

    /// The scale's own name, when it has a common one: `"Dorian"`,
    /// `"Whole tone"`, `"Altered"`.
    #[inline]
    #[must_use]
    pub fn name(self) -> Option<&'static str> {
        crate::catalog::mode_name(self)
    }
}

/// Each pitch class's place on the circle of fifths, signed: -5 to 5, and 0
/// for the tritone.
const fn signed_fifths(pitch: Pitch) -> i8 {
    let fifths = pitch.fifths() as i8;
    if fifths == 6 {
        0
    } else if fifths > 6 {
        fifths - 12
    } else {
        fifths
    }
}

/// The largest [`Scale::fifths_sum`] a scale of each size can reach: its
/// root plus the brightest notes still free. Indexed by size, 0 to 12.
const MOST_FIFTHS: [u8; 13] = [0, 0, 5, 9, 12, 14, 15, 15, 14, 12, 9, 5, 0];

impl fmt::Display for Scale {
    /// The degrees, space separated: `"1 2 b3 4 5 6 b7"`.
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let mut buffer = DisplayBuffer::<48>::new();
        for (i, degree) in self.degrees().enumerate() {
            let separator = if i > 0 { " " } else { "" };
            let _ = write!(buffer, "{separator}{degree}");
        }
        f.pad(buffer.as_str())
    }
}

impl fmt::Debug for Scale {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt::Display::fmt(self, f)
    }
}

/// Iterator over the different modes of a [`Scale`], from [`Scale::modes`].
#[derive(Clone, Copy, Debug)]
pub struct Modes {
    scale: Scale,
    next: u32,
    count: u32,
}

impl Iterator for Modes {
    type Item = Scale;

    #[inline]
    fn next(&mut self) -> Option<Scale> {
        if self.next >= self.count {
            return None;
        }
        let mode = self.scale.mode(self.next);
        self.next += 1;
        Some(mode)
    }

    #[inline]
    fn size_hint(&self) -> (usize, Option<usize>) {
        let left = (self.count - self.next) as usize;
        (left, Some(left))
    }
}

impl ExactSizeIterator for Modes {}
impl core::iter::FusedIterator for Modes {}

/// Iterator over the semitone steps of a [`Scale`], from [`Scale::steps`].
#[derive(Clone, Copy, Debug)]
pub struct Steps {
    scale: Scale,
    degree: u32,
}

impl Iterator for Steps {
    type Item = u8;

    #[inline]
    fn next(&mut self) -> Option<u8> {
        let len = self.scale.len();
        if self.degree >= len {
            return None;
        }
        let here = self.scale.semitones_of(self.degree);
        let there = if self.degree + 1 == len {
            EDO
        } else {
            self.scale.semitones_of(self.degree + 1)
        };
        self.degree += 1;
        Some(there - here)
    }

    #[inline]
    fn size_hint(&self) -> (usize, Option<usize>) {
        let left = (self.scale.len() - self.degree) as usize;
        (left, Some(left))
    }
}

impl ExactSizeIterator for Steps {}
impl core::iter::FusedIterator for Steps {}

/// Iterator over the chord qualities built on a scale's root, from
/// [`Scale::chords`].
#[derive(Clone, Copy, Debug)]
pub struct Chords {
    scale: Scale,
    next: u8,
}

impl Iterator for Chords {
    type Item = ChordQuality;

    fn next(&mut self) -> Option<ChordQuality> {
        while (self.next as usize) < ChordQuality::ALL.len() {
            let quality = ChordQuality::ALL[self.next as usize];
            self.next += 1;
            if quality.interval_set().is_subset(self.scale.pitch_set()) {
                return Some(quality);
            }
        }
        None
    }
}

impl core::iter::FusedIterator for Chords {}

/// A note of a scale named by its degree: a number from 1 to 7 and an
/// accidental, as in `b3` or `#4`.
///
/// Two bytes, `Copy`. The number is read against the major scale, so `3` is a
/// major third above the root and `b3` a minor third.
///
/// ```
/// use music_core::{Scale, ScaleDegree};
///
/// let flat_seven = ScaleDegree::new(7, -1);
/// assert_eq!(flat_seven.to_string(), "b7");
/// assert_eq!(flat_seven.semitones(), 10);
/// assert_eq!(Scale::MAJOR.mode(4).degrees().last(), Some(flat_seven));
/// ```
#[derive(Clone, Copy, PartialEq, Eq, Hash, Default)]
pub struct ScaleDegree {
    number: u8,
    accidental: i8,
}

impl ScaleDegree {
    /// Degree `number`, wrapped into 1 to 7, raised by `accidental`
    /// semitones (lowered when negative).
    #[inline]
    #[must_use]
    pub const fn new(number: u8, accidental: i8) -> Self {
        let wrapped = if number == 0 { 0 } else { (number - 1) % 7 };
        Self {
            number: wrapped + 1,
            accidental,
        }
    }

    /// The degree number, 1 to 7.
    #[inline]
    #[must_use]
    pub const fn number(self) -> u8 {
        self.number
    }

    /// Sharps positive, flats negative.
    #[inline]
    #[must_use]
    pub const fn accidental(self) -> i8 {
        self.accidental
    }

    /// How many semitones above the root it lies, 0 to 11.
    #[inline]
    #[must_use]
    pub const fn semitones(self) -> u8 {
        Pitch::new(MAJOR[(self.number - 1) as usize] as i32 + self.accidental as i32).value()
    }
}

impl fmt::Display for ScaleDegree {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let mut buffer = DisplayBuffer::<8>::new();
        let mark = if self.accidental < 0 { "b" } else { "#" };
        for _ in 0..self.accidental.unsigned_abs().min(2) {
            let _ = buffer.write_str(mark);
        }
        let _ = write!(buffer, "{}", self.number);
        f.pad(buffer.as_str())
    }
}

impl fmt::Debug for ScaleDegree {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt::Display::fmt(self, f)
    }
}

/// Iterator over the spelled degrees of a [`Scale`], from [`Scale::degrees`].
#[derive(Clone, Copy, Debug)]
pub struct Degrees {
    spelled: [ScaleDegree; 12],
    len: u8,
    next: u8,
}

impl Iterator for Degrees {
    type Item = ScaleDegree;

    #[inline]
    fn next(&mut self) -> Option<ScaleDegree> {
        if self.next >= self.len {
            return None;
        }
        let degree = self.spelled[self.next as usize];
        self.next += 1;
        Some(degree)
    }

    #[inline]
    fn size_hint(&self) -> (usize, Option<usize>) {
        let left = (self.len - self.next) as usize;
        (left, Some(left))
    }
}

impl ExactSizeIterator for Degrees {}
impl core::iter::FusedIterator for Degrees {}

/// What a spelling costs, so the cheapest one can be chosen. Scaled by 100 to
/// stay in integers: a flat costs 100, a sharp 101 (so flats win ties), a
/// second note on the same number 150, and a name that is really a white key
/// (`b4`, `#3`, `#7`) another 150.
const COST_FLAT: u32 = 100;
const COST_SHARP: u32 = 101;
const COST_REUSE: u32 = 150;
const COST_WHITE: u32 = 150;
const UNREACHABLE: u32 = u32::MAX / 4;

/// The cheapest spelling of every note, by dynamic programming over the notes
/// in order and the degree number each one takes. Numbers never go down, the
/// root is a plain 1, and accidentals stay within one sharp or flat.
const fn spell(scale: Scale) -> [ScaleDegree; 12] {
    let mut semitones = [0u8; 12];
    let len = scale.len() as usize;
    let mut i = 0;
    while i < len {
        semitones[i] = scale.semitones_of(i as u32);
        i += 1;
    }

    // cost[i][n]: the cheapest spelling of notes 0..=i with note i on number n.
    let mut cost = [[UNREACHABLE; 7]; 12];
    let mut from = [[0u8; 7]; 12];
    cost[0][0] = 0;

    let mut i = 1;
    while i < len {
        let mut number = 0;
        while number < 7 {
            let accidental = semitones[i] as i32 - MAJOR[number] as i32;
            if accidental.abs() <= 1 {
                let own = own_cost(number, accidental);
                let mut previous = 0;
                while previous <= number {
                    let before = cost[i - 1][previous];
                    if before < UNREACHABLE {
                        let reuse = if previous == number { COST_REUSE } else { 0 };
                        let total = before + own + reuse;
                        if total < cost[i][number] {
                            cost[i][number] = total;
                            from[i][number] = previous as u8;
                        }
                    }
                    previous += 1;
                }
            }
            number += 1;
        }
        i += 1;
    }

    let mut best = 0;
    let mut number = 1;
    while number < 7 {
        if cost[len - 1][number] < cost[len - 1][best] {
            best = number;
        }
        number += 1;
    }

    let mut out = [ScaleDegree {
        number: 1,
        accidental: 0,
    }; 12];
    let mut i = len;
    let mut number = best;
    while i > 1 {
        i -= 1;
        let accidental = semitones[i] as i32 - MAJOR[number] as i32;
        out[i] = ScaleDegree {
            number: number as u8 + 1,
            accidental: accidental as i8,
        };
        number = from[i][number] as usize;
    }
    out
}

/// The cost of naming a note `number` (0-based) with `accidental`.
const fn own_cost(number: usize, accidental: i32) -> u32 {
    if accidental == 0 {
        return 0;
    }
    let base = if accidental < 0 {
        COST_FLAT
    } else {
        COST_SHARP
    };
    // A flat on a number whose major step lies a semitone above the one
    // below (4 and the octave) lands on a white key: b4 is a 3.
    let semitone = MAJOR[number] as i32 + accidental;
    let mut white = false;
    let mut k = 0;
    while k < 7 {
        if MAJOR[k] as i32 == semitone.rem_euclid(12) {
            white = true;
        }
        k += 1;
    }
    if white { base + COST_WHITE } else { base }
}

/// A family of scales: every mode of one shape.
///
/// Held as its **canonical** mode, the one with the smallest mask, which is
/// also Rahn's normal order packed from the right. The major scale's family
/// is held as Locrian, `1 b2 b3 4 b5 b6 b7`, though it is
/// [referred to](ScaleFamily::reference) by Ionian.
///
/// Two bytes, `Copy`. There are 351 families of one or more notes, and 344
/// once single notes and intervals are left out: [`ScaleFamily::all`].
///
/// ```
/// use music_core::{Scale, ScaleFamily};
///
/// let diatonic = Scale::MAJOR.family();
/// assert_eq!(diatonic, Scale::MAJOR.mode(4).family());
/// assert_eq!(diatonic.name(), Some("Diatonic"));
/// assert_eq!(diatonic.reference(), Scale::MAJOR);
/// assert_eq!(diatonic.mode_count(), 7);
///
/// let whole_tone = Scale::WHOLE_TONE.family();
/// assert_eq!(whole_tone.mode_count(), 1);
/// assert_eq!(whole_tone.symmetry(), 6);
/// assert_eq!(whole_tone.transpositions(), 2);
/// ```
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
#[repr(transparent)]
pub struct ScaleFamily(Scale);

impl ScaleFamily {
    /// The family `scale` belongs to. Total: every scale has one.
    #[must_use]
    pub const fn new(scale: Scale) -> Self {
        let mut best = scale;
        let mut degree = 1;
        let len = scale.len();
        while degree < len {
            let mode = scale.mode(degree);
            if mode.bits() < best.bits() {
                best = mode;
            }
            degree += 1;
        }
        Self(best)
    }

    /// Every family, in ascending order of the canonical mask. Pass
    /// `min_notes` 3 to leave out single notes and intervals, as a table of
    /// scales does: that leaves 344.
    #[inline]
    #[must_use]
    pub const fn all(min_notes: u32) -> Families {
        Families { next: 1, min_notes }
    }

    /// The families with a common name, as listed in the crate's catalog.
    #[inline]
    #[must_use]
    pub const fn named() -> crate::catalog::NamedFamilies {
        crate::catalog::NamedFamilies::new()
    }

    /// The canonical mode: the one with the smallest mask.
    #[inline]
    #[must_use]
    pub const fn canonical(self) -> Scale {
        self.0
    }

    /// The mode the family is known by: Ionian for the diatonic family,
    /// melodic minor for its family. The canonical mode when the family has
    /// no common name.
    #[inline]
    #[must_use]
    pub fn reference(self) -> Scale {
        crate::catalog::family_reference(self).unwrap_or(self.0)
    }

    /// The family's common name, when it has one: `"Diatonic"`,
    /// `"Harmonic minor"`, `"Whole tone"`.
    #[inline]
    #[must_use]
    pub fn name(self) -> Option<&'static str> {
        crate::catalog::family_name(self)
    }

    /// A three-letter tag for tight spaces, when the family has a name:
    /// `"Dia"`, `"WhT"`, `"Oct"`.
    #[inline]
    #[must_use]
    pub fn abbreviation(self) -> Option<&'static str> {
        crate::catalog::family_abbreviation(self)
    }

    /// How many notes each mode has. N.
    #[inline]
    #[must_use]
    #[allow(clippy::len_without_is_empty)] // a family always has a root
    pub const fn len(self) -> u32 {
        self.0.len()
    }

    /// How many different modes it has. M.
    #[inline]
    #[must_use]
    pub const fn mode_count(self) -> u32 {
        self.0.mode_count()
    }

    /// How many times the shape repeats around the octave. s = N / M.
    #[inline]
    #[must_use]
    pub const fn symmetry(self) -> u32 {
        self.0.symmetry()
    }

    /// How many different pitch-class sets one mode makes on the twelve
    /// tonics. T = 12 / s.
    #[inline]
    #[must_use]
    pub const fn transpositions(self) -> u32 {
        self.0.transpositions()
    }

    /// Its modes, starting with the canonical one.
    #[inline]
    #[must_use]
    pub const fn modes(self) -> Modes {
        self.0.modes()
    }

    /// The interval-class vector every mode shares. See
    /// [`PitchSet::interval_vector`].
    #[inline]
    #[must_use]
    pub fn interval_vector(self) -> [u8; 6] {
        self.0.pitch_set().interval_vector()
    }
}

impl fmt::Display for ScaleFamily {
    /// The name, or the canonical mode's degrees when it has none.
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self.name() {
            Some(name) => f.pad(name),
            None => fmt::Display::fmt(&self.0, f),
        }
    }
}

impl fmt::Debug for ScaleFamily {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt::Display::fmt(self, f)
    }
}

/// Iterator over every [`ScaleFamily`], from [`ScaleFamily::all`].
#[derive(Clone, Copy, Debug)]
pub struct Families {
    next: u16,
    min_notes: u32,
}

impl Iterator for Families {
    type Item = ScaleFamily;

    fn next(&mut self) -> Option<ScaleFamily> {
        // Every scale has bit 0 set, so only odd masks are scales.
        while self.next < 1 << EDO {
            let bits = self.next;
            self.next += 2;
            let Some(scale) = Scale::from_bits(bits) else {
                continue;
            };
            if scale.len() >= self.min_notes && ScaleFamily::new(scale).canonical() == scale {
                return Some(ScaleFamily(scale));
            }
        }
        None
    }
}

impl core::iter::FusedIterator for Families {}

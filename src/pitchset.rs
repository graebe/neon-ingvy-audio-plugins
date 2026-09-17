//! A set of pitch classes, stored as a 12-bit mask.

use core::fmt::{self, Write as _};
use core::ops::{BitAnd, BitOr, BitXor, Not, Sub};

use crate::Harmony;
use crate::chord::Chord;
use crate::pitch::{Interval, Pitch, Spelling};

const MASK: u16 = 0x0FFF;

/// An unordered set of pitch classes, held as one bit per pitch class.
///
/// This is the generic harmony of the crate: any collection of pitch classes
/// at all, with union, intersection, transposition and inversion costing a
/// handful of instructions and no allocation.
///
/// ```
/// use music_core::{Pitch, PitchSet};
///
/// let c_major = PitchSet::from_pitches(&[Pitch::C, Pitch::E, Pitch::G]);
/// let a_minor = PitchSet::from_pitches(&[Pitch::A, Pitch::C, Pitch::E]);
///
/// assert_eq!((c_major & a_minor).len(), 2);   // C and E
/// assert!(c_major.contains(Pitch::G));
/// ```
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
#[repr(transparent)]
pub struct PitchSet(u16);

impl PitchSet {
    /// The empty set.
    pub const EMPTY: Self = Self(0);
    /// All twelve pitch classes.
    pub const CHROMATIC: Self = Self(MASK);

    /// Builds a set from a raw mask, returning `None` if bits above 11 are set.
    #[inline]
    #[must_use]
    pub const fn from_bits(bits: u16) -> Option<Self> {
        if bits & !MASK == 0 {
            Some(Self(bits))
        } else {
            None
        }
    }

    /// Builds a set from a raw mask, discarding bits above 11.
    #[inline]
    #[must_use]
    pub const fn from_bits_truncating(bits: u16) -> Self {
        Self(bits & MASK)
    }

    /// The raw mask, one bit per pitch class with C at bit 0.
    #[inline]
    #[must_use]
    pub const fn bits(self) -> u16 {
        self.0
    }

    /// Builds a set from a slice of pitch classes. Duplicates collapse.
    #[must_use]
    pub fn from_pitches(pitches: &[Pitch]) -> Self {
        let mut bits = 0u16;
        for p in pitches {
            bits |= 1 << p.value();
        }
        Self(bits)
    }

    /// Adds a pitch class.
    #[inline]
    #[must_use]
    pub const fn insert(self, pitch: Pitch) -> Self {
        Self(self.0 | (1 << pitch.value()))
    }

    /// Removes a pitch class.
    #[inline]
    #[must_use]
    pub const fn remove(self, pitch: Pitch) -> Self {
        Self(self.0 & !(1 << pitch.value()))
    }

    /// Whether a pitch class is present.
    #[inline]
    #[must_use]
    pub const fn contains(self, pitch: Pitch) -> bool {
        self.0 & (1 << pitch.value()) != 0
    }

    /// How many pitch classes are present, 0 to 12.
    #[inline]
    #[must_use]
    pub const fn len(self) -> u32 {
        self.0.count_ones()
    }

    /// Whether the set is empty.
    #[inline]
    #[must_use]
    pub const fn is_empty(self) -> bool {
        self.0 == 0
    }

    /// Everything in either set.
    #[inline]
    #[must_use]
    pub const fn union(self, other: Self) -> Self {
        Self(self.0 | other.0)
    }

    /// Everything in both sets.
    #[inline]
    #[must_use]
    pub const fn intersection(self, other: Self) -> Self {
        Self(self.0 & other.0)
    }

    /// Everything in this set but not the other.
    #[inline]
    #[must_use]
    pub const fn difference(self, other: Self) -> Self {
        Self(self.0 & !other.0)
    }

    /// Everything in exactly one of the two sets.
    #[inline]
    #[must_use]
    pub const fn symmetric_difference(self, other: Self) -> Self {
        Self(self.0 ^ other.0)
    }

    /// Everything not in this set.
    #[inline]
    #[must_use]
    pub const fn complement(self) -> Self {
        Self(!self.0 & MASK)
    }

    /// Whether every pitch class here is also in `other`.
    #[inline]
    #[must_use]
    pub const fn is_subset(self, other: Self) -> bool {
        self.0 & !other.0 == 0
    }

    /// Whether every pitch class in `other` is also here.
    #[inline]
    #[must_use]
    pub const fn is_superset(self, other: Self) -> bool {
        other.is_subset(self)
    }

    /// How many pitch classes the two sets share.
    #[inline]
    #[must_use]
    pub const fn common_tones(self, other: Self) -> u32 {
        (self.0 & other.0).count_ones()
    }

    /// Moves every pitch class by an interval, as a rotation of the bit mask.
    #[inline]
    #[must_use]
    pub const fn transpose(self, by: Interval) -> Self {
        let m = by.semitones() % 12;
        let n = (if m < 0 { m + 12 } else { m }) as u32;
        let bits = self.0 as u32;
        Self((((bits << n) | (bits >> (12 - n))) & MASK as u32) as u16)
    }

    /// Reflects every pitch class about an inversion axis.
    #[must_use]
    pub const fn invert(self, axis: i32) -> Self {
        let mut out = 0u16;
        let mut j = 0i32;
        while j < 12 {
            let m = (axis - j) % 12;
            let src = (if m < 0 { m + 12 } else { m }) as u16;
            if (self.0 >> src) & 1 == 1 {
                out |= 1 << j;
            }
            j += 1;
        }
        Self(out)
    }

    /// The interval-class vector: how many pairs sit at each distance 1 to 6.
    ///
    /// The classic fingerprint of a chord's sound, independent of its root.
    /// Both consonant triads give `[0, 0, 1, 1, 1, 0]`.
    #[must_use]
    pub fn interval_vector(self) -> [u8; 6] {
        let mut v = [0u8; 6];
        for a in self.iter() {
            for b in self.iter() {
                if a.value() < b.value() {
                    let ic = a.distance_to(b).value();
                    if ic >= 1 {
                        v[(ic - 1) as usize] += 1;
                    }
                }
            }
        }
        v
    }

    /// Every reading of this set as a chord, one per pitch that works as a root.
    ///
    /// A set of pitches has no root of its own, so it usually has more than one
    /// honest reading. The same four notes are a C6 and an A minor 7, and which
    /// one they are depends on musical context this type cannot see. So this
    /// yields all of them and lets you choose.
    ///
    /// Lazy and allocation-free. Only readings that match a named quality are
    /// yielded; see [`Chord::quality`].
    ///
    /// ```
    /// use music_core::{Chord, Pitch, PitchSet};
    ///
    /// let pitches = Chord::sixth(Pitch::C).pitches();
    /// let readings: usize = pitches.interpretations().count();
    /// assert_eq!(readings, 2);          // C6 and Am7
    ///
    /// // A diminished seventh is symmetric, so all four roots work.
    /// let dim = Chord::dim7(Pitch::C).pitches();
    /// assert_eq!(dim.interpretations().count(), 4);
    /// ```
    #[inline]
    #[must_use]
    pub const fn interpretations(self) -> Interpretations {
        Interpretations {
            roots: self.iter(),
            pitches: self,
        }
    }

    /// The single most likely reading of this set as a chord.
    ///
    /// A convenience over [`PitchSet::interpretations`] for when you want one
    /// answer. **This is a heuristic, not a fact.** It prefers the reading with
    /// the lowest [`crate::ChordQuality::rank`], which puts plain triads ahead
    /// of sevenths, sevenths ahead of sixths, and named chords ahead of altered
    /// ones. Ties go to the lower root.
    ///
    /// When the answer matters, read [`PitchSet::interpretations`] instead and
    /// decide with the context you have.
    ///
    /// ```
    /// use music_core::{Chord, ChordQuality, Pitch};
    ///
    /// // C, E, G, A is both a C6 and an A minor 7. The seventh wins.
    /// let pitches = Chord::sixth(Pitch::C).pitches();
    /// let best = pitches.identify().unwrap();
    ///
    /// assert_eq!(best.root(), Pitch::A);
    /// assert_eq!(best.quality(), Some(ChordQuality::Minor7));
    /// ```
    #[must_use]
    pub fn identify(self) -> Option<Chord> {
        let mut best: Option<Chord> = None;
        for candidate in self.interpretations() {
            let better = match best {
                None => true,
                Some(current) => {
                    let (a, b) = (candidate.quality()?, current.quality()?);
                    (a.rank(), candidate.root().value()) < (b.rank(), current.root().value())
                }
            };
            if better {
                best = Some(candidate);
            }
        }
        best
    }

    /// Iterates the pitch classes in ascending order from C. Allocation-free.
    #[inline]
    #[must_use]
    pub const fn iter(self) -> PitchSetIter {
        PitchSetIter(self.0)
    }
}

impl Harmony for PitchSet {
    #[inline]
    fn pitch_set(&self) -> PitchSet {
        *self
    }
}

/// Iterator over the pitch classes of a [`PitchSet`], ascending from C.
#[derive(Clone, Copy, Debug)]
pub struct PitchSetIter(u16);

impl Iterator for PitchSetIter {
    type Item = Pitch;

    #[inline]
    fn next(&mut self) -> Option<Pitch> {
        if self.0 == 0 {
            return None;
        }
        let index = self.0.trailing_zeros();
        self.0 &= self.0 - 1;
        Some(Pitch::new(index as i32))
    }

    #[inline]
    fn size_hint(&self) -> (usize, Option<usize>) {
        let n = self.0.count_ones() as usize;
        (n, Some(n))
    }
}

impl ExactSizeIterator for PitchSetIter {}

/// Iterator over the ways a [`PitchSet`] can be read as a chord.
///
/// Built by [`PitchSet::interpretations`]. Walks the set's own pitches, trying
/// each as a root, and yields the ones that name a known quality.
#[derive(Clone, Copy, Debug)]
pub struct Interpretations {
    roots: PitchSetIter,
    pitches: PitchSet,
}

impl Iterator for Interpretations {
    type Item = Chord;

    fn next(&mut self) -> Option<Chord> {
        for root in self.roots.by_ref() {
            let candidate = Chord::new(root, self.pitches);
            if candidate.quality().is_some() {
                return Some(candidate);
            }
        }
        None
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        (0, Some(self.roots.len()))
    }
}

impl core::iter::FusedIterator for Interpretations {}

impl core::iter::FusedIterator for PitchSetIter {}

impl IntoIterator for PitchSet {
    type Item = Pitch;
    type IntoIter = PitchSetIter;

    #[inline]
    fn into_iter(self) -> PitchSetIter {
        self.iter()
    }
}

impl FromIterator<Pitch> for PitchSet {
    fn from_iter<T: IntoIterator<Item = Pitch>>(iter: T) -> Self {
        let mut bits = 0u16;
        for p in iter {
            bits |= 1 << p.value();
        }
        Self(bits)
    }
}

impl BitOr for PitchSet {
    type Output = PitchSet;
    #[inline]
    fn bitor(self, rhs: PitchSet) -> PitchSet {
        self.union(rhs)
    }
}

impl BitAnd for PitchSet {
    type Output = PitchSet;
    #[inline]
    fn bitand(self, rhs: PitchSet) -> PitchSet {
        self.intersection(rhs)
    }
}

impl BitXor for PitchSet {
    type Output = PitchSet;
    #[inline]
    fn bitxor(self, rhs: PitchSet) -> PitchSet {
        self.symmetric_difference(rhs)
    }
}

impl Sub for PitchSet {
    type Output = PitchSet;
    #[inline]
    fn sub(self, rhs: PitchSet) -> PitchSet {
        self.difference(rhs)
    }
}

impl Not for PitchSet {
    type Output = PitchSet;
    #[inline]
    fn not(self) -> PitchSet {
        self.complement()
    }
}

impl fmt::Display for PitchSet {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let mut buffer = crate::DisplayBuffer::<64>::new();
        let _ = core::write!(buffer, "{{");
        for (i, p) in self.iter().enumerate() {
            let separator = if i > 0 { ", " } else { "" };
            let _ = core::write!(buffer, "{}{}", separator, p.name(Spelling::Sharps));
        }
        let _ = core::write!(buffer, "}}");
        f.pad(buffer.as_str())
    }
}

impl fmt::Debug for PitchSet {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt::Display::fmt(self, f)
    }
}

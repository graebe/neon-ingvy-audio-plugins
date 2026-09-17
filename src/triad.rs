//! The consonant triad, and the Tonnetz cycles built from it.

use core::fmt::{self, Write as _};
use core::str::FromStr;

use crate::chord::{Chord, ChordQuality};
use crate::pitch::{Interval, Pitch, Spelling, parse_pitch_prefix};
use crate::pitchset::PitchSet;
use crate::voiced::{Note, Transposable, Voiceable, Voiced};
use crate::{Harmony, ParseError, padded};

/// Major or minor. The only two qualities a [`Triad`] can have.
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Debug, Default)]
#[repr(u8)]
pub enum TriadQuality {
    /// Root, major third, perfect fifth.
    #[default]
    Major = 0,
    /// Root, minor third, perfect fifth.
    Minor = 1,
}

impl TriadQuality {
    /// The other quality.
    #[inline]
    #[must_use]
    pub const fn flip(self) -> Self {
        match self {
            TriadQuality::Major => TriadQuality::Minor,
            TriadQuality::Minor => TriadQuality::Major,
        }
    }

    /// The interval from the root to the third.
    #[inline]
    #[must_use]
    pub const fn third_interval(self) -> Interval {
        match self {
            TriadQuality::Major => Interval::MAJOR_THIRD,
            TriadQuality::Minor => Interval::MINOR_THIRD,
        }
    }
}

/// A consonant triad: major or minor, never diminished or augmented.
///
/// This narrowness is deliberate and load-bearing. There are exactly 24
/// consonant triads, and the neo-Riemannian transformations in the
/// `neo-riemann` crate act on them simply transitively, which is what makes
/// those moves total functions that cannot fail. Diminished and augmented
/// chords live in [`crate::Chord`].
///
/// Minor triads are indexed by their **root**, not dualistically by their
/// fifth. A minor has root A.
///
/// ```
/// use music_core::{Pitch, Triad};
///
/// let c = Triad::major(Pitch::C);
/// let a_minor = Triad::minor(Pitch::A);
///
/// assert_eq!(c.pitches(), [Pitch::C, Pitch::E, Pitch::G]);
/// assert_eq!(c.common_tones(a_minor), 2);
/// assert_eq!(c.voice_leading_distance(a_minor), 2);
/// ```
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
pub struct Triad {
    /// The root. Always the root, for both qualities.
    pub root: Pitch,
    /// Major or minor.
    pub quality: TriadQuality,
}

/// Which voice goes where, and how far, between two triads.
///
/// Public because a crate building transformations on top of this one needs it
/// to move the right voice. Most callers want [`Triad::voice_leading_distance`]
/// instead, which is this reduced to a single number.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct VoiceMap {
    /// Voice `i` of the source becomes voice `to[i]` of the target.
    pub to: [u8; 3],
    /// Signed semitone motion of voice `i`, by the shortest path.
    pub motion: [Interval; 3],
}

/// Every bijection between two three-voice chords.
const PERMUTATIONS: [[usize; 3]; 6] = [
    [0, 1, 2],
    [0, 2, 1],
    [1, 0, 2],
    [1, 2, 0],
    [2, 0, 1],
    [2, 1, 0],
];

impl Triad {
    /// All 24 consonant triads, ordered by [`Triad::index`].
    pub const ALL: [Triad; 24] = {
        let mut out = [Triad {
            root: Pitch::C,
            quality: TriadQuality::Major,
        }; 24];
        let mut i = 0;
        while i < 24 {
            out[i] = Triad {
                root: Pitch::new((i / 2) as i32),
                quality: if i % 2 == 0 {
                    TriadQuality::Major
                } else {
                    TriadQuality::Minor
                },
            };
            i += 1;
        }
        out
    };

    /// Builds a triad from a root and a quality.
    #[inline]
    #[must_use]
    pub const fn new(root: Pitch, quality: TriadQuality) -> Self {
        Self { root, quality }
    }

    /// Builds a major triad on a root.
    #[inline]
    #[must_use]
    pub const fn major(root: Pitch) -> Self {
        Self {
            root,
            quality: TriadQuality::Major,
        }
    }

    /// Builds a minor triad on a root.
    #[inline]
    #[must_use]
    pub const fn minor(root: Pitch) -> Self {
        Self {
            root,
            quality: TriadQuality::Minor,
        }
    }

    /// Whether this triad is major.
    #[inline]
    #[must_use]
    pub const fn is_major(self) -> bool {
        matches!(self.quality, TriadQuality::Major)
    }

    /// Whether this triad is minor.
    #[inline]
    #[must_use]
    pub const fn is_minor(self) -> bool {
        matches!(self.quality, TriadQuality::Minor)
    }

    /// A dense index over the 24 triads: `2 * root + quality`.
    ///
    /// Useful as an array key. Round-trips through [`Triad::from_index`].
    #[inline]
    #[must_use]
    pub const fn index(self) -> u8 {
        self.root.value() * 2 + self.quality as u8
    }

    /// Rebuilds a triad from its [`Triad::index`], returning `None` above 23.
    #[inline]
    #[must_use]
    pub const fn from_index(index: u8) -> Option<Self> {
        if index >= 24 {
            return None;
        }
        Some(Self {
            root: Pitch::new((index / 2) as i32),
            quality: if index % 2 == 0 {
                TriadQuality::Major
            } else {
                TriadQuality::Minor
            },
        })
    }

    /// The third: a major third above the root if major, a minor third if minor.
    #[inline]
    #[must_use]
    pub const fn third(self) -> Pitch {
        self.root.transpose(self.quality.third_interval())
    }

    /// The fifth, always a perfect fifth above the root.
    #[inline]
    #[must_use]
    pub const fn fifth(self) -> Pitch {
        self.root.transpose(Interval::PERFECT_FIFTH)
    }

    /// The three pitch classes, in canonical voice order: root, third, fifth.
    #[inline]
    #[must_use]
    pub const fn pitches(self) -> [Pitch; 3] {
        [self.root, self.third(), self.fifth()]
    }

    /// The three pitch classes as a set, losing which one is the root.
    #[inline]
    #[must_use]
    pub const fn pitch_set(self) -> PitchSet {
        PitchSet::EMPTY
            .insert(self.root)
            .insert(self.third())
            .insert(self.fifth())
    }

    /// Recognises a consonant triad from a set of pitch classes.
    ///
    /// Returns `None` for anything that is not exactly three pitch classes
    /// forming a major or minor triad, which includes every diminished and
    /// augmented chord.
    #[must_use]
    pub fn from_pitch_set(set: PitchSet) -> Option<Self> {
        if set.len() != 3 {
            return None;
        }
        let mut iter = set.iter();
        let a = iter.next()?;
        let b = iter.next()?;
        let c = iter.next()?;

        for (root, x, y) in [(a, b, c), (b, c, a), (c, a, b)] {
            let to_x = root.interval_to(x).semitones();
            let to_y = root.interval_to(y).semitones();
            if to_y == 7 {
                if to_x == 4 {
                    return Some(Triad::major(root));
                }
                if to_x == 3 {
                    return Some(Triad::minor(root));
                }
            }
        }
        None
    }

    /// Whether a pitch class is one of the three.
    #[inline]
    #[must_use]
    pub const fn contains(self, pitch: Pitch) -> bool {
        self.pitch_set().contains(pitch)
    }

    /// Moves the triad by an interval, keeping its quality.
    ///
    /// This is the T half of the T/I group, which the PLR group commutes with.
    #[inline]
    #[must_use]
    pub const fn transpose(self, by: Interval) -> Self {
        Self {
            root: self.root.transpose(by),
            quality: self.quality,
        }
    }

    /// Reflects the triad about an inversion axis, flipping its quality.
    ///
    /// This is the I half of the T/I group. A major triad inverts to a minor
    /// one and back.
    #[inline]
    #[must_use]
    pub const fn invert(self, axis: i32) -> Self {
        Self {
            root: Pitch::new(axis - self.root.value() as i32 - 7),
            quality: self.quality.flip(),
        }
    }

    /// How many pitch classes this triad shares with another, 0 to 3.
    #[inline]
    #[must_use]
    pub const fn common_tones(self, other: Self) -> u32 {
        self.pitch_set().common_tones(other.pitch_set())
    }

    /// Which voice goes where, and how far, by the smoothest bijection.
    ///
    ///
    /// Minimises total motion over all six pairings. A tie at the tritone
    /// resolves upward, which P, L and R never trigger but larger Schritte can.
    #[must_use]
    pub fn voice_map(self, other: Self) -> VoiceMap {
        let from = self.pitches();
        let to = other.pitches();

        let mut best = PERMUTATIONS[0];
        let mut best_cost = u32::MAX;
        for permutation in PERMUTATIONS {
            let mut cost = 0u32;
            for voice in 0..3 {
                cost += from[voice].distance_to(to[permutation[voice]]).value() as u32;
            }
            if cost < best_cost {
                best_cost = cost;
                best = permutation;
            }
        }

        let mut map = VoiceMap {
            to: [0; 3],
            motion: [Interval::UNISON; 3],
        };
        for voice in 0..3 {
            map.to[voice] = best[voice] as u8;
            let ascending = from[voice].interval_to(to[best[voice]]).semitones();
            map.motion[voice] = Interval::new(if ascending > 6 {
                ascending - 12
            } else {
                ascending
            });
        }
        map
    }

    /// Total semitone motion to another triad, under the smoothest bijection.
    ///
    /// One for P and L, two for R. This number is the whole subject of the
    /// theory: it measures how little has to move.
    #[must_use]
    pub fn voice_leading_distance(self, other: Self) -> u32 {
        let map = self.voice_map(other);
        let mut total = 0u32;
        for voice in 0..3 {
            total += map.motion[voice].abs() as u32;
        }
        total
    }

    /// Whether a single voice moves by a single step to reach `other`.
    ///
    /// True for exactly the three Tonnetz neighbours.
    #[must_use]
    pub fn is_parsimonious_to(self, other: Self) -> bool {
        self != other && self.common_tones(other) == 2 && self.voice_leading_distance(other) <= 2
    }

    /// Gives the triad a register, in close root position at `octave`.
    ///
    /// ```
    /// use music_core::{Pitch, Triad};
    ///
    /// assert_eq!(Triad::major(Pitch::C).voice(4).midi(), [60, 64, 67]);
    /// ```
    #[inline]
    #[must_use]
    pub fn voice(self, octave: i16) -> VoicedTriad {
        VoicedTriad::close(self, octave)
    }
}

impl Harmony for Triad {
    #[inline]
    fn pitch_set(&self) -> PitchSet {
        Triad::pitch_set(*self)
    }
}

impl fmt::Display for Triad {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let suffix = if self.is_minor() { "m" } else { "" };
        padded!(f, 8, "{}{}", self.root.name(Spelling::Sharps), suffix)
    }
}

impl fmt::Debug for Triad {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt::Display::fmt(self, f)
    }
}

impl FromStr for Triad {
    type Err = ParseError;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let (root, used) = parse_pitch_prefix(s.as_bytes())?;
        let quality = match &s[used..] {
            "" | "maj" | "M" => TriadQuality::Major,
            "m" | "min" | "-" => TriadQuality::Minor,
            _ => return Err(ParseError::InvalidQuality),
        };
        Ok(Triad { root, quality })
    }
}

// --- Voicing: the impls that follow `Triad` across the crate boundary ------
//
// `Voiced` is defined in `music-core`, so Rust reserves inherent methods on
// it for that crate. `Triad` is local here, which is what makes these impls
// legal, and why `apply` has to arrive as a trait rather than a method.

impl Voiceable for Triad {
    type Octaves = [i16; 3];
    type Pitches = [Pitch; 3];

    #[inline]
    fn pitches(self) -> [Pitch; 3] {
        Triad::pitches(self)
    }

    #[inline]
    fn uniform_octaves(fill: i16) -> [i16; 3] {
        [fill; 3]
    }
}

/// A consonant triad with a register. A voiced [`Triad`].
///
/// ```
/// use music_core::{Interval, Pitch, Triad};
///
/// let v = Triad::major(Pitch::C).voice(4);
/// assert_eq!(v.midi(), [60, 64, 67]);
///
/// let up = v.transpose(Interval::OCTAVE);
/// assert_eq!(v.displacement(up), 36);
/// ```
pub type VoicedTriad = Voiced<Triad>;

impl Transposable for Triad {
    #[inline]
    fn transpose_harmony(self, by: Interval) -> Self {
        self.transpose(by)
    }
}

// --- VoicedTriad: the three-voice instantiation ---------------------------

impl Voiced<Triad> {
    /// The triad being voiced.
    #[inline]
    #[must_use]
    pub fn triad(self) -> Triad {
        self.harmony()
    }

    /// The three sounding notes, in the triad's own voice order.
    ///
    /// That order is root, third, fifth, which is not necessarily ascending:
    /// an inverted voicing puts a later voice lower. Use [`Voiced::bass`] for
    /// the lowest note.
    #[inline]
    #[must_use]
    pub fn notes(self) -> [Note; 3] {
        [self.note_at(0), self.note_at(1), self.note_at(2)]
    }

    /// The three MIDI numbers, in the triad's own voice order.
    ///
    /// ```
    /// use music_core::{Pitch, Triad};
    ///
    /// assert_eq!(Triad::major(Pitch::C).voice(4).midi(), [60, 64, 67]);
    /// ```
    #[inline]
    #[must_use]
    pub fn midi(self) -> [i16; 3] {
        [self.midi_at(0), self.midi_at(1), self.midi_at(2)]
    }
}

// --- Conversions with the general chord type -------------------------------

impl From<Triad> for Chord {
    #[inline]
    fn from(triad: Triad) -> Chord {
        Chord {
            root: triad.root,
            quality: match triad.quality {
                TriadQuality::Major => ChordQuality::Major,
                TriadQuality::Minor => ChordQuality::Minor,
            },
        }
    }
}

impl TryFrom<Chord> for Triad {
    type Error = Chord;

    /// Succeeds only for major and minor chords, the two the PLR group acts on.
    /// The rejected chord is handed back as the error.
    fn try_from(chord: Chord) -> Result<Triad, Chord> {
        // Only the two consonant qualities have a Triad; everything else in
        // the general vocabulary is outside the PLR group's domain.
        match chord.quality {
            ChordQuality::Major => Ok(Triad::major(chord.root)),
            ChordQuality::Minor => Ok(Triad::minor(chord.root)),
            _ => Err(chord),
        }
    }
}

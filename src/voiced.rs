//! Giving an abstract harmony a register.
//!
//! One act, two levels. A [`Note`] is a voiced [`Pitch`], and the `neo-riemann`
//! crate adds a voiced triad the same way. Both are [`Voiced`], which stores
//! the harmony alongside one octave choice per voice.
//!
//! Carrying the harmony is what makes operations on a voicing total: it already
//! knows what it realizes, so nothing has to inspect the notes to work out what
//! they spell.

use core::cmp::Ordering;
use core::fmt::{self, Write as _};
use core::hash::Hash;
use core::ops::{Add, Sub};
use core::str::FromStr;

use crate::pitch::{Interval, Pitch, Spelling, parse_pitch_prefix};
use crate::pitchset::PitchSet;
use crate::{DisplayBuffer, Harmony, ParseError};

/// The MIDI number of a pitch class placed in an octave.
///
/// Scientific pitch notation: octave 4 holds middle C at 60.
#[inline]
pub(crate) const fn midi_of(pitch: Pitch, octave: i16) -> i16 {
    // Widened for the multiply: at the bottom of the MIDI range the
    // intermediate `(octave + 1) * 12` underflows an i16 even though the sum
    // that follows lands back inside it. The assertion still catches an octave
    // absurd enough that no i16 MIDI number can express it.
    let wide = (octave as i32 + 1) * 12 + pitch.value() as i32;
    debug_assert!(
        wide >= i16::MIN as i32 && wide <= i16::MAX as i32,
        "octave too large to express as a MIDI number"
    );
    wide as i16
}

/// A harmony with a fixed number of voices, so it can be given a register.
///
/// The voice count lives here as associated array types rather than as a const
/// parameter on [`Voiced`], which is what keeps a type like `Voiced<Triad>`
/// free of noise at every use site. The [`AsRef`] bounds compile away to
/// nothing.
///
/// [`crate::Chord`] deliberately does not implement this: its cardinality
/// varies between two and six notes, so it has no fixed voice count. Chords
/// are voiced into [`crate::Notes`] instead.
pub trait Voiceable: Copy + PartialEq + Eq + Hash {
    /// One octave per voice.
    type Octaves: Copy + PartialEq + Eq + Hash + AsRef<[i16]> + AsMut<[i16]>;
    /// The pitch classes of this harmony, in its own canonical voice order.
    type Pitches: Copy + AsRef<[Pitch]>;

    /// The pitch classes, in canonical voice order.
    fn pitches(self) -> Self::Pitches;

    /// An octave array with every voice set to the same value.
    fn uniform_octaves(fill: i16) -> Self::Octaves;
}

impl Voiceable for Pitch {
    type Octaves = [i16; 1];
    type Pitches = [Pitch; 1];

    #[inline]
    fn pitches(self) -> [Pitch; 1] {
        [self]
    }

    #[inline]
    fn uniform_octaves(fill: i16) -> [i16; 1] {
        [fill]
    }
}

/// An abstract harmony plus one register choice per voice.
///
/// See [`Note`] for the single-voice instantiation.
///
/// In the OPTIC vocabulary this sits between the other two collection types. It
/// keeps the **O**ctave, unlike [`crate::PitchSet`], so a voicing knows how high
/// it sounds. It is still equivalent under **P**ermutation, because the voices
/// are ordered by chord function rather than by who plays them: index 1 always
/// means "the third". [`crate::Notes`] keeps both.
///
/// That permutation equivalence is why moving a voicing changes which voice
/// holds which note, and why measuring the movement has to search for the
/// cheapest pairing rather than compare index to index.
///
/// Doublings are not expressible here, because the voice count is fixed by the
/// harmony's cardinality. Inversions and spacing are, since those are only
/// octave choices. For doublings, use [`crate::Notes`].
#[derive(Clone, Copy, PartialEq, Eq, Hash)]
pub struct Voiced<H: Voiceable> {
    harmony: H,
    octaves: H::Octaves,
}

/// A pitch class with a register. A voiced [`Pitch`].
///
/// ```
/// use music_core::{Note, Pitch};
///
/// let middle_c: Note = Pitch::C.at(4);
/// assert_eq!(middle_c.midi(), 60);
/// assert_eq!(middle_c.pitch(), Pitch::C);
/// ```
pub type Note = Voiced<Pitch>;

impl<H: Voiceable> Voiced<H> {
    /// Builds a voicing from a harmony and an explicit octave per voice.
    #[inline]
    #[must_use]
    pub fn new(harmony: H, octaves: H::Octaves) -> Self {
        Self { harmony, octaves }
    }

    /// Builds a close-position voicing rooted at `octave`.
    ///
    /// Voice 0 sits in `octave`; every later voice takes the lowest octave that
    /// keeps it above the voice before it. Nothing is doubled and nothing is
    /// spread beyond an octave of the one below.
    #[must_use]
    pub fn close(harmony: H, octave: i16) -> Self {
        let pitches = harmony.pitches();
        let pitches = pitches.as_ref();
        let mut octaves = H::uniform_octaves(octave);
        {
            let slots = octaves.as_mut();
            let mut previous = midi_of(pitches[0], octave);
            slots[0] = octave;
            for i in 1..pitches.len() {
                let mut candidate = octave;
                while midi_of(pitches[i], candidate) <= previous {
                    candidate += 1;
                }
                slots[i] = candidate;
                previous = midi_of(pitches[i], candidate);
            }
        }
        Self { harmony, octaves }
    }

    /// The abstract harmony being voiced.
    #[inline]
    #[must_use]
    pub fn harmony(self) -> H {
        self.harmony
    }

    /// The octave of each voice, in the harmony's canonical order.
    #[inline]
    #[must_use]
    pub fn octaves(self) -> H::Octaves {
        self.octaves
    }

    /// How many voices this voicing has.
    #[inline]
    #[must_use]
    pub fn voices(self) -> usize {
        self.octaves.as_ref().len()
    }

    /// The MIDI number of one voice.
    ///
    /// # Panics
    ///
    /// Panics if `voice` is out of range.
    #[inline]
    #[must_use]
    pub fn midi_at(self, voice: usize) -> i16 {
        midi_of(
            self.harmony.pitches().as_ref()[voice],
            self.octaves.as_ref()[voice],
        )
    }

    /// One voice as a standalone [`Note`].
    ///
    /// # Panics
    ///
    /// Panics if `voice` is out of range.
    #[inline]
    #[must_use]
    pub fn note_at(self, voice: usize) -> Note {
        Note::from_parts(
            self.harmony.pitches().as_ref()[voice],
            self.octaves.as_ref()[voice],
        )
    }

    /// The lowest sounding note.
    #[must_use]
    pub fn bass(self) -> Note {
        let mut best = self.note_at(0);
        for voice in 1..self.voices() {
            let candidate = self.note_at(voice);
            if candidate.midi() < best.midi() {
                best = candidate;
            }
        }
        best
    }

    /// The smallest total semitone motion from this voicing to another.
    ///
    /// Voices are **not** paired by index. A voicing stores one octave per
    /// chord function, so index 1 means "the third" and nothing more; after a
    /// transformation the third of the new chord may be the note that used to
    /// be the root. Pairing by index would compare unrelated notes. This
    /// instead finds the cheapest pairing, which is what a listener hears.
    ///
    /// For a voiced triad the answer always equals the voice-leading distance
    /// between the two triads, because a transformation moves every voice by
    /// its shortest signed path.
    ///
    /// ```
    /// use music_core::{Pitch, Voiced};
    ///
    /// let low = Pitch::C.at(4);
    /// let high = Pitch::C.at(5);
    /// assert_eq!(low.displacement(high), 12);
    /// ```
    #[must_use]
    pub fn displacement(self, other: Self) -> u32 {
        let n = self.voices();
        debug_assert!(n <= MAX_VOICES, "voice count exceeds the assignment solver");

        let mut cost = [[0u32; MAX_VOICES]; MAX_VOICES];
        for (i, row) in cost.iter_mut().enumerate().take(n) {
            let from = self.midi_at(i);
            for (j, slot) in row.iter_mut().enumerate().take(n) {
                *slot = (other.midi_at(j) - from).unsigned_abs() as u32;
            }
        }
        cheapest_assignment(&cost, n)
    }

    /// Moves every voice by the same interval.
    #[must_use]
    pub fn transpose(self, by: Interval) -> Self
    where
        H: Transposable,
    {
        let mut octaves = self.octaves;
        {
            let pitches = self.harmony.pitches();
            let pitches = pitches.as_ref();
            let slots = octaves.as_mut();
            for voice in 0..slots.len() {
                let target = midi_of(pitches[voice], slots[voice]) + by.semitones();
                let pitch = pitches[voice].transpose(by);
                slots[voice] = octave_holding(target, pitch);
            }
        }
        Self {
            harmony: self.harmony.transpose_harmony(by),
            octaves,
        }
    }
}

/// A harmony that can be moved bodily by an interval.
///
/// Separate from [`Voiceable`] so that [`Voiced::transpose`] can rebuild the
/// abstract half as well as the registers.
pub trait Transposable: Voiceable {
    /// Moves the abstract harmony by an interval.
    fn transpose_harmony(self, by: Interval) -> Self;
}

impl Transposable for Pitch {
    #[inline]
    fn transpose_harmony(self, by: Interval) -> Self {
        self.transpose(by)
    }
}

/// The most voices [`cheapest_assignment`] can pair up.
///
/// Three covers a triad and one covers a note, so four is already slack.
const MAX_VOICES: usize = 4;

/// The cheapest one-to-one pairing of `n` source voices onto `n` target voices.
///
/// Exact, not greedy. Walks subsets rather than permutations, so it is 16
/// states rather than 24 orderings at four voices, and it allocates nothing.
fn cheapest_assignment(cost: &[[u32; MAX_VOICES]; MAX_VOICES], n: usize) -> u32 {
    const UNREACHED: u32 = u32::MAX;

    let full = 1usize << n;
    let mut best = [UNREACHED; 1 << MAX_VOICES];
    best[0] = 0;

    for mask in 0..full {
        if best[mask] == UNREACHED {
            continue;
        }
        // Source voices are assigned in order, so the next one to place is
        // however many targets are already taken.
        let source = (mask as u32).count_ones() as usize;
        if source >= n {
            continue;
        }
        for (target, edge) in cost[source].iter().enumerate().take(n) {
            if mask & (1 << target) != 0 {
                continue;
            }
            let next = mask | (1 << target);
            let candidate = best[mask] + edge;
            if candidate < best[next] {
                best[next] = candidate;
            }
        }
    }
    best[full - 1]
}

/// The octave that places `pitch` at MIDI number `midi`.
///
/// `midi` must already be congruent to `pitch` modulo 12, which every caller
/// here guarantees by construction.
#[inline]
pub(crate) const fn octave_holding(midi: i16, pitch: Pitch) -> i16 {
    // Widened to 32 bits for the subtraction: at the very bottom of the MIDI
    // range, `midi - pitch` underflows an i16 before the division brings it
    // back into range. The result always fits.
    ((midi as i32 - pitch.value() as i32) / 12 - 1) as i16
}

impl<H: Voiceable> Harmony for Voiced<H> {
    fn pitch_set(&self) -> PitchSet {
        let mut set = PitchSet::EMPTY;
        for pitch in self.harmony.pitches().as_ref() {
            set = set.insert(*pitch);
        }
        set
    }
}

// --- Note: the one-voice instantiation ------------------------------------

impl Voiced<Pitch> {
    /// Builds a note from a pitch class and an octave.
    ///
    /// Prefer [`Pitch::at`], which reads better at a call site.
    #[inline]
    #[must_use]
    pub const fn from_parts(pitch: Pitch, octave: i16) -> Self {
        Self {
            harmony: pitch,
            octaves: [octave],
        }
    }

    /// Builds a note from a MIDI number, where 60 is middle C.
    #[inline]
    #[must_use]
    pub const fn from_midi(midi: i16) -> Self {
        let pitch = Pitch::new(midi as i32);
        Self {
            harmony: pitch,
            octaves: [octave_holding(midi, pitch)],
        }
    }

    /// The pitch class, with the register discarded.
    #[inline]
    #[must_use]
    pub const fn pitch(self) -> Pitch {
        self.harmony
    }

    /// The octave, in scientific pitch notation.
    #[inline]
    #[must_use]
    pub const fn octave(self) -> i16 {
        self.octaves[0]
    }

    /// The MIDI number, where 60 is middle C.
    ///
    /// Not clamped to the MIDI range of 0 to 127: a note outside it still
    /// answers, so validate before writing a file. Note also that an absurd
    /// octave overflows here, which panics in a debug build.
    #[inline]
    #[must_use]
    pub const fn midi(self) -> i16 {
        midi_of(self.harmony, self.octaves[0])
    }

    /// The frequency in hertz, in equal temperament with concert A at 440.
    ///
    /// Only a note has a frequency. A [`Pitch`] does not, because it has no
    /// register, which is the whole distinction between the two types.
    ///
    /// ```
    /// use music_core::Pitch;
    ///
    /// assert!((Pitch::A.at(4).frequency_hz() - 440.0).abs() < 1e-9);
    /// assert!((Pitch::A.at(5).frequency_hz() - 880.0).abs() < 1e-9);
    /// ```
    #[cfg(feature = "std")]
    #[inline]
    #[must_use]
    pub fn frequency_hz(self) -> f64 {
        self.frequency_hz_at(440.0)
    }

    /// The frequency in hertz against a chosen tuning reference for A4.
    ///
    /// Use it for pitch standards other than 440, such as 432, or baroque
    /// tunings around 415.
    ///
    /// ```
    /// use music_core::Pitch;
    ///
    /// assert!((Pitch::A.at(4).frequency_hz_at(432.0) - 432.0).abs() < 1e-9);
    /// ```
    #[cfg(feature = "std")]
    #[inline]
    #[must_use]
    pub fn frequency_hz_at(self, reference_a4: f64) -> f64 {
        reference_a4 * 2f64.powf(f64::from(self.midi() - 69) / 12.0)
    }
}

impl PartialOrd for Voiced<Pitch> {
    #[inline]
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl Ord for Voiced<Pitch> {
    #[inline]
    fn cmp(&self, other: &Self) -> Ordering {
        self.midi().cmp(&other.midi())
    }
}

impl Add<Interval> for Voiced<Pitch> {
    type Output = Note;
    #[inline]
    fn add(self, rhs: Interval) -> Note {
        Note::from_midi(self.midi() + rhs.semitones())
    }
}

impl Sub<Interval> for Voiced<Pitch> {
    type Output = Note;
    #[inline]
    fn sub(self, rhs: Interval) -> Note {
        Note::from_midi(self.midi() - rhs.semitones())
    }
}

impl Sub<Note> for Voiced<Pitch> {
    type Output = Interval;
    #[inline]
    fn sub(self, rhs: Note) -> Interval {
        Interval::new(self.midi() - rhs.midi())
    }
}

impl<H: Voiceable + fmt::Display> fmt::Display for Voiced<H> {
    /// A single voice prints as the note itself, `"Eb3"`. More than one prints
    /// the harmony followed by its notes, `"Cm [C4 Eb4 G4]"`.
    ///
    /// This is generic rather than one impl per instantiation because `Voiced`
    /// is defined here: a downstream crate cannot implement `Display` for
    /// `Voiced<ItsOwnType>`, so the rule has to live with the type.
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let mut buffer = DisplayBuffer::<96>::new();
        if self.voices() == 1 {
            let note = self.note_at(0);
            let _ = write!(
                buffer,
                "{}{}",
                note.pitch().name(Spelling::Sharps),
                note.octave()
            );
        } else {
            let _ = write!(buffer, "{} [", self.harmony);
            for voice in 0..self.voices() {
                let separator = if voice > 0 { " " } else { "" };
                let note = self.note_at(voice);
                let _ = write!(
                    buffer,
                    "{}{}{}",
                    separator,
                    note.pitch().name(Spelling::Sharps),
                    note.octave()
                );
            }
            let _ = write!(buffer, "]");
        }
        f.pad(buffer.as_str())
    }
}

impl<H: Voiceable + fmt::Display> fmt::Debug for Voiced<H> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt::Display::fmt(self, f)
    }
}

impl FromStr for Voiced<Pitch> {
    type Err = ParseError;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let bytes = s.as_bytes();
        let (pitch, used) = parse_pitch_prefix(bytes)?;
        let rest = &s[used..];
        if rest.is_empty() {
            return Err(ParseError::InvalidOctave);
        }
        let octave: i16 = rest.parse().map_err(|_| ParseError::InvalidOctave)?;
        Ok(pitch.at(octave))
    }
}

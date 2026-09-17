//! A free-form list of sounding notes.

use core::fmt::{self, Write as _};

use crate::Harmony;
use crate::pitch::Interval;
use crate::pitchset::PitchSet;
use crate::voiced::Note;

/// An ordered list of notes with no structural claims.
///
/// In the OPTIC vocabulary this type is equivalent under nothing at all. It
/// keeps the octave, the order you built it in, and any repeats, which is what
/// separates it from [`crate::PitchSet`] and [`crate::Voiced`].
///
/// This is where doublings, spacings and arbitrary output live. Unlike
/// [`crate::VoicedTriad`] it makes no promise about what the notes spell, so it
/// carries no harmony and supports no transformations. It is the bag you hand
/// to a synthesizer.
///
/// Fixed capacity of [`Notes::CAPACITY`], `Copy`, and never allocates.
///
/// ```
/// use music_core::{Notes, Pitch};
///
/// let spread = Notes::from_slice(&[
///     Pitch::C.at(3),
///     Pitch::G.at(3),
///     Pitch::E.at(4),
///     Pitch::C.at(5),      // the root, doubled two octaves up
/// ]).unwrap();
///
/// assert_eq!(spread.len(), 4);
/// assert_eq!(spread.pitch_set().len(), 3);   // still just C, E and G
/// ```
#[derive(Clone, Copy, PartialEq, Eq, Hash)]
pub struct Notes {
    notes: [Note; Self::CAPACITY],
    len: u8,
}

impl Notes {
    /// The most notes this list can hold.
    pub const CAPACITY: usize = 16;

    /// The empty list.
    pub const EMPTY: Self = Self {
        notes: [Note::from_parts(crate::pitch::Pitch::C, 0); Self::CAPACITY],
        len: 0,
    };

    /// Builds a list from a slice, `None` if it exceeds [`Notes::CAPACITY`].
    #[must_use]
    pub fn from_slice(notes: &[Note]) -> Option<Self> {
        if notes.len() > Self::CAPACITY {
            return None;
        }
        let mut out = Self::EMPTY;
        out.notes[..notes.len()].copy_from_slice(notes);
        out.len = notes.len() as u8;
        Some(out)
    }

    /// Appends a note, returning `false` if the list is already full.
    #[must_use = "a full list silently drops the note"]
    pub fn push(&mut self, note: Note) -> bool {
        if self.len as usize >= Self::CAPACITY {
            return false;
        }
        self.notes[self.len as usize] = note;
        self.len += 1;
        true
    }

    /// The notes, in the order they were added.
    #[inline]
    #[must_use]
    pub fn as_slice(&self) -> &[Note] {
        &self.notes[..self.len as usize]
    }

    /// The notes, mutably, in the order they were added.
    #[inline]
    fn as_mut_slice(&mut self) -> &mut [Note] {
        &mut self.notes[..self.len as usize]
    }

    /// How many notes the list holds.
    #[inline]
    #[must_use]
    pub const fn len(&self) -> usize {
        self.len as usize
    }

    /// Whether the list is empty.
    #[inline]
    #[must_use]
    pub const fn is_empty(&self) -> bool {
        self.len == 0
    }

    /// The lowest sounding note, `None` if the list is empty.
    #[must_use]
    pub fn bass(&self) -> Option<Note> {
        self.as_slice().iter().copied().min()
    }

    /// The same notes, reordered from lowest to highest.
    #[must_use]
    pub fn sorted(&self) -> Self {
        let mut out = *self;
        let len = out.len as usize;
        out.notes[..len].sort_unstable();
        out
    }

    /// Moves the lowest note up an octave, returning the notes in order.
    ///
    /// This is what turning a chord over does: the bass climbs above the rest
    /// and the next note up becomes the new bass. Applied once per note it
    /// walks every inversion and arrives back where it started, an octave
    /// higher.
    ///
    /// It is not called `invert`, because that already means mirroring pitch
    /// classes about an axis, on [`crate::Pitch`], [`PitchSet`] and
    /// [`crate::Triad`]. This changes register, not intervals.
    ///
    /// Unlike the rest of this type, it sorts: it takes the notes from lowest
    /// to highest and hands them back the same way. An inversion is a claim
    /// about register order, so there is nothing else for it to mean. An empty
    /// list comes back empty.
    ///
    /// ```
    /// use music_core::{Chord, Pitch};
    ///
    /// let root_position = Chord::major(Pitch::C).voice(4);
    /// assert_eq!(root_position.to_string(), "[C4 E4 G4]");
    ///
    /// let first_inversion = root_position.rotate_up();
    /// assert_eq!(first_inversion.to_string(), "[E4 G4 C5]");
    ///
    /// // The same three pitch classes throughout.
    /// assert_eq!(first_inversion.pitch_set(), root_position.pitch_set());
    /// ```
    #[must_use]
    pub fn rotate_up(&self) -> Self {
        let mut out = self.sorted();
        if let Some(lowest) = out.as_mut_slice().first_mut() {
            *lowest = *lowest + Interval::OCTAVE;
        }
        out.sorted()
    }

    /// Moves the highest note down an octave, returning the notes in order.
    ///
    /// The other direction of [`Notes::rotate_up`], and its inverse. Sorts for
    /// the same reason. An empty list comes back empty.
    #[must_use]
    pub fn rotate_down(&self) -> Self {
        let mut out = self.sorted();
        if let Some(highest) = out.as_mut_slice().last_mut() {
            *highest = *highest - Interval::OCTAVE;
        }
        out.sorted()
    }

    /// Moves every note by the same interval.
    #[must_use]
    pub fn transpose(&self, by: Interval) -> Self {
        let mut out = *self;
        for note in &mut out.notes[..self.len as usize] {
            *note = *note + by;
        }
        out
    }

    /// The pitch classes present, with octaves and doublings collapsed away.
    #[must_use]
    pub fn pitch_set(&self) -> PitchSet {
        let mut set = PitchSet::EMPTY;
        for note in self.as_slice() {
            set = set.insert(note.pitch());
        }
        set
    }

    /// Total semitone motion to another list, pairing voice `i` to voice `i`.
    ///
    /// Index pairing is the right answer here, unlike on [`crate::Voiced`]:
    /// this type keeps whatever order you built it in, so position really does
    /// identify a voice.
    ///
    /// `None` if the two lists have different lengths, since there is then no
    /// pairing to make.
    #[must_use]
    pub fn displacement(&self, other: &Self) -> Option<u32> {
        if self.len != other.len {
            return None;
        }
        let mut total = 0u32;
        for (a, b) in self.as_slice().iter().zip(other.as_slice()) {
            total += (b.midi() - a.midi()).unsigned_abs() as u32;
        }
        Some(total)
    }
}

impl Harmony for Notes {
    #[inline]
    fn pitch_set(&self) -> PitchSet {
        Notes::pitch_set(self)
    }
}

impl Default for Notes {
    fn default() -> Self {
        Self::EMPTY
    }
}

impl fmt::Display for Notes {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let mut buffer = crate::DisplayBuffer::<160>::new();
        let _ = core::write!(buffer, "[");
        for (i, note) in self.as_slice().iter().enumerate() {
            let separator = if i > 0 { " " } else { "" };
            let _ = core::write!(buffer, "{separator}{note}");
        }
        let _ = core::write!(buffer, "]");
        f.pad(buffer.as_str())
    }
}

impl fmt::Debug for Notes {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt::Display::fmt(self, f)
    }
}

impl<'a> IntoIterator for &'a Notes {
    type Item = &'a Note;
    type IntoIter = core::slice::Iter<'a, Note>;

    fn into_iter(self) -> Self::IntoIter {
        self.as_slice().iter()
    }
}

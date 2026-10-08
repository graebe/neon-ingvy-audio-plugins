// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! General music theory primitives: pitch classes, intervals, notes, chords
//! and pitch-class sets.
//!
//! No dependencies, `no_std` by default, and nothing here allocates. Every
//! type is `Copy` and small enough to pass by value without thinking about it.
//!
//! # Two levels
//!
//! A [`Pitch`] is a note name with the octave thrown away. Every C is the same
//! C. There are exactly twelve, numbered 0 to 11 upward from C, and one fits in
//! a single byte.
//!
//! A [`Note`] is that same pitch given a register. Middle C is C in octave 4,
//! MIDI 60.
//!
//! The two are one idea rather than two: `Note` is a type alias for
//! [`Voiced<Pitch>`], so giving something a register is a single act that
//! applies at either level.
//!
//! ```
//! use music_core::{Interval, Note, Pitch};
//!
//! let middle_c: Note = Pitch::C.at(4);
//! assert_eq!(middle_c.midi(), 60);
//! assert_eq!(middle_c.pitch(), Pitch::C);
//!
//! // A pitch wraps at the octave; a note climbs.
//! assert_eq!(Pitch::C.transpose(Interval::OCTAVE), Pitch::C);
//! assert_eq!((middle_c + Interval::OCTAVE).midi(), 72);
//! ```
//!
//! # What builds on this
//!
//! The `neo-riemann` crate adds consonant triads and the PLR transformation
//! group on top of these types. Nothing here depends on that theory, which is
//! the point of the separation.

#![cfg_attr(not(feature = "std"), no_std)]
#![warn(missing_docs)]

pub mod abletonmove;

mod chord;
mod key;
mod name;
mod notes;
mod pitch;
mod pitchset;
mod triad;
mod voiced;

pub use chord::{Chord, ChordQuality, Voicing};
pub use key::{Degree, Key, Mode};
pub use name::{Letter, NoteName};
pub use notes::Notes;
pub use pitch::{EDO, Interval, IntervalClass, Pitch, Spelling, parse_pitch_prefix};
pub use pitchset::{Completions, Interpretations, PitchSet, PitchSetIter};
pub use triad::{Triad, TriadQuality, VoiceMap, VoicedTriad};
pub use voiced::{Note, Transposable, Voiceable, Voiced};

use core::fmt;

/// Runs every code block in `README.md` as a doctest, so the front page cannot
/// drift away from the crate. Exists only while doctests are being collected.
#[cfg(doctest)]
#[doc = include_str!("../README.md")]
pub struct ReadmeExamples;

/// A fixed-size stack buffer that implements [`core::fmt::Write`].
///
/// Exists so that `Display` can render into a string and hand it to
/// [`core::fmt::Formatter::pad`], which is what makes width, alignment and
/// precision actually work. Writing straight to the formatter ignores all of
/// them, so `{:>8}` on a pitch would silently do nothing.
///
/// Every caller sizes `N` above the longest output that type can produce.
///
/// Public so that crates building on this one can implement `Display` the same
/// way without duplicating the buffer.
pub struct DisplayBuffer<const N: usize> {
    bytes: [u8; N],
    len: usize,
}

impl<const N: usize> DisplayBuffer<N> {
    /// An empty buffer.
    #[must_use]
    pub const fn new() -> Self {
        Self {
            bytes: [0; N],
            len: 0,
        }
    }

    /// What has been written so far.
    #[must_use]
    pub fn as_str(&self) -> &str {
        // Only whole `&str` values are ever written, so this is always valid
        // UTF-8. The fallback keeps the function free of panics regardless.
        core::str::from_utf8(&self.bytes[..self.len]).unwrap_or("")
    }
}

impl<const N: usize> Default for DisplayBuffer<N> {
    fn default() -> Self {
        Self::new()
    }
}

impl<const N: usize> fmt::Write for DisplayBuffer<N> {
    fn write_str(&mut self, text: &str) -> fmt::Result {
        let bytes = text.as_bytes();
        let end = self.len + bytes.len();
        if end > N {
            return Err(fmt::Error);
        }
        self.bytes[self.len..end].copy_from_slice(bytes);
        self.len = end;
        Ok(())
    }
}

/// A value paired with how to spell its black keys, for printing.
///
/// `Display` on the music types always uses sharps, so that one value has one
/// text. A display that knows its key wants flats sometimes; this carries that
/// choice to `Display` without changing the value. Made by `spelled` on
/// [`Chord`] and [`Note`].
///
/// ```
/// use music_core::{Pitch, Spelling};
///
/// let note = Pitch::E_FLAT.at(3);
/// assert_eq!(note.to_string(), "D#3");
/// assert_eq!(note.spelled(Spelling::Flats).to_string(), "Eb3");
/// ```
#[derive(Clone, Copy, PartialEq, Eq, Hash)]
pub struct Spelled<T> {
    value: T,
    spelling: Spelling,
}

impl<T: Copy> Spelled<T> {
    /// `value`, to be printed with `spelling`.
    #[inline]
    #[must_use]
    pub const fn new(value: T, spelling: Spelling) -> Self {
        Self { value, spelling }
    }

    /// The value itself.
    #[inline]
    #[must_use]
    pub const fn value(self) -> T {
        self.value
    }

    /// The accidentals it prints with.
    #[inline]
    #[must_use]
    pub const fn spelling(self) -> Spelling {
        self.spelling
    }
}

impl<T: Copy> fmt::Debug for Spelled<T>
where
    Spelled<T>: fmt::Display,
{
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt::Display::fmt(self, f)
    }
}

/// Renders into a stack buffer of `$size` bytes, then pads it.
///
/// Use this instead of writing to the formatter directly, which silently
/// discards width, fill and alignment.
#[macro_export]
macro_rules! padded {
    ($f:expr, $size:literal, $($arg:tt)*) => {{
        let mut buffer = $crate::DisplayBuffer::<$size>::new();
        let _ = core::write!(buffer, $($arg)*);
        $f.pad(buffer.as_str())
    }};
}

/// Anything that reduces to a set of pitch classes.
///
/// Implemented across every harmony type here, so generic code can treat a
/// named [`Chord`], a raw [`PitchSet`] and a realized [`Notes`] list uniformly.
/// Dispatch is static and the calls inline away.
pub trait Harmony {
    /// The pitches this harmony contains, as a 12-bit set.
    fn pitch_set(&self) -> PitchSet;
}

/// Error returned when a string cannot be parsed into one of these types.
///
/// Parsing exists for genuine user input. In code, prefer the constructors:
/// [`Chord::min7`], [`Pitch::C`] and friends.
#[derive(Clone, Copy, PartialEq, Eq, Hash, Debug)]
#[non_exhaustive]
pub enum ParseError {
    /// The input was empty.
    Empty,
    /// The input did not start with a note letter A through G.
    InvalidPitch,
    /// The octave was missing, malformed, or out of range.
    InvalidOctave,
    /// The chord or triad quality was not recognised.
    InvalidQuality,
    /// The transformation name was not recognised.
    InvalidOperation,
    /// Parsing succeeded but characters were left over.
    Trailing,
}

impl fmt::Display for ParseError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let msg = match self {
            ParseError::Empty => "empty input",
            ParseError::InvalidPitch => "expected a note letter A-G",
            ParseError::InvalidOctave => "invalid octave",
            ParseError::InvalidQuality => "unrecognised chord quality",
            ParseError::InvalidOperation => "unrecognised transformation",
            ParseError::Trailing => "unexpected trailing characters",
        };
        f.write_str(msg)
    }
}

#[cfg(feature = "std")]
impl std::error::Error for ParseError {}

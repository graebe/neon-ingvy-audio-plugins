// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! Note names as a score writes them: a letter and its accidentals.
//!
//! A [`Pitch`] is one of twelve values and knows no letter; B flat and A sharp
//! are the same pitch. A score cannot leave it there, because the letter
//! decides the line a note sits on: B flat sits on the B line, A sharp on the
//! A line. A [`NoteName`] is the pitch with its letter chosen. [`Key::name_of`]
//! chooses it the way the key would, and [`NoteName::of`] the plain way, always
//! sharps or always flats.
//!
//! [`Key::name_of`]: crate::Key::name_of

use core::fmt::{self, Write as _};

use crate::DisplayBuffer;
use crate::pitch::{Pitch, Spelling};
use crate::voiced::Note;

/// One of the seven note letters.
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Debug, Default)]
#[repr(u8)]
pub enum Letter {
    /// C.
    #[default]
    C,
    /// D.
    D,
    /// E.
    E,
    /// F.
    F,
    /// G.
    G,
    /// A.
    A,
    /// B.
    B,
}

impl Letter {
    /// All seven, upward from C.
    pub const ALL: [Letter; 7] = [
        Letter::C,
        Letter::D,
        Letter::E,
        Letter::F,
        Letter::G,
        Letter::A,
        Letter::B,
    ];

    /// The letter `steps` letters above this one, wrapping: B plus one is C.
    #[inline]
    #[must_use]
    pub const fn up(self, steps: i32) -> Letter {
        Self::ALL[(self as i32 + steps).rem_euclid(7) as usize]
    }

    /// Its place upward from C, 0 to 6.
    #[inline]
    #[must_use]
    pub const fn index(self) -> u8 {
        self as u8
    }

    /// The pitch class of the letter with no accidental.
    #[inline]
    #[must_use]
    pub const fn natural(self) -> Pitch {
        Pitch::new([0, 2, 4, 5, 7, 9, 11][self as usize])
    }

    /// The letter itself, `"C"` to `"B"`.
    #[must_use]
    pub const fn name(self) -> &'static str {
        ["C", "D", "E", "F", "G", "A", "B"][self as usize]
    }
}

/// A pitch class written with a letter: `Bb`, `F#`, `E#`, `Cbb`.
///
/// Two bytes, `Copy`, and total: any letter with any accidental names some
/// pitch, though only -2 to +2 are ever chosen here.
///
/// ```
/// use music_core::{Letter, NoteName, Pitch, Spelling};
///
/// let b_flat = NoteName::of(Pitch::B_FLAT, Spelling::Flats);
/// assert_eq!(b_flat.letter(), Letter::B);
/// assert_eq!(b_flat.to_string(), "Bb");
/// assert_eq!(NoteName::new(Letter::E, 1).pitch(), Pitch::F);   // E sharp
/// ```
#[derive(Clone, Copy, PartialEq, Eq, Hash, Default)]
pub struct NoteName {
    letter: Letter,
    accidental: i8,
}

impl NoteName {
    /// `letter` raised by `accidental` semitones (lowered when negative).
    #[inline]
    #[must_use]
    pub const fn new(letter: Letter, accidental: i8) -> Self {
        Self { letter, accidental }
    }

    /// `pitch` named the plain way: a white key by its letter, a black key as
    /// the letter below and a sharp, or the letter above and a flat.
    #[must_use]
    pub const fn of(pitch: Pitch, spelling: Spelling) -> Self {
        let mut i = 0;
        while i < 7 {
            if Letter::ALL[i].natural().value() == pitch.value() {
                return Self::new(Letter::ALL[i], 0);
            }
            i += 1;
        }
        match spelling {
            Spelling::Sharps => Self::new(Self::below(pitch), 1),
            Spelling::Flats => Self::new(Self::below(pitch).up(1), -1),
        }
    }

    /// The white key a semitone below a black one.
    const fn below(black: Pitch) -> Letter {
        let mut i = 0;
        while i < 7 {
            if Letter::ALL[i].natural().value() == (black.value() + 11) % 12 {
                return Letter::ALL[i];
            }
            i += 1;
        }
        Letter::C
    }

    /// The letter.
    #[inline]
    #[must_use]
    pub const fn letter(self) -> Letter {
        self.letter
    }

    /// The accidentals: sharps positive, flats negative.
    #[inline]
    #[must_use]
    pub const fn accidental(self) -> i8 {
        self.accidental
    }

    /// The pitch class it names.
    #[inline]
    #[must_use]
    pub const fn pitch(self) -> Pitch {
        Pitch::new(self.letter.natural().value() as i32 + self.accidental as i32)
    }

    /// The octave a score writes `note` in under this name, which is not
    /// always the note's own: C flat 4 sounds as B 3, and B sharp 3 as C 4.
    ///
    /// ```
    /// use music_core::{Letter, NoteName, Pitch};
    ///
    /// let c_flat = NoteName::new(Letter::C, -1);
    /// assert_eq!(c_flat.octave_of(Pitch::B.at(3)), 4);
    /// ```
    #[must_use]
    pub const fn octave_of(self, note: Note) -> i16 {
        let natural = note.midi() as i32 - self.accidental as i32;
        (natural.div_euclid(12) - 1) as i16
    }

    /// How far up the staff `note` sits under this name, in letter steps from
    /// C in octave 0: one more for each line and each space.
    #[must_use]
    pub const fn staff_step(self, note: Note) -> i32 {
        self.octave_of(note) as i32 * 7 + self.letter as i32
    }
}

impl fmt::Display for NoteName {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let mut buffer = DisplayBuffer::<8>::new();
        let _ = buffer.write_str(self.letter.name());
        let mark = if self.accidental < 0 { "b" } else { "#" };
        for _ in 0..self.accidental.unsigned_abs().min(3) {
            let _ = buffer.write_str(mark);
        }
        f.pad(buffer.as_str())
    }
}

impl fmt::Debug for NoteName {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt::Display::fmt(self, f)
    }
}

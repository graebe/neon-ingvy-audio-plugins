// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
A reading as the words a display shows.

Every function writes into a `core::fmt::Write` -- a fixed buffer on the audio
thread -- and allocates nothing.

  name         the readout:          Am7/C      C4      C5      C–E
  description  the line under it:    A minor 7 · 1st inversion
  degree       the roman numeral:    vi7
  notes        what sounds:          C3 E3 G3 A4
  chord        another reading:      C6/C

HOW A NOTE IS NAMED is [`Names`]: the key's way, letter by letter (`Bb` in C
major, `E#` in F# major, `G#` in A minor), or plainly with sharps or flats
throughout. The Spelling parameter picks, and Auto is the key's way.

The description is in sentence case; a display that sets it in capitals does
so itself.
*/

use core::fmt::{self, Write};

use music_core::{Chord, Interval, Key, Note, NoteName, Pitch, Spelling};

use crate::reading::{Kind, Reading};
use crate::sounding::notes_of;

/// How notes are named.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Names {
    /// As a score in this key writes them.
    Key(Key),
    /// Always with sharps, or always with flats.
    Fixed(Spelling),
}

impl Names {
    /// The name of a pitch class.
    pub const fn of(self, pitch: Pitch) -> NoteName {
        match self {
            Names::Key(key) => key.name_of(pitch),
            Names::Fixed(spelling) => NoteName::of(pitch, spelling),
        }
    }

    /// A note with its written octave: `Bb3`, `Cb5`.
    pub fn write_note(self, note: Note, w: &mut impl Write) -> fmt::Result {
        let name = self.of(note.pitch());
        write!(w, "{}{}", name, name.octave_of(note))
    }

    /// A chord: its root, its quality's symbol (or its shape, unnamed), and
    /// a slash and the bass when that is not the root.
    pub fn write_chord(self, chord: Chord, w: &mut impl Write) -> fmt::Result {
        write!(w, "{}", self.of(chord.root()))?;
        match chord.quality() {
            Some(quality) => w.write_str(quality.symbol())?,
            None => {
                w.write_str("[")?;
                for (i, offset) in chord.intervals().iter().enumerate() {
                    write!(w, "{}{}", if i > 0 { "," } else { "" }, offset.value())?;
                }
                w.write_str("]")?;
            }
        }
        if chord.is_inverted() {
            write!(w, "/{}", self.of(chord.bass()))?;
        }
        Ok(())
    }
}

/// The ordinal suffix of a small number: 1st, 2nd, 3rd, 4th.
fn ordinal(n: u8) -> &'static str {
    match n {
        1 => "st",
        2 => "nd",
        3 => "rd",
        _ => "th",
    }
}

/// The readout.
pub fn write_name(r: &Reading, names: Names, w: &mut impl Write) -> fmt::Result {
    match r.kind() {
        Kind::Empty => Ok(()),
        Kind::Note => match r.bass() {
            Some(note) => names.write_note(note, w),
            None => Ok(()),
        },
        Kind::Interval => match (r.chord(), r.interval()) {
            (Some(chord), _) => names.write_chord(chord, w),
            (None, Some((low, high))) => {
                write!(w, "{}–{}", names.of(low.pitch()), names.of(high.pitch()))
            }
            (None, None) => Ok(()),
        },
        Kind::Chord => match r.chord() {
            Some(chord) => names.write_chord(chord, w),
            None => Ok(()),
        },
        Kind::Unnamed => {
            let mut first = true;
            for pitch in r.pitch_classes() {
                if !first {
                    w.write_str(" ")?;
                }
                first = false;
                write!(w, "{}", names.of(pitch))?;
            }
            Ok(())
        }
    }
}

/// The line under the readout.
pub fn write_description(r: &Reading, names: Names, w: &mut impl Write) -> fmt::Result {
    match r.kind() {
        Kind::Empty => Ok(()),
        Kind::Note => {
            let Some(note) = r.bass() else { return Ok(()) };
            let octaves = r.notes().count_ones();
            if octaves > 1 {
                write!(w, "{} in {} octaves", names.of(note.pitch()), octaves)
            } else {
                write!(w, "note · MIDI {}", note.midi())
            }
        }
        Kind::Interval => {
            let Some((low, high)) = r.interval() else {
                return Ok(());
            };
            let size: Interval = high - low;
            w.write_str(size.name())?;
            match size.compound_octaves() {
                0 => {}
                1 => w.write_str(" + 1 octave")?,
                n => write!(w, " + {n} octaves")?,
            }
            if r.chord().is_some() {
                w.write_str(" · power chord")?;
            }
            Ok(())
        }
        Kind::Chord => {
            let Some(chord) = r.chord() else {
                return Ok(());
            };
            write_chord_words(chord, names, w)
        }
        Kind::Unnamed => write!(
            w,
            "no common name · {} pitch classes",
            r.pitch_classes().len()
        ),
    }
}

/// "A minor 7 · 1st inversion", or "· over F#" for a bass that is not one of
/// the chord's own notes.
fn write_chord_words(chord: Chord, names: Names, w: &mut impl Write) -> fmt::Result {
    let root = names.of(chord.root());
    match chord.quality() {
        Some(quality) => write!(w, "{root} {}", quality.name())?,
        None => write!(w, "{root} chord")?,
    }
    if chord.is_inverted() {
        let inversion = chord.inversion();
        if inversion > 0 {
            write!(w, " · {inversion}{} inversion", ordinal(inversion))?;
        } else {
            write!(w, " · over {}", names.of(chord.bass()))?;
        }
    }
    Ok(())
}

/// The roman numeral, for a reading that has a chord.
pub fn write_degree(r: &Reading, w: &mut impl Write) -> fmt::Result {
    match r.degree() {
        Some(degree) => write!(w, "{degree}"),
        None => Ok(()),
    }
}

/// The sounding notes, lowest first, separated by spaces.
pub fn write_notes(r: &Reading, names: Names, w: &mut impl Write) -> fmt::Result {
    let mut first = true;
    for midi in notes_of(r.notes()) {
        if !first {
            w.write_str(" ")?;
        }
        first = false;
        names.write_note(Note::from_midi(midi as i16), w)?;
    }
    Ok(())
}

/// One alternative reading.
pub fn write_chord(chord: Chord, names: Names, w: &mut impl Write) -> fmt::Result {
    names.write_chord(chord, w)
}

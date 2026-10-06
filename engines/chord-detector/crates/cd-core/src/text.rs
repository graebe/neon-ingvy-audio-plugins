// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
A reading as the words a display shows.

Every function writes into a `core::fmt::Write` -- a fixed buffer on the audio
thread -- and allocates nothing. Spelling is decided by the caller, once, from
the Spelling parameter and the key.

  name         the readout:          Am7/C      C4      C5      C–E
  description  the line under it:    A minor 7 · 1st inversion
  degree       the roman numeral:    vi7
  notes        what sounds:          C3 E3 G3 A4
  alternative  another reading:      C6/C

The description is in sentence case; a display that sets it in capitals does
so itself.
*/

use core::fmt::{self, Write};

use music_core::{Chord, Interval, Spelling};

use crate::reading::{Kind, Reading};
use crate::sounding::notes_of;

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
pub fn write_name(r: &Reading, spelling: Spelling, w: &mut impl Write) -> fmt::Result {
    match r.kind() {
        Kind::Empty => Ok(()),
        Kind::Note => match r.bass() {
            Some(note) => write!(w, "{}", note.spelled(spelling)),
            None => Ok(()),
        },
        Kind::Interval => match (r.chord(), r.interval()) {
            (Some(chord), _) => write!(w, "{}", chord.spelled(spelling)),
            (None, Some((low, high))) => write!(
                w,
                "{}–{}",
                low.pitch().name(spelling),
                high.pitch().name(spelling)
            ),
            (None, None) => Ok(()),
        },
        Kind::Chord => match r.chord() {
            Some(chord) => write!(w, "{}", chord.spelled(spelling)),
            None => Ok(()),
        },
        Kind::Unnamed => {
            let mut first = true;
            for pitch in r.pitch_classes() {
                if !first {
                    w.write_str(" ")?;
                }
                first = false;
                w.write_str(pitch.name(spelling))?;
            }
            Ok(())
        }
    }
}

/// The line under the readout.
pub fn write_description(r: &Reading, spelling: Spelling, w: &mut impl Write) -> fmt::Result {
    match r.kind() {
        Kind::Empty => Ok(()),
        Kind::Note => {
            let Some(note) = r.bass() else { return Ok(()) };
            let octaves = r.notes().count_ones();
            if octaves > 1 {
                write!(w, "{} in {} octaves", note.pitch().name(spelling), octaves)
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
            write_chord_words(chord, spelling, w)
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
fn write_chord_words(chord: Chord, spelling: Spelling, w: &mut impl Write) -> fmt::Result {
    let root = chord.root().name(spelling);
    match chord.quality() {
        Some(quality) => write!(w, "{root} {}", quality.name())?,
        None => write!(w, "{root} chord")?,
    }
    if chord.is_inverted() {
        let inversion = chord.inversion();
        if inversion > 0 {
            write!(w, " · {inversion}{} inversion", ordinal(inversion))?;
        } else {
            write!(w, " · over {}", chord.bass().name(spelling))?;
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
pub fn write_notes(r: &Reading, spelling: Spelling, w: &mut impl Write) -> fmt::Result {
    let mut first = true;
    for midi in notes_of(r.notes()) {
        if !first {
            w.write_str(" ")?;
        }
        first = false;
        write!(
            w,
            "{}",
            music_core::Note::from_midi(midi as i16).spelled(spelling)
        )?;
    }
    Ok(())
}

/// One alternative reading.
pub fn write_chord(chord: Chord, spelling: Spelling, w: &mut impl Write) -> fmt::Result {
    write!(w, "{}", chord.spelled(spelling))
}

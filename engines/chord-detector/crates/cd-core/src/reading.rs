// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
What the sounding notes are called: a note, an interval, a chord, or a set
with no name.

THE NAME IS music-core's. `Notes::identify` reads a voicing by its bass -- the
lowest note decides between a chord and its inversion -- and falls back to the
pitch classes with the bass as a slash. This file only decides which kind of
answer fits how many notes there are, and gathers what a display shows beside
the name.

THE KIND, by how many notes and how many pitch classes:

  no notes                    Empty
  one pitch class             Note: one note, or the same note in octaves
  exactly two notes           Interval, from the lower to the upper; a fifth
                              also carries its chord, `C5`
  three or more, named        Chord -- C3 G3 C4 too: two classes a fifth
                              apart are the power chord `C5`
  three or more, two classes  Interval between the bass and the other class,
  with no name                C3 E3 C4 say
  three or more, unnamed      Unnamed

SIXTEEN NOTES AT MOST ARE NAMED, the lowest sixteen: `Notes` holds that many,
and the bass -- the one note that decides an inversion -- is always among
them. A seventeenth note changes nothing a name can say; the keyboard and the
history still show every note.

ALTERNATIVES are the other roots the same pitch classes can be read from: C6 is
also Am7, a diminished seventh is four chords. Each is shown over the sounding
bass, so it reads as what it would be in this voicing.
*/

use music_core::{Chord, Degree, Key, Note, Notes, PitchSet};

use crate::sounding::{notes_of, NoteSet};

/// The most alternatives a reading carries. Twelve roots is the most a set
/// can have, and a display has room for fewer.
pub const ALTERNATIVES_MAX: usize = 8;

/// Which kind of answer a reading is.
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
#[repr(u8)]
pub enum Kind {
    /// Nothing is sounding.
    #[default]
    Empty = 0,
    /// One pitch class, in one octave or several.
    Note = 1,
    /// Two pitch classes.
    Interval = 2,
    /// Three or more, with a name.
    Chord = 3,
    /// Three or more, with no name.
    Unnamed = 4,
}

/// Everything a display shows about the sounding notes.
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub struct Reading {
    kind: Kind,
    /// Shown after release because Hold is on, rather than sounding now.
    held: bool,
    notes: NoteSet,
    /// The two notes an interval is between, lower first.
    interval: Option<(Note, Note)>,
    chord: Option<Chord>,
    alternatives: [Option<Chord>; ALTERNATIVES_MAX],
    degree: Option<Degree>,
}

impl Reading {
    /// Read `notes` in `key`.
    pub fn of(notes: NoteSet, key: Key) -> Reading {
        let mut reading = Reading {
            notes,
            ..Reading::default()
        };
        let count = notes.count_ones();
        if count == 0 {
            return reading;
        }

        let mut voicing = Notes::EMPTY;
        for midi in notes_of(notes).take(Notes::CAPACITY) {
            let _ = voicing.push(Note::from_midi(midi as i16));
        }
        let classes = pitch_classes(notes);
        let lowest = notes_of(notes).next().map(|m| Note::from_midi(m as i16));

        if classes.len() == 1 {
            reading.kind = Kind::Note;
            return reading;
        }

        reading.chord = voicing.identify();
        if count == 2 || (reading.chord.is_none() && classes.len() == 2) {
            reading.kind = Kind::Interval;
            if let Some(low) = lowest {
                let other = notes_of(notes)
                    .map(|m| Note::from_midi(m as i16))
                    .find(|n| n.pitch() != low.pitch());
                reading.interval = other.map(|high| (low, high));
            }
        } else if reading.chord.is_some() {
            reading.kind = Kind::Chord;
        } else {
            reading.kind = Kind::Unnamed;
        }

        if let Some(chord) = reading.chord {
            reading.degree = Some(key.degree_of(chord));
            if reading.kind == Kind::Chord {
                let mut slot = 0;
                for other in classes.interpretations() {
                    if slot == ALTERNATIVES_MAX {
                        break;
                    }
                    if other.root() != chord.root() {
                        reading.alternatives[slot] = Some(other.over(chord.bass()));
                        slot += 1;
                    }
                }
            }
        }
        reading
    }

    /// The same reading, marked as held over after release.
    pub const fn held_over(self) -> Reading {
        Reading { held: true, ..self }
    }

    /// Which kind of answer this is.
    pub const fn kind(&self) -> Kind {
        self.kind
    }

    /// Whether this is shown because Hold kept it, not because it sounds.
    pub const fn is_held(&self) -> bool {
        self.held
    }

    /// The notes it was read from.
    pub const fn notes(&self) -> NoteSet {
        self.notes
    }

    /// Their pitch classes.
    pub fn pitch_classes(&self) -> PitchSet {
        pitch_classes(self.notes)
    }

    /// The lowest note, if any.
    pub fn bass(&self) -> Option<Note> {
        notes_of(self.notes)
            .next()
            .map(|m| Note::from_midi(m as i16))
    }

    /// The chord, for a chord, and for an interval that has a name (a fifth).
    pub const fn chord(&self) -> Option<Chord> {
        self.chord
    }

    /// The two notes of an interval, lower first.
    pub const fn interval(&self) -> Option<(Note, Note)> {
        self.interval
    }

    /// The chord's degree in the key it was read in.
    pub const fn degree(&self) -> Option<Degree> {
        self.degree
    }

    /// The other readings of the same pitch classes, over the same bass.
    pub fn alternatives(&self) -> impl Iterator<Item = Chord> + '_ {
        self.alternatives.iter().map_while(|c| *c)
    }
}

/// The pitch classes of a set of notes.
pub fn pitch_classes(notes: NoteSet) -> PitchSet {
    let mut bits = 0u16;
    for midi in notes_of(notes) {
        bits |= 1 << (midi % 12);
    }
    PitchSet::from_bits_truncating(bits)
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! Where the notes sit on an Ableton Move's pads.
//!
//! The Move has thirty-two pads in four rows of eight. Played in a key, each pad
//! sounds a scale degree rather than a chromatic step, and each row starts a
//! fixed number of degrees above the one below it — three by default, so a row
//! begins a fourth above the one beneath it. That mirrors the chromatic layout,
//! where rows are five semitones apart, and it is what Ableton's in-key layouts
//! do.
//!
//! This module is the mapping and nothing else: which note a pad sounds, which
//! degree it is, and which part of a chord it is playing. **Deciding what colour
//! to light it is the caller's job**, and so is talking to the hardware. That
//! keeps the crate free of the MIDI I/O its scope excludes, and leaves the one
//! thing that is genuinely music theory here.
//!
//! ```
//! use music_core::abletonmove::{PadGrid, PadRole, COLUMNS};
//! use music_core::{Pitch, Triad};
//!
//! // C major, starting at C4.
//! let grid = PadGrid::new(Pitch::C, 4);
//!
//! // The bottom-left pad is the tonic; the next one along is the second.
//! assert_eq!(grid.note_at(0, 0).midi(), 60);
//! assert_eq!(grid.note_at(0, 1).midi(), 62);
//! assert_eq!(grid.degree_at(0, 0), 1);
//!
//! // Each row starts three degrees higher, so the pad above the tonic is the
//! // fourth — F, a fourth up.
//! assert_eq!(grid.note_at(1, 0).midi(), 65);
//! assert_eq!(grid.degree_at(1, 0), 4);
//!
//! // A triad lies along a row, on every other pad.
//! let c_major = Triad::major(Pitch::C);
//! assert_eq!(grid.role_at(0, 0, c_major), Some(PadRole::Root));
//! assert_eq!(grid.role_at(0, 2, c_major), Some(PadRole::Third));
//! assert_eq!(grid.role_at(0, 4, c_major), Some(PadRole::Fifth));
//! assert_eq!(grid.role_at(0, 1, c_major), None);
//! # let _ = COLUMNS;
//! ```

use crate::{Note, Pitch, Triad};

/// Rows of pads, bottom to top.
pub const ROWS: usize = 4;

/// Pads across a row, left to right.
pub const COLUMNS: usize = 8;

/// Pads in total.
pub const PADS: usize = ROWS * COLUMNS;

/// Semitones above the tonic for each degree of the major scale.
///
/// Private on purpose. `music-core` has no scale type yet, and a seven-element
/// array inside one device module should not pre-empt the design of one.
const MAJOR: [u8; 7] = [0, 2, 4, 5, 7, 9, 11];

/// How many scale degrees each row starts above the row below it.
///
/// Three is Ableton's own default for playing in key, putting each row a fourth
/// above the last — the same interval the chromatic layout uses.
pub const DEFAULT_ROW_OFFSET: u8 = 3;

/// How the pads are laid out.
#[derive(Clone, Copy, PartialEq, Eq, Hash, Debug, Default)]
pub enum Layout {
    /// Only the notes of the key, so nothing played can be out of it. Pads are
    /// numbered by scale degree.
    #[default]
    InKey,
    /// Every semitone. Wider to reach around, but it can show any note at all —
    /// which an in-key grid cannot, since a chord may borrow from outside.
    Chromatic,
}

/// Which part of a chord a pad is sounding.
///
/// Says what the note *is*, not what colour to make it — the palette belongs to
/// whatever is doing the lighting.
#[derive(Clone, Copy, PartialEq, Eq, Hash, Debug)]
pub enum PadRole {
    /// The chord's root.
    Root,
    /// Its third, major or minor.
    Third,
    /// Its fifth.
    Fifth,
}

impl PadRole {
    /// The role's position in the chord, counting the root as zero.
    ///
    /// Useful as an index into a palette, which is the usual reason to ask.
    pub const fn index(self) -> u8 {
        match self {
            Self::Root => 0,
            Self::Third => 1,
            Self::Fifth => 2,
        }
    }
}

/// The notes laid out across a Move's pads, in one key.
///
/// `Copy` and four bytes wide, like everything else here.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct PadGrid {
    tonic: Pitch,
    octave: i16,
    row_offset: u8,
    layout: Layout,
}

impl PadGrid {
    /// A grid in the major scale on `tonic`, with the bottom-left pad in
    /// `octave`.
    pub const fn new(tonic: Pitch, octave: i16) -> Self {
        Self {
            tonic,
            octave,
            row_offset: DEFAULT_ROW_OFFSET,
            layout: Layout::InKey,
        }
    }

    /// The same grid laid out chromatically, every semitone to a pad.
    ///
    /// Rows stay a fourth apart, as they are in key; here that is five
    /// semitones rather than three degrees.
    pub const fn chromatic(self) -> Self {
        Self {
            layout: Layout::Chromatic,
            ..self
        }
    }

    /// How this grid is laid out.
    pub const fn layout(self) -> Layout {
        self.layout
    }

    /// The same grid with rows a different number of scale degrees apart.
    ///
    /// Seven would stack the rows in octaves; two would stack them in thirds, so
    /// a triad sits under three pads in a column; one would lay the whole grid
    /// out as a single run of the scale.
    pub const fn with_row_offset(self, steps: u8) -> Self {
        Self {
            row_offset: steps,
            ..self
        }
    }

    /// The key this grid is in.
    pub const fn tonic(self) -> Pitch {
        self.tonic
    }

    /// The octave of the bottom-left pad.
    pub const fn octave(self) -> i16 {
        self.octave
    }

    /// How many scale degrees apart the rows are.
    pub const fn row_offset(self) -> u8 {
        self.row_offset
    }

    /// How many scale degrees above the bottom-left pad this one sits.
    ///
    /// Rows are numbered from the bottom, matching how the pads are played
    /// rather than how they are usually drawn.
    const fn steps_at(self, row: usize, column: usize) -> usize {
        let offset = match self.layout {
            // A fourth either way: three degrees in key, five semitones out.
            Layout::InKey => self.row_offset as usize,
            Layout::Chromatic => 5,
        };
        column + offset * row
    }

    /// The degree this pad plays, 1 through 7 in key.
    ///
    /// This is the number the pad is usually labelled with. A chromatic grid
    /// has no degrees, so it reports the semitone above the tonic instead,
    /// 1 through 12.
    pub const fn degree_at(self, row: usize, column: usize) -> u8 {
        match self.layout {
            Layout::InKey => (self.steps_at(row, column) % 7) as u8 + 1,
            Layout::Chromatic => (self.steps_at(row, column) % 12) as u8 + 1,
        }
    }

    /// The note this pad sounds.
    ///
    /// Out-of-range coordinates keep counting rather than failing: the layout is
    /// an arithmetic progression, so asking about a ninth column is a sensible
    /// question with a sensible answer.
    pub const fn note_at(self, row: usize, column: usize) -> Note {
        let steps = self.steps_at(row, column);
        let semitones = match self.layout {
            Layout::InKey => MAJOR[steps % 7] as i16 + 12 * (steps / 7) as i16,
            Layout::Chromatic => steps as i16,
        };
        // Work in absolute MIDI rather than octave-plus-pitch: a scale step can
        // carry past the octave boundary, and `from_midi` settles that in one
        // place instead of every caller having to.
        let tonic_midi = 12 * (self.octave + 1) + self.tonic.value() as i16;
        Note::from_midi(tonic_midi + semitones)
    }

    /// Every pad's note, bottom-left first and reading along each row.
    ///
    /// A fixed array: nothing here allocates.
    pub fn notes(self) -> [Note; PADS] {
        let mut out = [self.note_at(0, 0); PADS];
        let mut index = 0;
        while index < PADS {
            out[index] = self.note_at(index / COLUMNS, index % COLUMNS);
            index += 1;
        }
        out
    }

    /// Which part of `triad` this pad is sounding, if any.
    ///
    /// Compares pitch classes, so a pad two octaves up from the root still
    /// answers [`PadRole::Root`] — which is what lighting a chord wants.
    pub fn role_at(self, row: usize, column: usize, triad: Triad) -> Option<PadRole> {
        let pitch = self.note_at(row, column).pitch();
        let [root, third, fifth] = triad.pitches();
        if pitch.value() == root.value() {
            Some(PadRole::Root)
        } else if pitch.value() == third.value() {
            Some(PadRole::Third)
        } else if pitch.value() == fifth.value() {
            Some(PadRole::Fifth)
        } else {
            None
        }
    }
}

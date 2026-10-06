// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
NI Chord-Detector's engine: MIDI in, the name of what is sounding out.

It makes no sound. The plugin is a silent instrument whose only output is
what its window shows, so the engine's whole job is three readings of one
MIDI stream:

  - what is sounding now, and what it is called (`Reading`, from music-core);
  - every note's start and end on a clock that never goes back, for the
    scrolling history (`NoteEvent`, `Timeline`);
  - the settings both are read with: the key, the spelling, Hold (`params`).

ONE THREAD. A `Detector` belongs to the audio thread, as every engine here
does; the C ABI's shell carries its readings to the editor. Nothing in it
allocates after `new`, which `tests/no_alloc.rs` holds it to.

A READING IS RECOMPUTED ONLY WHEN SOMETHING IT SHOWS CHANGED -- the sounding
notes, the key, or Hold -- and each recomputation bumps `serial`, so a reader
can tell a new reading from the same one read twice.
*/

pub mod params;
pub mod reading;
pub mod sounding;
pub mod text;
pub mod timeline;

pub use ground_core::Transport;
pub use music_core::{Key, Mode, Pitch, Spelling};
pub use params::{Param, PARAM_COUNT};
pub use reading::{Kind, Reading};
pub use sounding::NoteSet;
pub use timeline::Timeline;

use sounding::{Effect, Sounding};

/// A note starting or stopping, on the history's clock.
#[derive(Clone, Copy, PartialEq, Debug)]
pub struct NoteEvent {
    /// The MIDI note.
    pub note: u8,
    /// The note-on's velocity, or 0 for a note that stopped sounding.
    pub velocity: u8,
    /// When, in quarters on the [`Timeline`].
    pub at: f64,
}

/// The engine.
pub struct Detector {
    sounding: Sounding,
    values: [u8; PARAM_COUNT],
    reading: Reading,
    serial: u32,
    timeline: Timeline,
}

impl Detector {
    /// A detector at `sample_rate`, every parameter at its default.
    pub fn new(sample_rate: f64) -> Detector {
        let mut values = [0u8; PARAM_COUNT];
        for p in Param::ALL {
            values[p as usize] = p.info().default;
        }
        Detector {
            sounding: Sounding::default(),
            values,
            reading: Reading::default(),
            serial: 0,
            timeline: Timeline::new(sample_rate),
        }
    }

    /// The host's rate changed.
    pub fn set_sample_rate(&mut self, sample_rate: f64) {
        self.timeline.set_sample_rate(sample_rate);
    }

    /// The top of a block: the host's clock, if it reported one.
    pub fn begin_block(&mut self, transport: Option<&Transport>) {
        self.timeline.begin(transport);
    }

    /// The bottom of a block of `frames` samples.
    pub fn end_block(&mut self, frames: usize) {
        self.timeline.end(frames);
    }

    /// One MIDI message, `offset` samples into the block. Every note that
    /// starts or stops sounding because of it is handed to `emit`.
    pub fn on_midi(&mut self, msg: &[u8], offset: u32, mut emit: impl FnMut(NoteEvent)) {
        let before = self.sounding.notes();
        let effect = self.sounding.apply(msg);
        let velocity = match effect {
            Effect::None => return,
            Effect::Changed { velocity } => velocity,
            Effect::Panic => 0,
        };
        let at = self.timeline.at(offset);
        let after = self.sounding.notes();
        for note in sounding::notes_of(before & !after) {
            emit(NoteEvent {
                note,
                velocity: 0,
                at,
            });
        }
        for note in sounding::notes_of(after & !before) {
            emit(NoteEvent {
                note,
                velocity: velocity.max(1),
                at,
            });
        }
        if effect == Effect::Panic {
            self.reading = Reading::default();
            self.serial = self.serial.wrapping_add(1);
        } else {
            self.reread(after & !before != 0);
        }
    }

    /// A panic from the shell rather than from MIDI: everything stops, and a
    /// held reading goes too.
    pub fn reset(&mut self, emit: impl FnMut(NoteEvent)) {
        self.on_midi(&[0xB0, 123, 0], 0, emit);
    }

    /// Set a parameter to a choice; out-of-range choices are clamped.
    pub fn set_param(&mut self, param: Param, choice: i32) {
        let choice = param.clamp(choice);
        if self.values[param as usize] == choice {
            return;
        }
        self.values[param as usize] = choice;
        match param {
            Param::Tonic | Param::Mode | Param::Spelling => {
                /* The degree and the spelling of every text depend on these;
                 * the notes do not, so a held reading stays held. */
                let held = self.reading.is_held();
                let fresh = Reading::of(self.reading.notes(), self.key());
                self.reading = if held { fresh.held_over() } else { fresh };
                self.serial = self.serial.wrapping_add(1);
            }
            Param::Hold => {
                if choice == 0 && self.reading.is_held() {
                    self.reading = Reading::default();
                    self.serial = self.serial.wrapping_add(1);
                }
            }
            Param::HistoryView | Param::HistorySpan | Param::Zoom => {}
        }
    }

    /// A parameter's current choice.
    pub fn param(&self, param: Param) -> u8 {
        self.values[param as usize]
    }

    /// The key the parameters describe.
    pub fn key(&self) -> Key {
        let tonic = Pitch::new(i32::from(self.param(Param::Tonic)));
        let mode = Mode::from_index(self.param(Param::Mode)).unwrap_or_default();
        Key::new(tonic, mode)
    }

    /// How the texts spell: the key's way under Auto, otherwise as chosen.
    pub fn spelling(&self) -> Spelling {
        match self.param(Param::Spelling) {
            1 => Spelling::Sharps,
            2 => Spelling::Flats,
            _ => self.key().spelling(),
        }
    }

    /// What is shown now.
    pub fn reading(&self) -> &Reading {
        &self.reading
    }

    /// Bumped whenever the reading changes.
    pub fn serial(&self) -> u32 {
        self.serial
    }

    /// The notes sounding now. Under Hold this can be empty while the reading
    /// still shows the last chord.
    pub fn sounding(&self) -> NoteSet {
        self.sounding.notes()
    }

    /// Whether the sustain pedal is down.
    pub fn pedal(&self) -> bool {
        self.sounding.pedal()
    }

    /// The history's clock.
    pub fn timeline(&self) -> &Timeline {
        &self.timeline
    }

    /// Read the sounding notes again after a message changed them. `attacked`
    /// says a note started.
    ///
    /// UNDER HOLD ONLY AN ATTACK CHANGES THE READING. Keys never come up
    /// together, so a chord released key by key passes through every subset
    /// of itself on the way down; naming each would leave its last single note
    /// on the display, not the chord. So the chord played stays named while
    /// its keys come up, and is marked held once the last one has.
    fn reread(&mut self, attacked: bool) {
        let notes = self.sounding.notes();
        let hold = self.param(Param::Hold) == 1;
        let next = if hold && !attacked {
            if notes != 0 || self.reading.kind() == Kind::Empty {
                return;
            }
            self.reading.held_over()
        } else {
            Reading::of(notes, self.key())
        };
        if next != self.reading {
            self.reading = next;
            self.serial = self.serial.wrapping_add(1);
        }
    }
}

#[cfg(test)]
mod tests;

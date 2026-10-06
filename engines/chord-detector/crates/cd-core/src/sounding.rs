// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
Which notes are sounding: keys down, keys the pedal holds, and the panic.

A NOTE SOUNDS from its note-on until its note-off -- or, when the sustain pedal
(CC 64) was down at the note-off, until the pedal comes up. That is what the
listener hears, so it is what is named: a chord played and pedalled is still
that chord after the hand has moved on.

ALL CHANNELS ARE ONE. A MIDI lane in Live is one track, and a chord split
between two channels is still one chord.

A RE-STRUCK NOTE THAT IS STILL SOUNDING does not start again: it was sounding
before and it is sounding after, so nothing changed that a name or a history
could show. Its pedal latch is cleared, though -- the key is down now, and its
own note-off decides when it stops.

CC 120 (All Sound Off) AND CC 123 (All Notes Off) SILENCE EVERYTHING, the pedal
included. A host sends them as a panic, and a detector that kept naming a chord
nobody is playing would look exactly like a stuck note. They are checked before
anything else, as the Side-Chain's trigger does.

THE BYTES ARE `wmidi`'s. It reads a note-on at velocity 0 as the note-off it is
and refuses a message cut short, without allocating.
*/

use wmidi::{ControlFunction, MidiMessage};

/// The sustain pedal's controller.
const SUSTAIN: ControlFunction = ControlFunction::DAMPER_PEDAL;

/// A set of MIDI notes, 0 to 127, one bit each.
pub type NoteSet = u128;

/// What one message did to the sounding set.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Effect {
    /// Nothing that sounds changed.
    None,
    /// The sounding set changed; `velocity` is the note-on's, for a note that
    /// started (0 when nothing started).
    Changed { velocity: u8 },
    /// A panic: everything stopped.
    Panic,
}

/// Keys down, keys the pedal holds, and the pedal.
#[derive(Clone, Copy, Default, Debug)]
pub struct Sounding {
    down: NoteSet,
    latched: NoteSet,
    pedal: bool,
}

impl Sounding {
    /// The notes sounding now.
    #[inline]
    pub const fn notes(&self) -> NoteSet {
        self.down | self.latched
    }

    /// Whether the sustain pedal is down.
    #[inline]
    pub const fn pedal(&self) -> bool {
        self.pedal
    }

    /// Silence everything and lift the pedal.
    pub fn clear(&mut self) {
        *self = Sounding::default();
    }

    /// Read one message. Bytes that are not a whole message, and messages
    /// that do not change what sounds, are [`Effect::None`].
    pub fn apply(&mut self, msg: &[u8]) -> Effect {
        let Ok(message) = MidiMessage::try_from(msg) else {
            return Effect::None;
        };
        let before = self.notes();
        let mut velocity = 0;
        match message {
            MidiMessage::ControlChange(
                _,
                ControlFunction::ALL_SOUND_OFF | ControlFunction::ALL_NOTES_OFF,
                _,
            ) => {
                self.clear();
                return Effect::Panic;
            }
            MidiMessage::ControlChange(_, SUSTAIN, value) => {
                self.pedal = u8::from(value) >= 64;
                if !self.pedal {
                    self.latched = 0;
                }
            }
            MidiMessage::NoteOn(_, note, vel) => {
                let bit = 1u128 << u8::from(note);
                self.down |= bit;
                self.latched &= !bit;
                if before & bit == 0 {
                    velocity = u8::from(vel);
                }
            }
            MidiMessage::NoteOff(_, note, _) => {
                let bit = 1u128 << u8::from(note);
                if self.down & bit != 0 {
                    self.down &= !bit;
                    if self.pedal {
                        self.latched |= bit;
                    }
                }
            }
            _ => {}
        }
        if self.notes() == before {
            Effect::None
        } else {
            Effect::Changed { velocity }
        }
    }
}

/// The notes of a set, lowest first.
pub fn notes_of(set: NoteSet) -> impl Iterator<Item = u8> {
    let mut rest = set;
    core::iter::from_fn(move || {
        if rest == 0 {
            return None;
        }
        let note = rest.trailing_zeros() as u8;
        rest &= rest - 1;
        Some(note)
    })
}

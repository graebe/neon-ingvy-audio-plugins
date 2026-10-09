// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The MIDI trigger: what a message asks the envelope to do, and WHEN.

WHAT LIVE WILL AND WILL NOT ROUTE. Live hands MIDI to a plugin only on a MIDI
track, after an instrument; on an audio track nothing arrives at all. The engine
cannot tell "no routing" from "no notes", so it does not try -- the editor says
`no midi` when nothing has come, and the user's guide says why.

THE BYTES ARE PARSED BY `wmidi`, NOT BY HAND. A decode that reads byte 1 as the
note and byte 2 as the velocity accepts whatever sits there -- including a byte
with its top bit set, which is not data at all but the next message's status.
`[0x90, 36, 0x80]` is a note-on cut short, not a note-on at velocity 128, and
`[0xB0, 123, 0x90]` is not an All Notes Off. The crate refuses both, and a
refusal changes nothing here.

THE CHANNEL FILTER COVERS EVERYTHING, PANICS INCLUDED. CC 120 (All Sound Off)
and CC 123 (All Notes Off) are channel mode messages: each speaks for the
channel it arrives on, so a ducker listening on channel 2 keeps its held note
through channel 1's panic. Omni hears every channel, and so every channel's
panic. The NOTE filter, on the other hand, never applies to a panic -- one that
only worked when it arrived on the trigger note would not be a panic. It is
also the only reset a Schwung host can deliver, which is why the engine honours
it whatever the source (see `Instance::on_midi`).

A NOTE-ON AT VELOCITY 0 IS A NOTE-OFF. That is MIDI's own rule, so running
status can keep the same status byte for a whole phrase, and hosts and hardware
both send it. Read as a trigger at zero depth it would leave Gate mode holding a
note that was never pressed. `wmidi` already reports it as a note-off.

TRIGGER VS GATE. In Trigger mode a note fires the shape and the hold stage times
everything after it; a note-off means nothing. In Gate mode the duck stays down
while any trigger note is held, and the LAST note-off releases it -- so `held`
counts notes in both modes, and only Gate mode reads it.

THE QUEUE HONOURS SAMPLE OFFSETS. A note applied at the top of its block lands
up to a whole buffer early or late depending on where in the buffer the host
put it, and that is jitter, not latency: no delay compensation can take it out,
and it falls on the one event whose timing is the whole effect. So each action
waits in `Queue` until the sample loop reaches its offset.

AN OFFSET PAST THE BLOCK IS LATE, NOT LOST. The queue is emptied after every
block, so an event the sample walk never reaches would be an event that never
happened. `prepare` clamps it onto the block's last sample instead -- a host
reporting offsets against a different buffer size is a real thing.

THE OVERFLOW IS COUNTED. The queue is a fixed array, because the audio thread
does not allocate; a burst past `QUEUE_MAX` in one block loses the excess. A
dropped note is a missing duck that sounds like a bug in the envelope, so
`dropped` makes it visible -- the engine reports it as a diagnostic.
*/

#![deny(clippy::indexing_slicing, clippy::unwrap_used, clippy::expect_used)]

use wmidi::{ControlFunction, MidiMessage, Velocity};

/// What a message asks the envelope to do.
#[derive(Clone, Copy, PartialEq, Debug)]
pub enum Action {
    /// Fire the shape. The depth multiplier, 0..1, with velocity applied.
    Trigger(f64),
    /// Begin the recovery: Gate mode's last note-off.
    Release,
    /// Open now and forget the trigger: a panic.
    Reset,
}

/// An action and the sample, within the coming block, it belongs on.
#[derive(Clone, Copy, Debug)]
pub struct Event {
    pub at: usize,
    pub action: Action,
}

/// Events one block can hold. A drum pattern needs a handful; this is the
/// headroom for a dense one plus a flurry of panics, not a target.
pub const QUEUE_MAX: usize = 64;

/// One block's actions, in sample order. No allocation, ever.
///
/// The cycle per block is: `push` as messages arrive, `prepare` once at the
/// top of the block, `pop_at` for every sample in order, `clear` at the end.
pub struct Queue {
    events: [Event; QUEUE_MAX],
    /// Events held, `events[..len]`.
    len: usize,
    /// Events already handed out by `pop_at`, `events[..next]`.
    next: usize,
    /// Events lost to a full queue, over the queue's lifetime.
    dropped: u32,
}

/// What an unused slot holds. Never read: only `events[..len]` is live.
const VACANT: Event = Event {
    at: 0,
    action: Action::Reset,
};

impl Default for Queue {
    fn default() -> Self {
        Queue {
            events: [VACANT; QUEUE_MAX],
            len: 0,
            next: 0,
            dropped: 0,
        }
    }
}

impl Queue {
    /// Append an action at a sample offset. A full queue drops it and counts.
    pub fn push(&mut self, at: usize, action: Action) {
        match self.events.get_mut(self.len) {
            Some(slot) => {
                *slot = Event { at, action };
                self.len += 1;
            }
            None => self.dropped = self.dropped.saturating_add(1),
        }
    }

    /// Forget every queued event. The `dropped` count is kept: it is a
    /// lifetime diagnostic, and a reset is exactly when it should survive.
    pub fn clear(&mut self) {
        self.len = 0;
        self.next = 0;
    }

    /// Events lost to overflow since this queue was made.
    pub fn dropped(&self) -> u32 {
        self.dropped
    }

    /// The next action due on or before sample `i`, if any.
    ///
    /// Called for `i = 0, 1, 2, ...` and drained at each: several actions on
    /// one sample come out in the order they arrived.
    pub fn pop_at(&mut self, i: usize) -> Option<Action> {
        let ev = *self.live().get(self.next)?;
        if ev.at <= i {
            self.next += 1;
            Some(ev.action)
        } else {
            None
        }
    }

    /// Ready the queue for a block of `frames` samples. Called once, after the
    /// block's pushes and before its first `pop_at`.
    ///
    /// Drops what was already handed out, clamps anything past the block onto
    /// its last sample (late, not lost), and sorts by offset STABLY, so two
    /// actions on one sample -- a release and a retrigger, say -- keep the order
    /// they were sent in.
    pub fn prepare(&mut self, frames: usize) {
        let done = self.next.min(self.len);
        if let Some(held) = self.events.get_mut(..self.len) {
            held.rotate_left(done);
        }
        self.len -= done;
        self.next = 0;

        let Some(live) = self.events.get_mut(..self.len) else {
            return;
        };
        if let Some(last) = frames.checked_sub(1) {
            for ev in live.iter_mut() {
                ev.at = ev.at.min(last);
            }
        }
        sort_by_offset(live);
    }

    /// Nothing left to hand out.
    pub fn is_empty(&self) -> bool {
        self.next >= self.len
    }

    fn live(&self) -> &[Event] {
        self.events.get(..self.len).unwrap_or_default()
    }
}

/// Binary insertion sort by offset, stable.
///
/// Sixty-four elements at most, usually a handful already in order, and no
/// scratch buffer: the standard library's stable sort may want one, and the
/// audio thread does not allocate.
fn sort_by_offset(ev: &mut [Event]) {
    for i in 1..ev.len() {
        let Some(run) = ev.get_mut(..=i) else { return };
        let Some((last, sorted)) = run.split_last() else {
            return;
        };
        let at = last.at;
        /* After every event at or before `at`: equal offsets keep arrival order. */
        let p = sorted.partition_point(|e| e.at <= at);
        if let Some(tail) = run.get_mut(p..) {
            tail.rotate_right(1);
        }
    }
}

/// The MIDI trigger's settings and the one piece of state it keeps.
#[derive(Clone, Copy, Debug)]
pub struct Midi {
    /// 0 is Omni; 1..=16 hears that channel only.
    pub channel: i32,
    /// The trigger note, 0..=127.
    pub note: i32,
    /// `false` is Trigger mode, `true` is Gate mode.
    pub gate: bool,
    /// How much velocity scales the depth, 0..1. At 0 every note ducks fully.
    pub vel_sens: f64,
    /// Trigger notes currently held. Counted in both modes, read in Gate mode.
    pub held: i32,
}

impl Default for Midi {
    /// Channel 1, and note 36 -- C1 in Live's numbering, where a drum rack's
    /// first pad, the kick, sits.
    fn default() -> Self {
        Midi {
            channel: 1,
            note: 36,
            gate: false,
            vel_sens: 0.0,
            held: 0,
        }
    }
}

impl Midi {
    /// What one message asks for. `None` for anything that means nothing to a
    /// ducker, and for anything that is not a well-formed message at all.
    ///
    /// The message is read from the FRONT of `msg`; bytes past it are ignored.
    pub fn decode(&mut self, msg: &[u8]) -> Option<Action> {
        match MidiMessage::try_from(msg).ok()? {
            /* Any value: the controller number is the whole message. Reset All
             * Controllers (121) and every other controller fall through. */
            MidiMessage::ControlChange(
                ch,
                ControlFunction::ALL_SOUND_OFF | ControlFunction::ALL_NOTES_OFF,
                _,
            ) if self.hears(ch) => {
                self.held = 0;
                Some(Action::Reset)
            }
            MidiMessage::NoteOn(ch, note, vel) if self.hears(ch) && self.is_trigger(note) => {
                self.held = self.held.saturating_add(1);
                Some(Action::Trigger(self.scale(vel)))
            }
            /* Includes a note-on at velocity 0; `wmidi` reports it as this. */
            MidiMessage::NoteOff(ch, note, _) if self.hears(ch) && self.is_trigger(note) => {
                let was = self.held;
                self.held = was.saturating_sub(1).max(0);
                (self.gate && was > 0 && self.held == 0).then_some(Action::Release)
            }
            _ => None,
        }
    }

    fn hears(&self, ch: wmidi::Channel) -> bool {
        self.channel == 0 || self.channel == i32::from(ch.index()) + 1
    }

    fn is_trigger(&self, note: wmidi::Note) -> bool {
        i32::from(u8::from(note)) == self.note
    }

    /// The depth multiplier: 1 at no sensitivity, `velocity / 127` at full,
    /// and a straight line between.
    fn scale(&self, vel: Velocity) -> f64 {
        let v = f64::from(u8::from(vel)) / 127.0;
        (1.0 - self.vel_sens * (1.0 - v)).clamp(0.0, 1.0)
    }
}

/// A note name in Live's numbering -- `C-2` is 0, `C1` is 36, `G8` is 127 --
/// or `None` for anything that is not one.
///
/// Sharps only, because that is how the parameter's own labels are spelled.
/// Byte by byte and without allocating, because `set_param` reaches it on the
/// audio callback too.
pub fn note_from_name(s: &str) -> Option<i32> {
    let (&letter, rest) = s.as_bytes().split_first()?;
    let pitch = match letter {
        b'C' => 0,
        b'D' => 2,
        b'E' => 4,
        b'F' => 5,
        b'G' => 7,
        b'A' => 9,
        b'B' => 11,
        _ => return None,
    };
    let (pitch, rest) = match rest.split_first() {
        Some((b'#', r)) => (pitch + 1, r),
        _ => (pitch, rest),
    };
    let (negative, digits) = match rest.split_first() {
        Some((b'-', r)) => (true, r),
        _ => (false, rest),
    };
    if digits.is_empty() {
        return None;
    }
    let magnitude = digits.iter().try_fold(0i32, |acc, &d| {
        let d = char::from(d).to_digit(10)?;
        acc.checked_mul(10)?.checked_add(d as i32)
    })?;
    let octave = if negative { -magnitude } else { magnitude };
    if !(-2..=8).contains(&octave) {
        return None;
    }
    let n = (octave + 2) * 12 + pitch;
    (0..=127).contains(&n).then_some(n)
}

#[cfg(test)]
mod tests;

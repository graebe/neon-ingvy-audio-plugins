// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber
//
// Ported in part from https://github.com/charlesvestal/schwung-ducker
// (src/dsp/ducker.c), MIT License, Copyright (c) 2026 Charles Vestal.
// THIRD_PARTY_LICENSES.md carries that notice and the licence's text.

/*!
The MIDI trigger: what a note means, and when it means it.

Ported from `schwung-modules/graebe/schwung-ducker/src/dsp/ducker.c:306-345`
(MIT, (c) 2026 Charles Vestal) -- the channel filter, the note match, Trigger
vs Gate, and velocity scaling depth. See THIRD_PARTY_LICENSES.md.

WHAT IS ADDED HERE, AND WHY IT IS NOT A LUXURY: A SAMPLE OFFSET.

`ducker.c` applies a note the moment the host hands it over, which in a chain
host means the top of the block it arrived in. At a 256-frame buffer that is up
to 5 ms of jitter on the one event whose timing is the entire point of the
effect -- and it is jitter, not latency, so it cannot be compensated: the same
note lands on a different sample depending on where in the buffer it fell.

iPlug2 gives the offset in `IMidiMsg::mOffset` and it costs one queue to
honour it, so it is honoured. The Schwung `audio_fx` v2 `on_midi` has no offset
field, so that path passes 0 and gets `ducker.c`'s behaviour exactly -- which
is also what makes the Live/Move render A/B meaningful: drive both with offset
0 and they must agree bit for bit.

CC 120 (All Sound Off) AND CC 123 (All Notes Off) OPEN THE GATE.

The `audio_fx` v2 vtable has no reset hook, so `on_midi` is the only channel a
host panic can reach a module through. A ducker that keeps a note held after a
panic leaves the track quiet with nothing playing, which is the worst failure
this plugin has: silence that looks like a broken session rather than a stuck
effect.
*/

/// What a message asks the envelope to do.
#[derive(Clone, Copy, PartialEq, Debug)]
pub enum Action {
    /// Fire, with this depth multiplier (velocity already applied).
    Trigger(f64),
    /// Begin the recovery -- Gate mode's last note-off.
    Release,
    /// Open now and forget the trigger -- a panic.
    Reset,
}

/// A queued action and the sample within the block it belongs on.
#[derive(Clone, Copy, Debug)]
pub struct Event {
    pub at: usize,
    pub action: Action,
}

/// How many events one block can carry.
///
/// A block holding more than this is a host misbehaving or a stuck arpeggiator,
/// and the overflow is COUNTED rather than silently absorbed -- `dropped` is
/// readable through `get_param("dropped")` for exactly the reason
/// `spectro_dropped()` exists: a diagnostic you cannot read is a diagnostic you
/// will misattribute.
pub const QUEUE_MAX: usize = 64;

/*
 * SORTED ONCE PER BLOCK, WALKED WITH A CURSOR.
 *
 * The sample loop asks "what lands on sample i" for every i in order. Scanning
 * the whole queue for each answer was O(frames x events) -- 8192 x 64 scans at
 * the largest block, per block, on the audio thread, for what is almost always
 * zero or one event. `prepare` puts the events in offset order once, and the
 * walk then only ever looks at the next one.
 */
pub struct Queue {
    events: [Event; QUEUE_MAX],
    len: usize,
    /// The first event the walk has not yet handed out.
    next: usize,
    dropped: u32,
}

impl Default for Queue {
    fn default() -> Self {
        Queue {
            events: [Event {
                at: 0,
                action: Action::Reset,
            }; QUEUE_MAX],
            len: 0,
            next: 0,
            dropped: 0,
        }
    }
}

impl Queue {
    pub fn push(&mut self, at: usize, action: Action) {
        if self.len >= QUEUE_MAX {
            self.dropped = self.dropped.saturating_add(1);
            return;
        }
        self.events[self.len] = Event { at, action };
        self.len += 1;
    }

    pub fn clear(&mut self) {
        self.len = 0;
        self.next = 0;
    }

    pub fn dropped(&self) -> u32 {
        self.dropped
    }

    /// The next event landing on sample `i`, if any; call until `None`.
    ///
    /// The walk must visit samples in increasing order after `prepare`, which
    /// the sample loop does by construction. Events on the same sample come
    /// out in ARRIVAL order -- a note-on and its note-off at one offset must
    /// trigger and then release, not the other way round.
    #[inline]
    pub fn pop_at(&mut self, i: usize) -> Option<Action> {
        let e = self.events[..self.len].get(self.next)?;
        if e.at != i {
            return None;
        }
        self.next += 1;
        Some(e.action)
    }

    /// Get the queue ready for the block about to be rendered: clamp every
    /// offset into it, then put the events in offset order.
    ///
    /// AN OFFSET PAST THE END WOULD BE DROPPED, NOT DEFERRED. The queue is
    /// cleared per block, so an event the walk never reaches is an event that
    /// never happens -- and a host that reports an offset against a different
    /// buffer size is a real thing. Clamping makes it late by under a block
    /// instead of lost.
    ///
    /// AN INSERTION SORT, because it is STABLE -- arrival order must survive
    /// among equal offsets -- and allocation-free, which `slice::sort` is not
    /// promised to be. Hosts deliver events in offset order, so this is one
    /// comparison per event in practice; 64 events in reverse is the worst
    /// case, and still only a couple of thousand moves.
    pub fn prepare(&mut self, frames: usize) {
        let last = frames.saturating_sub(1);
        let ev = &mut self.events[..self.len];
        for e in ev.iter_mut() {
            if e.at > last {
                e.at = last;
            }
        }
        for i in 1..ev.len() {
            let mut j = i;
            while j > 0 && ev[j - 1].at > ev[j].at {
                ev.swap(j - 1, j);
                j -= 1;
            }
        }
        self.next = 0;
    }

    pub fn is_empty(&self) -> bool {
        self.len == 0
    }
}

/// The MIDI half of the parameter set, plus the held-note count.
#[derive(Clone, Copy, Debug)]
pub struct Midi {
    /// 0 = Omni, 1..16 = that channel only.
    pub channel: i32,
    /// The note that triggers, 0..127.
    pub note: i32,
    /// Gate mode: hold while a note is down. Trigger mode: Hold times out.
    pub gate: bool,
    /// How much velocity scales depth, 0..1. At 0 every note ducks fully.
    pub vel_sens: f64,
    /// Held matching notes. Gate mode releases when this reaches zero.
    pub held: i32,
}

impl Default for Midi {
    fn default() -> Self {
        Midi {
            channel: 1,
            note: 36, /* C1 -- Live's octave numbering, the kick pad */
            gate: false,
            vel_sens: 0.0,
            held: 0,
        }
    }
}

/// A note NAME in Live's numbering -- `C-2` is 0, `C1` is 36, `G8` is 127 --
/// or `None` if `s` is not one.
///
/// THE LABEL IS ONE OF TWO SPELLINGS THE MOVE CAN SEND. The declaration wires
/// `trigger_note` by index, but a hand-written patch and a host that has not
/// yet learned the convention both send the option's name, and `atof` reads
/// every name as 0 -- C-2, a note no kick pad sends. Sharps only, because the
/// declared options are spelled with sharps. Byte parsing, no allocation: this
/// runs on the audio callback.
pub fn note_from_name(s: &str) -> Option<i32> {
    let b = s.trim().as_bytes();
    let semitone = match b.first()? {
        b'C' => 0,
        b'D' => 2,
        b'E' => 4,
        b'F' => 5,
        b'G' => 7,
        b'A' => 9,
        b'B' => 11,
        _ => return None,
    };
    let (sharp, rest) = match b.get(1) {
        Some(b'#') => (1, &b[2..]),
        _ => (0, &b[1..]),
    };
    let (neg, digits) = match rest.first() {
        Some(b'-') => (true, &rest[1..]),
        _ => (false, rest),
    };
    if digits.is_empty() || digits.len() > 2 || !digits.iter().all(u8::is_ascii_digit) {
        return None;
    }
    let mut octave = digits.iter().fold(0i32, |a, d| a * 10 + (d - b'0') as i32);
    if neg {
        octave = -octave;
    }
    let note = (octave + 2) * 12 + semitone + sharp;
    (0..=127).contains(&note).then_some(note)
}

impl Midi {
    /// Decode one message into an action, or `None` if it is not ours.
    ///
    /// `msg` is raw MIDI bytes. A running-status or malformed message is
    /// rejected on length rather than indexed into.
    pub fn decode(&mut self, msg: &[u8]) -> Option<Action> {
        if msg.len() < 3 {
            return None;
        }
        let status = msg[0] & 0xF0;
        let chan = (msg[0] & 0x0F) as i32 + 1;
        if self.channel != 0 && chan != self.channel {
            return None;
        }

        /* Panic first, and regardless of the note filter: a panic that only
         * arrived on the trigger note would not be a panic. */
        if status == 0xB0 && (msg[1] == 120 || msg[1] == 123) {
            self.held = 0;
            return Some(Action::Reset);
        }

        let note = msg[1] as i32;
        if note != self.note {
            return None;
        }

        let vel = msg[2];
        /* A note-on at velocity 0 IS a note-off. Hosts and hardware both send
         * it, and reading it as a trigger at zero depth would leave Gate mode
         * holding a note that was never pressed. */
        let on = status == 0x90 && vel > 0;
        let off = status == 0x80 || (status == 0x90 && vel == 0);

        if on {
            self.held += 1;
            /* ducker.c:325-330: at sens 0 every note is full depth; at sens 1
             * depth follows velocity linearly. */
            let v = vel as f64 / 127.0;
            let scale = 1.0 - self.vel_sens + self.vel_sens * v;
            return Some(Action::Trigger(scale.clamp(0.0, 1.0)));
        }
        if off {
            if self.held > 0 {
                self.held -= 1;
            }
            if self.gate && self.held == 0 {
                return Some(Action::Release);
            }
        }
        None
    }
}

#[cfg(test)]
mod tests;

/*!
The MIDI trigger: what a note means, and when it means it.

Ported from `schwung-modules/graebe/schwung-ducker/src/dsp/ducker.c:306-345`
(MIT, (c) charlesvestal) -- the channel filter, the note match, Trigger vs
Gate, and velocity scaling depth. See THIRD_PARTY_LICENSES.md.

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

pub struct Queue {
    events: [Event; QUEUE_MAX],
    len: usize,
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
    }

    pub fn dropped(&self) -> u32 {
        self.dropped
    }

    /// The events landing on sample `i`, in arrival order.
    pub fn at(&self, i: usize) -> impl Iterator<Item = Action> + '_ {
        self.events[..self.len]
            .iter()
            .filter(move |e| e.at == i)
            .map(|e| e.action)
    }

    /// Clamp every offset into the block about to be rendered.
    ///
    /// AN OFFSET PAST THE END WOULD BE DROPPED, NOT DEFERRED. The queue is
    /// cleared per block, so an event the walk never reaches is an event that
    /// never happens -- and a host that reports an offset against a different
    /// buffer size is a real thing. Clamping makes it late by under a block
    /// instead of lost.
    pub fn clamp_into(&mut self, frames: usize) {
        let last = frames.saturating_sub(1);
        for e in self.events[..self.len].iter_mut() {
            if e.at > last {
                e.at = last;
            }
        }
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

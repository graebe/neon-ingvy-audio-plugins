/*!
The animated ground's bass-onset detector.

This crate exists because of one fact about the product: the design system's
`Ground` only moves when a kick lands, and the thing that has to see the kick
lives in a WebView with no access to the host's audio. `detect.rs` holds the
detector and the reasoning behind every constant in it; this file holds the one
piece of plumbing that detector needs in order to be useful across a thread
boundary.

WHY A COUNTER AND NOT A FLAG. The detector runs on the audio thread and the
editor reads it from the message thread, ~20-50 times a second, which is far
slower than kicks arrive in the worst case. A boolean "an onset happened" is
lossy in a way that shows: two kicks inside one idle tick would draw one ring,
and a tick that lands between the audio thread setting and clearing the flag
would draw none. A MONOTONIC COUNT cannot lose an event -- the reader compares
it against what it saw last and knows both THAT something happened and how many
times. NI Side-Chain's `sc_core_fires` is the same shape for the same reason,
and it is the one piece of prior art here worth copying exactly.

WHAT THE READER IS ALLOWED TO CONCLUDE, precisely, because the pair is two
atomics and not one: if the count moved, at least one onset happened, and
`strength` is the strength of the MOST RECENT one. It is not a queue. Under two
onsets inside one tick the reader sees `count += 2` and the later strength --
and that is the honest answer for this consumer, because the field it feeds
sums overlapping kicks anyway (`Field._step` adds every live wavelet), so a
ring lost inside 20 ms is a ring that would have been inside the one it merged
with. A consumer that needed each strength would need a ring buffer, and none
does.

RELAXED ORDERING IS SUFFICIENT AND IS NOT A SHORTCUT. Nothing here publishes a
pointer or guards a buffer; the two values are independent scalars whose only
consumer tolerates seeing a new count beside an old strength (it would draw one
ring at the previous kick's strength -- invisible). Paying for an acquire/release
pair on every audio block to protect against that is a cost with no symptom to
prevent. The spectrogram publishes its transport the same way and says so.
*/

mod biquad;
mod detect;

pub use detect::{Detector, Onset};

use core::sync::atomic::{AtomicU32, Ordering};

/// A detector plus the two published values the editor polls.
///
/// One per plugin instance. `push` is the audio thread's; `fires` and
/// `strength` are the message thread's.
pub struct Ground {
    detector: Detector,
    /// Monotonic onset count. Wraps at `u32::MAX`, which a reader comparing for
    /// inequality rather than ordering handles without noticing -- at a
    /// physically impossible ten onsets a second that is thirteen years.
    fires: AtomicU32,
    /// The most recent onset's strength, as `f32` bits.
    strength: AtomicU32,
}

impl Ground {
    pub fn new(sample_rate: f64) -> Self {
        Ground {
            detector: Detector::new(sample_rate),
            fires: AtomicU32::new(0),
            strength: AtomicU32::new(0f32.to_bits()),
        }
    }

    /// The audio thread's entry point: one block of stereo, as two slices.
    ///
    /// f64 AND NOT f32, which is the opposite of what the other engines here
    /// take, so it is worth saying why. They PROCESS: a ducker writes its result
    /// back, and the buffer it writes has to be the format the host handed over,
    /// so the shell converts into a pre-sized float scratch first. This only
    /// READS, and iPlug2's `sample` is a double -- so taking doubles means no
    /// scratch buffer, no chunking loop and no conversion in any of the four
    /// plugins, and the detector's own arithmetic was f64 all along.
    ///
    /// Allocates nothing, takes no lock, makes no system call. A mono host
    /// passes the same slice twice; a length mismatch takes the shorter, which
    /// is the only interpretation that cannot read past an end.
    pub fn push(&mut self, left: &[f64], right: &[f64]) {
        let n = left.len().min(right.len());
        for i in 0..n {
            if let Some(onset) = self.detector.next(left[i], right[i]) {
                /* The strength goes first. A reader that sees the new count is
                 * then guaranteed to see at least this strength rather than the
                 * previous one -- the reverse order would make the common case
                 * the stale one. */
                self.strength
                    .store(onset.strength.to_bits(), Ordering::Relaxed);
                self.fires.fetch_add(1, Ordering::Relaxed);
            }
        }
    }

    /// Reconfigure for a new sample rate and forget all state.
    pub fn set_sample_rate(&mut self, sample_rate: f64) {
        self.detector.set_sample_rate(sample_rate);
    }

    /// Forget the detector's state without disturbing the published count --
    /// a reader mid-comparison must not see the count go backwards.
    pub fn reset(&mut self) {
        self.detector.reset();
    }

    /// Monotonic onset count. Watch it CHANGE; its absolute value means nothing.
    pub fn fires(&self) -> u32 {
        self.fires.load(Ordering::Relaxed)
    }

    /// The most recent onset's strength, 0.3..1. Meaningless until `fires` has
    /// moved at least once, where it reads 0.
    pub fn strength(&self) -> f32 {
        f32::from_bits(self.strength.load(Ordering::Relaxed))
    }

    /// The detector's smoothed band level. For tests and diagnosis only.
    pub fn level(&self) -> f64 {
        self.detector.level()
    }
}

#[cfg(test)]
mod tests;

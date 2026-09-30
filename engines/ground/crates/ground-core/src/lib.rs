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

WHO OWNS WHAT, AND WHY `push` TAKES `&self`. There is one handle and three
threads touch it: the audio thread feeds it, the message thread reads the pair,
and whichever thread the host calls `OnReset` or opens the editor on asks for a
reset. Handing the audio thread a `&mut` while the message thread holds a `&`
to the same object is undefined behaviour however the fields are typed -- and
a reset that rewrote the detector while `push` was halfway through a block was
a real race, because hosts do not promise to call `OnReset` off the audio
thread. So:

  the detector      lives in an `UnsafeCell` and is touched by `push` ALONE.
                    `push` is `unsafe` for exactly one reason: its caller
                    promises it is never run twice at once (a single producer,
                    which is what an audio callback is).
  the pair          `fires` and `strength`, atomics, written by `push` and read
                    by anyone.
  requests          sample rate, reset and active are atomics written by anyone
                    and CONSUMED by `push` at the top of its next block. Nothing
                    but `push` ever writes the detector, so nothing needs a lock.

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

use core::cell::UnsafeCell;
use core::sync::atomic::{AtomicBool, AtomicU32, AtomicU64, Ordering};

/// A detector plus the two published values the editor polls.
///
/// One per plugin instance. `push` is the audio thread's; everything else may
/// be called from any thread. See the header for who owns what.
pub struct Ground {
    /// Touched by `push` and nothing else.
    detector: UnsafeCell<Detector>,
    /// Monotonic onset count. Wraps at `u32::MAX`, which a reader comparing for
    /// inequality rather than ordering handles without noticing -- at a
    /// physically impossible ten onsets a second that is thirteen years.
    fires: AtomicU32,
    /// The most recent onset's strength, as `f32` bits.
    strength: AtomicU32,
    /// The sample rate the next reset configures for, as `f64` bits.
    rate: AtomicU64,
    /// A reset has been asked for and `push` has not applied it yet. Written
    /// AFTER `rate`, with release, so the push that takes it sees that rate.
    reset_pending: AtomicBool,
    /// Whether `push` does anything at all. False until something asks: the
    /// detector only drives an editor, and running it with no editor open is
    /// audio-thread work that nobody sees.
    active: AtomicBool,
}

/* SAFETY: the only non-Sync field is the detector, and the only code that
 * touches it is `push`, whose contract forbids concurrent calls. Everything
 * else is an atomic. */
unsafe impl Sync for Ground {}

impl Ground {
    /// A detector for `sample_rate`, INACTIVE: `push` is a no-op until
    /// `set_active(true)`.
    pub fn new(sample_rate: f64) -> Self {
        Ground {
            detector: UnsafeCell::new(Detector::new(sample_rate)),
            fires: AtomicU32::new(0),
            strength: AtomicU32::new(0f32.to_bits()),
            rate: AtomicU64::new(sample_rate.to_bits()),
            reset_pending: AtomicBool::new(false),
            active: AtomicBool::new(false),
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
    /// A pending reset or sample-rate change is applied first, here, on the
    /// thread that owns the detector. An inactive detector returns at once.
    ///
    /// Allocates nothing, takes no lock, makes no system call. A mono host
    /// passes the same slice twice; a length mismatch takes the shorter, which
    /// is the only interpretation that cannot read past an end.
    ///
    /// # Safety
    /// Single producer: `push` must never run concurrently with another `push`
    /// on the same `Ground`. One audio callback per plugin instance is exactly
    /// that. Every other method may run concurrently with it.
    pub unsafe fn push(&self, left: &[f64], right: &[f64]) {
        /* Acquire, pairing with `set_active`'s release: a push that sees the
         * detector switched on also sees the reset that switching on asked for. */
        if !self.active.load(Ordering::Acquire) {
            return;
        }
        /* SAFETY: `push` is the only code that touches the detector, and the
         * caller has promised no two pushes overlap. */
        let detector = &mut *self.detector.get();
        if self.reset_pending.swap(false, Ordering::Acquire) {
            /* Recomputing the coefficients is a handful of exp/sin/cos: no
             * allocation and no system call, so it is fine here, and doing it
             * unconditionally keeps one path for "reset" and "new rate". */
            detector.set_sample_rate(f64::from_bits(self.rate.load(Ordering::Relaxed)));
        }
        let n = left.len().min(right.len());
        for i in 0..n {
            if let Some(onset) = detector.next(left[i], right[i]) {
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

    /// Ask for a new sample rate and a clean detector, from any thread. Applied
    /// by the next `push`.
    pub fn set_sample_rate(&self, sample_rate: f64) {
        self.rate.store(sample_rate.to_bits(), Ordering::Relaxed);
        self.reset_pending.store(true, Ordering::Release);
    }

    /// Ask for the detector's state to be forgotten, from any thread, without
    /// disturbing the published count -- a reader mid-comparison must not see
    /// the count go backwards. Applied by the next `push`.
    pub fn reset(&self) {
        self.reset_pending.store(true, Ordering::Release);
    }

    /// Start or stop feeding the detector, from any thread.
    ///
    /// Activating also asks for a reset, so a detector that resumes after a
    /// pause starts from silence rather than from the hump it was holding when
    /// it stopped -- a stale hump would fire a ring the moment the editor
    /// reopened.
    pub fn set_active(&self, active: bool) {
        if active {
            self.reset();
        }
        self.active.store(active, Ordering::Release);
    }

    /// Whether `push` currently does anything.
    pub fn is_active(&self) -> bool {
        self.active.load(Ordering::Relaxed)
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
}

#[cfg(test)]
mod tests;

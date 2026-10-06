/*!
The animated ground's beat clock, and the atomics that carry its rings to the
editor.

This crate exists because of one fact about the product: the design system's
`Ground` is a wave field that rings, and what tells it WHEN lives on the audio
thread -- the host's transport -- while the field lives in a WebView. `beat.rs`
holds the clock and the musical rule; this file holds the one piece of plumbing
that clock needs in order to be useful across a thread boundary.

WHY A COUNTER AND NOT A FLAG. The clock runs on the audio thread and the editor
reads it from the message thread, ~20-50 times a second. A boolean "a ring
happened" is lossy in a way that shows: two rings inside one idle tick would
draw one, and a tick that lands between the audio thread setting and clearing
the flag would draw none. A MONOTONIC COUNT cannot lose an event -- the reader
compares it against what it saw last and knows both THAT something happened and
how many times. NI Side-Chain's `sc_core_fires` is the same shape for the same
reason.

WHAT THE READER IS ALLOWED TO CONCLUDE, precisely, because the pair is two
atomics and not one: if the count moved, at least one ring happened, and
`strength` is the strength of the MOST RECENT one. It is not a queue. The field
sums overlapping rings anyway (`Field._step` adds every live wavelet), so a ring
merged into another inside one 20 ms tick is not a ring anybody could have told
apart.

WHO OWNS WHAT, AND WHY `tick` TAKES `&self`. There is one handle and three
threads touch it: the audio thread feeds it, the message thread reads the pair,
and whichever thread the host calls `OnReset` or opens the editor on asks for a
reset. Handing the audio thread a `&mut` while the message thread holds a `&`
to the same object is undefined behaviour however the fields are typed. So:

  the clock         lives in an `UnsafeCell` and is touched by `tick` ALONE.
                    `tick` is `unsafe` for exactly one reason: its caller
                    promises it is never run twice at once (a single producer,
                    which is what an audio callback is).
  the pair          `fires` and `strength`, atomics, written by `tick` and read
                    by anyone.
  requests          sample rate, reset and active are atomics written by anyone
                    and CONSUMED by `tick` at the top of its next block. Nothing
                    but `tick` ever writes the clock, so nothing needs a lock.

RELAXED ORDERING IS SUFFICIENT AND IS NOT A SHORTCUT. Nothing here publishes a
pointer or guards a buffer; the two values are independent scalars whose only
consumer tolerates seeing a new count beside an old strength (it would draw one
ring at the previous ring's strength). The spectrogram publishes its transport
the same way and says so.
*/

mod beat;

pub use beat::{
    bar_quarters, BeatClock, Rings, Transport, BEAT_STRENGTH, DEFAULT_BPM, DOWNBEAT_STRENGTH,
};

use core::cell::UnsafeCell;
use core::sync::atomic::{AtomicBool, AtomicU32, AtomicU64, Ordering};

/// A beat clock plus the two published values the editor polls.
///
/// One per plugin instance. `tick` is the audio thread's; everything else may
/// be called from any thread.
pub struct Ground {
    /// Touched by `tick` and nothing else.
    clock: UnsafeCell<BeatClock>,
    /// Monotonic ring count. Wraps at `u32::MAX`, which a reader comparing for
    /// inequality rather than ordering handles without noticing.
    fires: AtomicU32,
    /// The most recent ring's strength, as `f32` bits.
    strength: AtomicU32,
    /// The sample rate the next reset configures for, as `f64` bits.
    rate: AtomicU64,
    /// A reset has been asked for and `tick` has not applied it yet. Written
    /// AFTER `rate`, with release, so the tick that takes it sees that rate.
    reset_pending: AtomicBool,
    /// Whether `tick` does anything at all. False until something asks: the
    /// clock only drives an editor.
    active: AtomicBool,
}

/* SAFETY: the only non-Sync field is the clock, and the only code that touches
 * it is `tick`, whose contract forbids concurrent calls. Everything else is an
 * atomic. */
unsafe impl Sync for Ground {}

impl Ground {
    /// A ground for `sample_rate`, INACTIVE: `tick` is a no-op until
    /// `set_active(true)`.
    pub fn new(sample_rate: f64) -> Self {
        Ground {
            clock: UnsafeCell::new(BeatClock::new(sample_rate)),
            fires: AtomicU32::new(0),
            strength: AtomicU32::new(0f32.to_bits()),
            rate: AtomicU64::new(sample_rate.to_bits()),
            reset_pending: AtomicBool::new(false),
            active: AtomicBool::new(false),
        }
    }

    /// The audio thread's entry point: one block of `frames` samples at the
    /// host's clock `t`.
    ///
    /// A pending reset or sample-rate change is applied first, here, on the
    /// thread that owns the clock. An inactive ground returns at once.
    ///
    /// Allocates nothing, takes no lock, makes no system call.
    ///
    /// # Safety
    /// Single producer: `tick` must never run concurrently with another `tick`
    /// on the same `Ground`. One audio callback per plugin instance is exactly
    /// that. Every other method may run concurrently with it.
    pub unsafe fn tick(&self, t: &Transport, frames: usize) {
        /* Acquire, pairing with `set_active`'s release: a tick that sees the
         * ground switched on also sees the reset that switching on asked for. */
        if !self.active.load(Ordering::Acquire) {
            return;
        }
        /* SAFETY: `tick` is the only code that touches the clock, and the
         * caller has promised no two ticks overlap. */
        let clock = &mut *self.clock.get();
        if self.reset_pending.swap(false, Ordering::Acquire) {
            clock.set_sample_rate(f64::from_bits(self.rate.load(Ordering::Relaxed)));
        }
        let rings = clock.tick(t, frames);
        if rings.count > 0 {
            /* The strength goes first. A reader that sees the new count is
             * then guaranteed to see at least this strength rather than the
             * previous one -- the reverse order would make the common case
             * the stale one. */
            self.strength.store(rings.strength.to_bits(), Ordering::Relaxed);
            self.fires.fetch_add(rings.count, Ordering::Relaxed);
        }
    }

    /// Ask for a new sample rate and a fresh clock, from any thread. Applied
    /// by the next `tick`.
    pub fn set_sample_rate(&self, sample_rate: f64) {
        self.rate.store(sample_rate.to_bits(), Ordering::Relaxed);
        self.reset_pending.store(true, Ordering::Release);
    }

    /// Ask for the clock to forget the last block, from any thread, without
    /// disturbing the published count -- a reader mid-comparison must not see
    /// the count go backwards. Applied by the next `tick`, which then counts
    /// as a transport start.
    pub fn reset(&self) {
        self.reset_pending.store(true, Ordering::Release);
    }

    /// Start or stop the clock, from any thread.
    ///
    /// Activating also asks for a reset, so a reopened editor's first block is
    /// a fresh start: it rings what that block holds, never a backlog.
    pub fn set_active(&self, active: bool) {
        if active {
            self.reset();
        }
        self.active.store(active, Ordering::Release);
    }

    /// Whether `tick` currently does anything.
    pub fn is_active(&self) -> bool {
        self.active.load(Ordering::Relaxed)
    }

    /// Monotonic ring count. Watch it CHANGE; its absolute value means nothing.
    pub fn fires(&self) -> u32 {
        self.fires.load(Ordering::Relaxed)
    }

    /// The most recent ring's strength: DOWNBEAT_STRENGTH or BEAT_STRENGTH.
    /// Meaningless until `fires` has moved at least once, where it reads 0.
    pub fn strength(&self) -> f32 {
        f32::from_bits(self.strength.load(Ordering::Relaxed))
    }
}

#[cfg(test)]
mod tests;

/*
 * The own channel's way off the audio thread.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * WHY THE PLUGIN'S OWN AUDIO TAKES A RING RATHER THAN GOING STRAIGHT IN.
 *
 * A receiver draws several sources at once and the clash between them is read
 * per cell, so column k of every source has to be the SAME MOMENT. That only
 * holds if every analyzer is fed the same number of frames -- and the bus
 * sources are drained by the message thread, because `bus_core::Reader::read`
 * is documented "one thread, the same one each time" and opening a reader
 * allocates and mmaps, which is not an audio-thread act.
 *
 * So the own channel meets them there. ProcessBlock does nothing but copy its
 * mono sum in here; the message thread takes min(available) across every source
 * and pushes exactly that many frames into each. Everything then lines up by
 * construction rather than by hoping two threads keep step.
 *
 * It also takes the FFT off the audio thread, which is a plain win: an analyzer
 * that stutters draws a stuttering picture, where one that overruns the audio
 * thread makes a noise.
 *
 * THE DISCIPLINE IS THE HOUSE ONE, copied from spectro-core's `Columns` and
 * audio-bus's ring: one producer, one consumer, neither ever forms a reference
 * to the whole buffer, and the two counters publish the handover with
 * Release/Acquire. Full means DROP and say so -- overwriting the oldest would
 * need the writer to move the reader's counter, which is exactly the kind of
 * small shared write that makes a lock-free queue subtly wrong.
 */
use core::cell::UnsafeCell;
use core::sync::atomic::{AtomicU64, AtomicUsize, Ordering};

/// Frames the ring holds. A power of two so the wrap is a mask.
///
/// 32768 is 0.68 s at 48 kHz. The consumer runs off the editor's idle timer at
/// ~50 Hz, so this absorbs a host that stalls the message thread for two thirds
/// of a second before anything is lost -- and when something is, `dropped` says
/// how much rather than letting a gap pass as silence.
pub const CAPACITY: usize = 1 << 15;

pub struct MonoRing {
    buf: UnsafeCell<Box<[f32]>>,
    write: AtomicUsize,
    read: AtomicUsize,
    dropped: AtomicU64,
}

/* One producer and one consumer by construction; see the header. */
unsafe impl Sync for MonoRing {}
unsafe impl Send for MonoRing {}

impl MonoRing {
    pub fn new() -> Self {
        Self {
            buf: UnsafeCell::new(vec![0.0; CAPACITY].into_boxed_slice()),
            write: AtomicUsize::new(0),
            read: AtomicUsize::new(0),
            dropped: AtomicU64::new(0),
        }
    }

    /// Frames waiting to be taken.
    pub fn available(&self) -> usize {
        self.write
            .load(Ordering::Acquire)
            .wrapping_sub(self.read.load(Ordering::Relaxed))
    }

    pub fn dropped(&self) -> u64 {
        self.dropped.load(Ordering::Relaxed)
    }

    /// **Producer only.** Allocates nothing, takes no lock, makes no call.
    pub fn push(&self, src: &[f32]) {
        let w = self.write.load(Ordering::Relaxed);
        let r = self.read.load(Ordering::Acquire);
        let free = CAPACITY - w.wrapping_sub(r);

        /* Nothing partial: half a block is a splice, and a splice reads as
         * audio. The whole block is refused and counted. */
        if src.len() > free {
            self.dropped.fetch_add(src.len() as u64, Ordering::Relaxed);
            return;
        }

        let buf = unsafe { &mut *self.buf.get() };
        for (i, &s) in src.iter().enumerate() {
            /* A NaN here would poison every window it appears in, not just its
             * own column -- the same guard spectro-core's push carries. */
            buf[(w + i) & (CAPACITY - 1)] = if s.is_finite() { s } else { 0.0 };
        }
        self.write.store(w.wrapping_add(src.len()), Ordering::Release);
    }

    /// **Consumer only.** Fills as much of `out` as there is, returns how many.
    pub fn take(&self, out: &mut [f32]) -> usize {
        let w = self.write.load(Ordering::Acquire);
        let r = self.read.load(Ordering::Relaxed);
        let n = out.len().min(w.wrapping_sub(r));

        let buf = unsafe { &*self.buf.get() };
        for (i, slot) in out.iter_mut().take(n).enumerate() {
            *slot = buf[(r + i) & (CAPACITY - 1)];
        }
        if n > 0 {
            self.read.store(r.wrapping_add(n), Ordering::Release);
        }
        n
    }

    /// Forget everything waiting. For a source change, where the frames still
    /// in flight belong to the previous answer.
    pub fn clear(&self) {
        self.read
            .store(self.write.load(Ordering::Acquire), Ordering::Release);
    }
}

impl Default for MonoRing {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn what_goes_in_comes_out_in_order() {
        let r = MonoRing::new();
        r.push(&[1.0, 2.0, 3.0]);
        assert_eq!(r.available(), 3);

        let mut out = [0.0; 4];
        assert_eq!(r.take(&mut out), 3);
        assert_eq!(&out[..3], &[1.0, 2.0, 3.0]);
        assert_eq!(r.available(), 0);
        assert_eq!(r.take(&mut out), 0, "an empty ring is not an error");
    }

    #[test]
    fn it_wraps_without_losing_a_frame() {
        let r = MonoRing::new();
        let block = vec![0.0f32; 1000];
        let mut out = vec![0.0f32; 1000];
        /* Well past one lap of the buffer. */
        for lap in 0..100 {
            let mut b = block.clone();
            for (i, s) in b.iter_mut().enumerate() {
                *s = (lap * 1000 + i) as f32;
            }
            r.push(&b);
            assert_eq!(r.take(&mut out), 1000);
            for (i, &s) in out.iter().enumerate() {
                assert_eq!(s, (lap * 1000 + i) as f32, "lap {lap} frame {i}");
            }
        }
        assert_eq!(r.dropped(), 0);
    }

    #[test]
    fn a_full_ring_drops_whole_blocks_and_counts_them() {
        let r = MonoRing::new();
        let block = vec![0.5f32; 4096];
        let mut pushed = 0u64;
        for _ in 0..(CAPACITY / 4096) {
            r.push(&block);
            pushed += 4096;
        }
        assert_eq!(r.dropped(), 0, "it dropped before it was full");
        assert_eq!(r.available(), pushed as usize);

        r.push(&block);
        assert_eq!(r.dropped(), 4096, "a full ring took a block anyway");
        assert_eq!(r.available(), pushed as usize, "a refused block still moved the head");
    }

    #[test]
    fn a_nan_never_reaches_the_analyzer() {
        let r = MonoRing::new();
        r.push(&[f32::NAN, f32::INFINITY, 0.25]);
        let mut out = [0.0; 3];
        r.take(&mut out);
        assert_eq!(out, [0.0, 0.0, 0.25]);
    }

    #[test]
    fn clearing_drops_what_was_in_flight() {
        let r = MonoRing::new();
        r.push(&[1.0, 2.0, 3.0]);
        r.clear();
        assert_eq!(r.available(), 0);
        let mut out = [0.0; 3];
        assert_eq!(r.take(&mut out), 0);
    }
}

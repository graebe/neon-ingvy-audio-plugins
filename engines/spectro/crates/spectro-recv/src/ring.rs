// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The own channel's way off the audio thread.
 *
 * WHY THE PLUGIN'S OWN AUDIO TAKES A RING RATHER THAN GOING STRAIGHT IN.
 *
 * A receiver draws several sources at once and the clash between them is read
 * per cell, so column k of every source has to be the SAME MOMENT. That only
 * holds if every analyzer is fed the same number of frames -- and the bus
 * sources are drained by the pump, off the audio thread, because `bus_core::Reader::read`
 * is documented "one thread, the same one each time" and opening a reader
 * allocates and mmaps, which is not an audio-thread act.
 *
 * So the own channel meets them there. ProcessBlock does nothing but copy its
 * mono sum in here; the pump takes the same number of frames from every source
 * and pushes exactly that many into each. Everything then lines up by
 * construction rather than by hoping two threads keep step.
 *
 * It also takes the FFT off the audio thread, which is a plain win: an analyzer
 * that stutters draws a stuttering picture, where one that overruns the audio
 * thread makes a noise.
 *
 * THE DISCIPLINE IS THE HOUSE ONE, copied from spectro-core's `Columns` and
 * audio-bus's ring: one producer, one consumer, and the two counters publish the
 * handover with Release/Acquire. The buffer is held as a RAW POINTER taken once
 * when it is allocated, so no reference to the whole buffer exists while both
 * sides run -- each side touches only its own samples, through the pointer.
 * Full means DROP and say so -- overwriting the oldest would need the writer to
 * move the reader's counter, which is exactly the kind of small shared write
 * that makes a lock-free queue subtly wrong.
 *
 * `mono_ring` hands out the two ends as separate values. Neither is Clone and
 * each works through `&mut self`, so "one producer, one consumer" is enforced
 * by the compiler rather than promised.
 */
use core::sync::atomic::{AtomicU64, AtomicUsize, Ordering};
use std::sync::Arc;

/// Frames the ring holds. A power of two so the wrap is a mask.
///
/// 32768 is 0.68 s at 48 kHz, 0.34 s at 96. The consumer is the receiver's
/// worker, waking every few milliseconds, so this absorbs a stall of a third
/// of a second before anything is lost -- and when something is, `dropped` says
/// how much rather than letting a gap pass as silence.
pub const CAPACITY: usize = 1 << 15;

struct Ring {
    buf: *mut f32,
    write: AtomicUsize,
    read: AtomicUsize,
    dropped: AtomicU64,
}

/* The pointer is owned (see Drop); the producer writes only slots the consumer
 * has released and the consumer reads only slots the producer has published,
 * and there is exactly one of each -- `MonoProducer` and `MonoConsumer`. */
unsafe impl Send for Ring {}
unsafe impl Sync for Ring {}

impl Drop for Ring {
    fn drop(&mut self) {
        unsafe { drop(Box::from_raw(core::ptr::slice_from_raw_parts_mut(self.buf, CAPACITY))) }
    }
}

/// The audio thread's end.
pub struct MonoProducer {
    ring: Arc<Ring>,
}

/// The pump's end.
pub struct MonoConsumer {
    ring: Arc<Ring>,
}

/// A ring and its two ends.
pub fn mono_ring() -> (MonoProducer, MonoConsumer) {
    let ring = Arc::new(Ring {
        buf: Box::into_raw(vec![0.0f32; CAPACITY].into_boxed_slice()) as *mut f32,
        write: AtomicUsize::new(0),
        read: AtomicUsize::new(0),
        dropped: AtomicU64::new(0),
    });
    (MonoProducer { ring: ring.clone() }, MonoConsumer { ring })
}

impl MonoProducer {
    /// Allocates nothing, takes no lock, makes no call.
    pub fn push(&mut self, src: &[f32]) {
        let ring = &*self.ring;
        let w = ring.write.load(Ordering::Relaxed);
        let r = ring.read.load(Ordering::Acquire);
        let free = CAPACITY - w.wrapping_sub(r);

        /* Nothing partial: half a block is a splice, and a splice reads as
         * audio. The whole block is refused and counted. */
        if src.len() > free {
            ring.dropped.fetch_add(src.len() as u64, Ordering::Relaxed);
            return;
        }

        for (i, &s) in src.iter().enumerate() {
            /* A NaN here would poison every window it appears in, not just its
             * own column -- the same guard spectro-core's push carries. */
            let v = if s.is_finite() { s } else { 0.0 };
            unsafe { ring.buf.add((w + i) & (CAPACITY - 1)).write(v) };
        }
        ring.write.store(w.wrapping_add(src.len()), Ordering::Release);
    }

    pub fn dropped(&self) -> u64 {
        self.ring.dropped.load(Ordering::Relaxed)
    }
}

/// Reads the ring's drop count from a thread that is neither end.
pub struct RingStats {
    ring: Arc<Ring>,
}

impl RingStats {
    pub fn dropped(&self) -> u64 {
        self.ring.dropped.load(Ordering::Relaxed)
    }
}

impl MonoConsumer {
    pub fn stats(&self) -> RingStats {
        RingStats { ring: self.ring.clone() }
    }

    /// Frames waiting to be taken.
    pub fn available(&self) -> usize {
        self.ring
            .write
            .load(Ordering::Acquire)
            .wrapping_sub(self.ring.read.load(Ordering::Relaxed))
    }

    pub fn dropped(&self) -> u64 {
        self.ring.dropped.load(Ordering::Relaxed)
    }

    /// Fills as much of `out` as there is, returns how many.
    pub fn take(&mut self, out: &mut [f32]) -> usize {
        let ring = &*self.ring;
        let w = ring.write.load(Ordering::Acquire);
        let r = ring.read.load(Ordering::Relaxed);
        let n = out.len().min(w.wrapping_sub(r));

        for (i, slot) in out.iter_mut().take(n).enumerate() {
            *slot = unsafe { ring.buf.add((r + i) & (CAPACITY - 1)).read() };
        }
        if n > 0 {
            ring.read.store(r.wrapping_add(n), Ordering::Release);
        }
        n
    }

    /// Forget everything waiting. For a source change, where the frames still
    /// in flight belong to the previous answer.
    pub fn clear(&mut self) {
        let ring = &*self.ring;
        ring.read.store(ring.write.load(Ordering::Acquire), Ordering::Release);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn what_goes_in_comes_out_in_order() {
        let (mut p, mut c) = mono_ring();
        p.push(&[1.0, 2.0, 3.0]);
        assert_eq!(c.available(), 3);

        let mut out = [0.0; 4];
        assert_eq!(c.take(&mut out), 3);
        assert_eq!(&out[..3], &[1.0, 2.0, 3.0]);
        assert_eq!(c.available(), 0);
        assert_eq!(c.take(&mut out), 0, "an empty ring is not an error");
    }

    #[test]
    fn it_wraps_without_losing_a_frame() {
        let (mut p, mut c) = mono_ring();
        let block = vec![0.0f32; 1000];
        let mut out = vec![0.0f32; 1000];
        /* Well past one lap of the buffer. */
        for lap in 0..100 {
            let mut b = block.clone();
            for (i, s) in b.iter_mut().enumerate() {
                *s = (lap * 1000 + i) as f32;
            }
            p.push(&b);
            assert_eq!(c.take(&mut out), 1000);
            for (i, &s) in out.iter().enumerate() {
                assert_eq!(s, (lap * 1000 + i) as f32, "lap {lap} frame {i}");
            }
        }
        assert_eq!(c.dropped(), 0);
    }

    #[test]
    fn a_full_ring_drops_whole_blocks_and_counts_them() {
        let (mut p, c) = mono_ring();
        let block = vec![0.5f32; 4096];
        let mut pushed = 0u64;
        for _ in 0..(CAPACITY / 4096) {
            p.push(&block);
            pushed += 4096;
        }
        assert_eq!(c.dropped(), 0, "it dropped before it was full");
        assert_eq!(c.available(), pushed as usize);

        p.push(&block);
        assert_eq!(c.dropped(), 4096, "a full ring took a block anyway");
        assert_eq!(c.available(), pushed as usize, "a refused block still moved the head");
    }

    #[test]
    fn a_nan_never_reaches_the_analyzer() {
        let (mut p, mut c) = mono_ring();
        p.push(&[f32::NAN, f32::INFINITY, 0.25]);
        let mut out = [0.0; 3];
        c.take(&mut out);
        assert_eq!(out, [0.0, 0.0, 0.25]);
    }

    #[test]
    fn clearing_drops_what_was_in_flight() {
        let (mut p, mut c) = mono_ring();
        p.push(&[1.0, 2.0, 3.0]);
        c.clear();
        assert_eq!(c.available(), 0);
        let mut out = [0.0; 3];
        assert_eq!(c.take(&mut out), 0);
    }
}

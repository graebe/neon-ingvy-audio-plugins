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
 * THE RING IS rtrb's: one producer, one consumer, wait-free, and its buffer is
 * allocated once, when the ring is made. Full means DROP and say so -- rtrb
 * refuses a block it has no room for rather than overwriting the oldest, and
 * overwriting would need the writer to move the reader's position, which is
 * exactly the kind of small shared write that makes a lock-free queue subtly
 * wrong.
 *
 * `mono_ring` hands out the two ends as separate values. Neither is Clone,
 * each works through `&mut self`, and rtrb's ends are Send but not Sync, so
 * "one producer, one consumer" is enforced by the compiler rather than
 * promised.
 */
use core::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;

use rtrb::RingBuffer;

/// Frames the ring holds.
///
/// 32768 is 0.68 s at 48 kHz, 0.34 s at 96. The consumer is the receiver's
/// worker, waking every few milliseconds, so this absorbs a stall of a third
/// of a second before anything is lost -- and when something is, `dropped` says
/// how much rather than letting a gap pass as silence.
pub const CAPACITY: usize = 1 << 15;

/// The audio thread's end.
pub struct MonoProducer {
    ring: rtrb::Producer<f32>,
    dropped: Arc<AtomicU64>,
}

/// The pump's end.
pub struct MonoConsumer {
    ring: rtrb::Consumer<f32>,
    dropped: Arc<AtomicU64>,
}

/// A ring and its two ends.
pub fn mono_ring() -> (MonoProducer, MonoConsumer) {
    let (tx, rx) = RingBuffer::new(CAPACITY);
    /* Frames refused for want of room: the producer counts them, and the
     * consumer and any `RingStats` read the count. */
    let dropped = Arc::new(AtomicU64::new(0));
    (MonoProducer { ring: tx, dropped: dropped.clone() }, MonoConsumer { ring: rx, dropped })
}

impl MonoProducer {
    /// Allocates nothing, takes no lock, makes no call.
    pub fn push(&mut self, src: &[f32]) {
        /* Nothing partial: half a block is a splice, and a splice reads as
         * audio. The whole block is refused and counted. */
        let Ok(block) = self.ring.write_chunk_uninit(src.len()) else {
            self.dropped.fetch_add(src.len() as u64, Ordering::Relaxed);
            return;
        };
        /* A NaN here would poison every window it appears in, not just its
         * own column -- the same guard spectro-core's push carries. */
        block.fill_from_iter(src.iter().map(|&s| if s.is_finite() { s } else { 0.0 }));
    }

    pub fn dropped(&self) -> u64 {
        self.dropped.load(Ordering::Relaxed)
    }
}

/// Reads the ring's drop count from a thread that is neither end.
pub struct RingStats {
    dropped: Arc<AtomicU64>,
}

impl RingStats {
    pub fn dropped(&self) -> u64 {
        self.dropped.load(Ordering::Relaxed)
    }
}

impl MonoConsumer {
    pub fn stats(&self) -> RingStats {
        RingStats { dropped: self.dropped.clone() }
    }

    /// Frames waiting to be taken.
    pub fn available(&self) -> usize {
        self.ring.slots()
    }

    pub fn dropped(&self) -> u64 {
        self.dropped.load(Ordering::Relaxed)
    }

    /// Fills as much of `out` as there is, returns how many.
    pub fn take(&mut self, out: &mut [f32]) -> usize {
        self.ring.pop_partial_slice(out).0.len()
    }

    /// Forget everything waiting. For a source change, where the frames still
    /// in flight belong to the previous answer.
    pub fn clear(&mut self) {
        /* What is waiting now. A block pushed meanwhile already belongs to the
         * new answer, and stays. */
        if let Ok(waiting) = self.ring.read_chunk(self.ring.slots()) {
            waiting.commit_all();
        }
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

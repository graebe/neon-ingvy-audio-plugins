// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The ring, tested without a single shm_open.
 *
 * `ring.rs` takes a `&Header` and a `&[AtomicF32]` rather than a mapping precisely
 * so that this file can build one on the heap. Every part that can be subtly
 * wrong -- the wrap, the lap detection, the resync -- is exercised here with no
 * shared memory, no second process, and no race to provoke.
 *
 * What shm_open IS needed for is covered by `slots.rs` (one process) and
 * `tests/abus_ipc.c` (two).
 */

use core::sync::atomic::Ordering;

use atomic_float::AtomicF32;

use crate::header::{Header, Stamp, CHANNELS, DATA_OFFSET, RING_FRAMES, STAMPS, STAMP_OFFSET};
use crate::ring::{self, Cursor, RING_SAMPLES, USABLE_FRAMES};

/// A segment on the heap: the same bytes shm would have handed us.
struct Segment {
    buf: Vec<u8>,
}

impl Segment {
    fn new() -> Segment {
        let seg = Segment {
            buf: vec![0u8; crate::header::segment_size()],
        };
        /* Vec<u8> is only 1-aligned in the type system; the allocator gives us
         * far more in practice, but assert it rather than hope. */
        assert_eq!(seg.buf.as_ptr() as usize % 8, 0, "test segment misaligned");
        seg.hdr().initialise(48_000, 1234);
        seg
    }

    /* Both take &self. The segment is shared memory in production -- a writer
     * and any number of readers hold it at once and none of them has exclusive
     * access -- so a helper that demanded &mut would be modelling something
     * the real thing never has. */
    fn hdr(&self) -> &Header {
        unsafe { &*(self.buf.as_ptr() as *const Header) }
    }
    fn data(&self) -> &[AtomicF32] {
        unsafe {
            core::slice::from_raw_parts(
                self.buf.as_ptr().add(DATA_OFFSET) as *const AtomicF32,
                RING_SAMPLES,
            )
        }
    }
    fn stamps(&self) -> &[Stamp] {
        unsafe {
            core::slice::from_raw_parts(
                self.buf.as_ptr().add(STAMP_OFFSET) as *const Stamp,
                STAMPS as usize,
            )
        }
    }
}

/// `n` frames whose every sample IS its own absolute frame number, as the
/// float's bit pattern -- left the number, right its complement. A reader can
/// then check not just the values but their CONTINUITY, which is what a
/// spliced buffer breaks and a merely-wrong one usually does not.
///
/// Bits, not `frame as f32`: a float counts exactly only to 2^24, which the
/// splice test's writer passes in a few seconds, after which neighbouring
/// frames round to the same value and a splice can hide in the rounding. The
/// ring moves bits and never does arithmetic on them, so every pattern --
/// NaNs included -- comes back as it went in; compare with `frame_of`, never
/// with `==` on the float.
fn ramp(from: u64, n: usize) -> Vec<f32> {
    let mut v = Vec::with_capacity(n * CHANNELS as usize);
    for i in 0..n {
        let f = (from + i as u64) as u32;
        v.push(f32::from_bits(f));
        v.push(f32::from_bits(!f));
    }
    v
}

/// The frame number frame `i` of `out` carries, checking the right channel
/// agrees with the left.
fn frame_of(out: &[f32], i: usize) -> u64 {
    let l = out[i * 2].to_bits();
    assert_eq!(out[i * 2 + 1].to_bits(), !l, "channels disagree at frame {i}");
    l as u64
}

#[test]
fn a_block_comes_back_exactly() {
    let seg = Segment::new();
    let mut cur = Cursor::at_live_edge(seg.hdr());
    let src = ramp(0, 512);

    ring::push(seg.hdr(), seg.data(), seg.stamps(), &src, None);

    let mut out = vec![0f32; 512 * CHANNELS as usize];
    let got = ring::read(seg.hdr(), seg.data(), &mut cur, &mut out);

    assert_eq!(got.frames, 512);
    assert_eq!(got.dropped, 0);
    assert!(!got.resynced);
    let same = out.iter().zip(&src).all(|(a, b)| a.to_bits() == b.to_bits());
    assert!(same, "the samples that came out are not the ones that went in");
}

#[test]
fn reading_an_idle_bus_yields_nothing_rather_than_silence() {
    let seg = Segment::new();
    let mut cur = Cursor::at_live_edge(seg.hdr());
    let mut out = vec![0f32; 64 * CHANNELS as usize];
    let got = ring::read(seg.hdr(), seg.data(), &mut cur, &mut out);
    assert_eq!(got.frames, 0);
    assert_eq!(got.dropped, 0);
}

#[test]
fn the_wrap_is_seamless() {
    let seg = Segment::new();
    let mut cur = Cursor::at_live_edge(seg.hdr());

    /* Three times round the ring in 1000-frame blocks, reading after each so
     * the reader never falls behind. If the wrap is off by one frame or by one
     * channel, the continuity check below finds it. */
    let block = 1000usize;
    let blocks = (RING_FRAMES as usize / block) * 3;
    let mut expect = 0u64;
    let mut written = 0u64;

    for _ in 0..blocks {
        ring::push(seg.hdr(), seg.data(), seg.stamps(), &ramp(written, block), None);
        written += block as u64;

        let mut out = vec![0f32; block * CHANNELS as usize];
        let got = ring::read(seg.hdr(), seg.data(), &mut cur, &mut out);
        assert_eq!(got.dropped, 0);
        for i in 0..got.frames as usize {
            assert_eq!(frame_of(&out, i), expect, "discontinuity at frame {expect}");
            expect += 1;
        }
    }
    assert_eq!(expect, written, "not every frame came back");
}

#[test]
fn falling_behind_is_reported_not_silently_papered_over() {
    let seg = Segment::new();
    let mut cur = Cursor::at_live_edge(seg.hdr());

    /* Two full rings with nobody reading: the first is definitively gone. */
    let block = 4096usize;
    let n = (RING_FRAMES as usize / block) * 2;
    for b in 0..n {
        ring::push(seg.hdr(), seg.data(), seg.stamps(), &ramp((b * block) as u64, block), None);
    }

    let mut out = vec![0f32; block * CHANNELS as usize];
    let got = ring::read(seg.hdr(), seg.data(), &mut cur, &mut out);

    /* USABLE_FRAMES, not RING_FRAMES: the reader deliberately stays clear of
     * the run the writer is in the middle of publishing -- see
     * MAX_BLOCK_FRAMES in ring.rs. */
    let total = (n * block) as u64;
    assert_eq!(
        got.dropped,
        total - USABLE_FRAMES as u64,
        "a reader that fell off the end must be told how much went past"
    );
    assert!(got.frames > 0, "and must still get the newest audio");
    /* What it gets is the OLDEST frame still safe to read, not the newest: the
     * reader is catching up, not jumping to the live edge. */
    assert_eq!(frame_of(&out, 0), total - USABLE_FRAMES as u64);
}

#[test]
fn a_block_longer_than_the_ring_keeps_its_tail() {
    let seg = Segment::new();
    let mut cur = Cursor::at_live_edge(seg.hdr());

    let huge = RING_FRAMES as usize + 777;
    ring::push(seg.hdr(), seg.data(), seg.stamps(), &ramp(0, huge), None);

    /* The whole block is published -- in runs of MAX_BLOCK_FRAMES -- but only
     * its last RING_FRAMES can still be in the ring, and only USABLE_FRAMES of
     * those are safe to read. */
    assert_eq!(
        seg.hdr().write_frames.load(Ordering::Acquire),
        huge as u64,
        "every frame handed in is accounted for, even the ones overwritten"
    );

    let mut out = vec![0f32; 16 * CHANNELS as usize];
    let got = ring::read(seg.hdr(), seg.data(), &mut cur, &mut out);
    assert_eq!(got.frames, 16);
    assert_eq!(
        frame_of(&out, 0),
        huge as u64 - USABLE_FRAMES as u64,
        "the tail is what survives, not the head"
    );
}

#[test]
fn a_new_epoch_throws_the_cursor_away() {
    let seg = Segment::new();
    let mut cur = Cursor::at_live_edge(seg.hdr());
    ring::push(seg.hdr(), seg.data(), seg.stamps(), &ramp(0, 256), None);

    /* A sender claims the slot: new epoch, counter reset. */
    seg.hdr().write_frames.store(0, Ordering::Release);
    seg.hdr().epoch.fetch_add(1, Ordering::AcqRel);

    let mut out = vec![0f32; 256 * CHANNELS as usize];
    let got = ring::read(seg.hdr(), seg.data(), &mut cur, &mut out);
    assert!(got.resynced, "a reader must notice the stream restarted");
    assert_eq!(got.frames, 0, "and must not splice across the seam");

    /* And carries on normally afterwards. */
    ring::push(seg.hdr(), seg.data(), seg.stamps(), &ramp(9000, 128), None);
    let got = ring::read(seg.hdr(), seg.data(), &mut cur, &mut out);
    assert!(!got.resynced);
    assert_eq!(got.frames, 128);
    assert_eq!(frame_of(&out, 0), 9000);
}

#[test]
fn a_restart_during_the_copy_is_not_handed_out() {
    /*
     * THE SPLICE THE LAP CHECK CANNOT SEE. The reader has checked the epoch
     * and started copying old-stream frames; meanwhile the writer restarts
     * (a claim, a rate change): count back to zero, epoch bumped, and new
     * audio written from the START of the ring -- over the frames being
     * copied. The count after the copy is now small, so "how far did the
     * writer get" says nowhere near us, and the old re-check passed a buffer
     * that was half one stream and half the other.
     */
    let seg = Segment::new();
    let mut cur = Cursor::at_live_edge(seg.hdr());
    ring::push(seg.hdr(), seg.data(), seg.stamps(), &ramp(0, 512), None);

    let mut out = vec![0f32; 512 * CHANNELS as usize];
    let got = ring::read_with(seg.hdr(), seg.data(), &mut cur, &mut out, || {
        seg.hdr().write_frames.store(0, Ordering::Relaxed);
        seg.hdr().epoch.fetch_add(1, Ordering::Release);
        ring::push(seg.hdr(), seg.data(), seg.stamps(), &ramp(50_000, 256), None);
    });
    assert_eq!(got.frames, 0, "a block that straddled a restart was handed out");
    assert!(got.resynced, "and the restart was not reported");

    /* It picks up the new stream at its live edge. */
    ring::push(seg.hdr(), seg.data(), seg.stamps(), &ramp(60_000, 64), None);
    let got = ring::read(seg.hdr(), seg.data(), &mut cur, &mut out);
    assert_eq!(got.frames, 64);
    assert_eq!(frame_of(&out, 0), 60_000);
}

#[test]
fn a_label_survives_the_round_trip_and_is_clamped() {
    let seg = Segment::new();
    let hdr = seg.hdr();

    hdr.set_label("Bass");
    let got = hdr.label().expect("a settled label reads back");
    assert_eq!(&got[..4], b"Bass");
    assert_eq!(got[4], 0);

    /* 31 bytes plus a NUL, and the caller's 60 are not allowed to run off the
     * end of a 32-byte field. */
    hdr.set_label(&"x".repeat(60));
    let got = hdr.label().expect("reads back");
    assert_eq!(got[crate::header::LABEL_BYTES - 1], 0, "always NUL-terminated");
    assert_eq!(got.iter().filter(|&&b| b == b'x').count(), 31);

    /* A shorter label afterwards leaves nothing of the longer one behind. */
    hdr.set_label("Pad");
    let got = hdr.label().expect("reads back");
    assert_eq!(&got[..4], b"Pad\0");
    assert!(got[4..].iter().all(|&b| b == 0));
}

#[test]
fn a_clamped_label_is_still_utf8() {
    /* 30 ASCII bytes and then a two-byte 'ä' straddling byte 31: a byte clamp
     * keeps half of it, and every reader gets a name that is not UTF-8. */
    let seg = Segment::new();
    let text = format!("{}äß", "x".repeat(30));
    seg.hdr().set_label(&text);
    let got = seg.hdr().label().expect("reads back");
    let end = got.iter().position(|&b| b == 0).unwrap();
    let name = core::str::from_utf8(&got[..end]).expect("the label was cut inside a character");
    assert_eq!(name, "x".repeat(30));
}

/*
 * THE ONE THAT NEEDS THREADS.
 *
 * Everything above is deterministic, and none of it can catch the failure this
 * design is most likely to have: the writer lapping the reader DURING the copy,
 * handing back a buffer that is half old audio and half new, spliced at an
 * arbitrary point. It looks like audio. It passes every value check. The only
 * thing wrong with it is that those samples were never adjacent.
 *
 * So: a writer running flat out, a reader deliberately slow, and every
 * delivered block checked for internal continuity. A splice fails this.
 *
 * ENOUGH BLOCKS, NOT ENOUGH POLLS. The reader used to make 4000 polls and stop,
 * which natively checks a few hundred blocks. Under emulation, beside the rest
 * of the suite, the writer thread could still be waiting to be scheduled when
 * the last poll came and went: nothing was checked, and the test failed for a
 * reason that had nothing to do with the ring. So the reader reads until it has
 * checked BLOCKS blocks -- more than a native run of the old loop ever did --
 * and the deadline is there only so that a ring that stops delivering fails
 * instead of hanging the suite.
 */
#[test]
fn a_reader_is_never_handed_a_splice() {
    use core::sync::atomic::AtomicBool;
    use std::time::{Duration, Instant};

    const BLOCKS: u32 = 400;

    /* Stops the writer however the reader's half ends. An assert that fails
     * mid-loop would otherwise leave the scope below waiting forever on a
     * writer nobody told to stop. */
    struct Stop<'a>(&'a AtomicBool);
    impl Drop for Stop<'_> {
        fn drop(&mut self) {
            self.0.store(true, Ordering::Relaxed);
        }
    }

    let seg = Segment::new();
    let stop = AtomicBool::new(false);

    /* Scoped, so the writer borrows the segment and is joined before the
     * segment is freed, whichever way the reader leaves. */
    std::thread::scope(|scope| {
        let writer = scope.spawn(|| {
            let mut written = 0u64;
            while !stop.load(Ordering::Relaxed) {
                let block = 512usize;
                ring::push(seg.hdr(), seg.data(), seg.stamps(), &ramp(written, block), None);
                written += block as u64;
            }
        });
        let stopping = Stop(&stop);

        let mut cur = Cursor::at_live_edge(seg.hdr());
        let mut out = vec![0f32; 4096 * CHANNELS as usize];
        let mut blocks_seen = 0u32;
        let deadline = Instant::now() + Duration::from_secs(60);

        while blocks_seen < BLOCKS {
            assert!(
                Instant::now() < deadline,
                "only {blocks_seen} blocks were delivered in a minute"
            );
            let got = ring::read(seg.hdr(), seg.data(), &mut cur, &mut out);
            if got.frames > 1 {
                blocks_seen += 1;
                let first = frame_of(&out, 0);
                for i in 0..got.frames as usize {
                    assert_eq!(
                        frame_of(&out, i),
                        first + i as u64,
                        "SPLICE: frame {i} of a {}-frame block is not contiguous",
                        got.frames
                    );
                }
            }
            std::thread::yield_now();
        }

        drop(stopping);
        writer.join().expect("the writer panicked");
    });
}

/* ---- the timeline stamps (ABI 3) ---- */

#[test]
fn every_frame_read_knows_its_timeline_sample() {
    let seg = Segment::new();
    let mut cur = Cursor::at_live_edge(seg.hdr());
    ring::push(seg.hdr(), seg.data(), seg.stamps(), &ramp(0, 128), Some(48_000));
    ring::push(seg.hdr(), seg.data(), seg.stamps(), &ramp(128, 64), Some(48_128));

    let mut out = vec![0f32; 256 * CHANNELS as usize];
    let got = ring::read(seg.hdr(), seg.data(), &mut cur, &mut out);
    assert_eq!((got.frames, got.first), (192, 0));
    for frame in [0u64, 1, 127, 128, 191] {
        let span = ring::stamp_at(seg.hdr(), seg.stamps(), cur.epoch(), frame).expect("stamped");
        assert_eq!(span.timeline_of(frame), Some(48_000 + frame as i64), "frame {frame}");
    }
    /* Not written yet: no run holds it. */
    assert_eq!(ring::stamp_at(seg.hdr(), seg.stamps(), cur.epoch(), 192), None);
}

#[test]
fn a_loop_is_two_runs_not_one_line() {
    /* The host jumped back between the blocks: the stamps say so, and a frame
     * after the jump is not extrapolated from the run before it. */
    let seg = Segment::new();
    let cur = Cursor::at_live_edge(seg.hdr());
    ring::push(seg.hdr(), seg.data(), seg.stamps(), &ramp(0, 100), Some(96_000));
    ring::push(seg.hdr(), seg.data(), seg.stamps(), &ramp(100, 100), Some(0));
    let at = |f| ring::stamp_at(seg.hdr(), seg.stamps(), cur.epoch(), f).and_then(|s| s.timeline_of(f));
    assert_eq!(at(99), Some(96_099));
    assert_eq!(at(100), Some(0));
    assert_eq!(at(150), Some(50));
}

#[test]
fn a_stopped_transport_is_unstamped_not_zero() {
    let seg = Segment::new();
    let cur = Cursor::at_live_edge(seg.hdr());
    ring::push(seg.hdr(), seg.data(), seg.stamps(), &ramp(0, 64), None);
    let span = ring::stamp_at(seg.hdr(), seg.stamps(), cur.epoch(), 10).expect("the run is known");
    assert_eq!((span.start, span.frames, span.timeline), (0, 64, None));
    assert_eq!(span.timeline_of(10), None);
}

#[test]
fn a_long_block_is_stamped_run_by_run() {
    /* Published in runs of MAX_BLOCK_FRAMES, each with its own stamp, all on
     * one line. */
    let seg = Segment::new();
    let cur = Cursor::at_live_edge(seg.hdr());
    let n = ring::MAX_BLOCK_FRAMES as usize * 2 + 10;
    ring::push(seg.hdr(), seg.data(), seg.stamps(), &ramp(0, n), Some(1_000));
    assert_eq!(seg.hdr().stamp_runs.load(Ordering::Relaxed), 3);
    for f in [0u64, 8191, 8192, (n - 1) as u64] {
        let at = ring::stamp_at(seg.hdr(), seg.stamps(), cur.epoch(), f).and_then(|s| s.timeline_of(f));
        assert_eq!(at, Some(1_000 + f as i64), "frame {f}");
    }
}

#[test]
fn a_stamp_from_before_a_restart_is_not_this_streams() {
    /* After a restart the new stream reaches the old one's positions again;
     * the epoch in the stamp keeps them apart. */
    let seg = Segment::new();
    ring::push(seg.hdr(), seg.data(), seg.stamps(), &ramp(0, 256), Some(5_000));
    let old = Cursor::at_live_edge(seg.hdr()).epoch();
    seg.hdr().write_frames.store(0, Ordering::Relaxed);
    seg.hdr().epoch.fetch_add(1, Ordering::Release);
    let new = Cursor::at_live_edge(seg.hdr()).epoch();
    assert_ne!(old, new);
    assert_eq!(ring::stamp_at(seg.hdr(), seg.stamps(), new, 10), None);

    ring::push(seg.hdr(), seg.data(), seg.stamps(), &ramp(0, 32), Some(7));
    let at = ring::stamp_at(seg.hdr(), seg.stamps(), new, 10).and_then(|s| s.timeline_of(10));
    assert_eq!(at, Some(17));
}

#[test]
fn a_stamp_reused_by_later_runs_is_gone() {
    let seg = Segment::new();
    let cur = Cursor::at_live_edge(seg.hdr());
    for b in 0..(STAMPS as u64 + 5) {
        ring::push(seg.hdr(), seg.data(), seg.stamps(), &ramp(b * 4, 4), Some((b * 4) as i64));
    }
    /* The first five runs' stamps were taken by the last five. */
    assert_eq!(ring::stamp_at(seg.hdr(), seg.stamps(), cur.epoch(), 0), None);
    assert_eq!(ring::stamp_at(seg.hdr(), seg.stamps(), cur.epoch(), 19), None);
    let f = 20u64;
    let at = ring::stamp_at(seg.hdr(), seg.stamps(), cur.epoch(), f).and_then(|s| s.timeline_of(f));
    assert_eq!(at, Some(20));
}

#[test]
fn a_stamp_being_rewritten_is_skipped() {
    /* A writer parked mid-rewrite: the reader takes nothing from it. */
    let seg = Segment::new();
    let cur = Cursor::at_live_edge(seg.hdr());
    ring::push(seg.hdr(), seg.data(), seg.stamps(), &ramp(0, 16), Some(0));
    seg.stamps()[0].start.store(crate::header::STAMP_PARKED, Ordering::Relaxed);
    assert_eq!(ring::stamp_at(seg.hdr(), seg.stamps(), cur.epoch(), 3), None);
}

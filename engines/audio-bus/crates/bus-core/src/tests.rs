/*
 * The ring, tested without a single shm_open.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * `ring.rs` takes a `&Header` and a `*mut f32` rather than a mapping precisely
 * so that this file can build one on the heap. Every part that can be subtly
 * wrong -- the wrap, the lap detection, the resync -- is exercised here with no
 * shared memory, no second process, and no race to provoke.
 *
 * What shm_open IS needed for is covered by `slots.rs` (one process) and
 * `tests/abus_ipc.c` (two).
 */

use crate::header::{Header, CHANNELS, DATA_OFFSET, RING_FRAMES};
use crate::ring::{self, Cursor, USABLE_FRAMES};

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
    fn data(&self) -> *mut f32 {
        unsafe { (self.buf.as_ptr() as *mut u8).add(DATA_OFFSET) as *mut f32 }
    }
}

/// `n` frames whose every sample is its own absolute frame number. A reader
/// can then check not just the values but their CONTINUITY -- which is what a
/// spliced buffer breaks and a merely-wrong one usually does not.
fn ramp(from: u64, n: usize) -> Vec<f32> {
    let mut v = Vec::with_capacity(n * CHANNELS as usize);
    for i in 0..n {
        let f = (from + i as u64) as f32;
        v.push(f);
        v.push(-f);
    }
    v
}

#[test]
fn a_block_comes_back_exactly() {
    let seg = Segment::new();
    let mut cur = Cursor::at_live_edge(seg.hdr());
    let src = ramp(0, 512);

    unsafe { ring::push(seg.hdr(), seg.data(), &src) };

    let mut out = vec![0f32; 512 * CHANNELS as usize];
    let got = unsafe { ring::read(seg.hdr(), seg.data(), &mut cur, &mut out) };

    assert_eq!(got.frames, 512);
    assert_eq!(got.dropped, 0);
    assert!(!got.resynced);
    assert_eq!(out, src, "the samples that came out are not the ones that went in");
}

#[test]
fn reading_an_idle_bus_yields_nothing_rather_than_silence() {
    let seg = Segment::new();
    let mut cur = Cursor::at_live_edge(seg.hdr());
    let mut out = vec![0f32; 64 * CHANNELS as usize];
    let got = unsafe { ring::read(seg.hdr(), seg.data(), &mut cur, &mut out) };
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
        unsafe { ring::push(seg.hdr(), seg.data(), &ramp(written, block)) };
        written += block as u64;

        let mut out = vec![0f32; block * CHANNELS as usize];
        let got = unsafe { ring::read(seg.hdr(), seg.data(), &mut cur, &mut out) };
        assert_eq!(got.dropped, 0);
        for i in 0..got.frames as usize {
            assert_eq!(out[i * 2], expect as f32, "discontinuity at frame {expect}");
            assert_eq!(out[i * 2 + 1], -(expect as f32));
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
        unsafe { ring::push(seg.hdr(), seg.data(), &ramp((b * block) as u64, block)) };
    }

    let mut out = vec![0f32; block * CHANNELS as usize];
    let got = unsafe { ring::read(seg.hdr(), seg.data(), &mut cur, &mut out) };

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
    assert_eq!(out[0], (total - USABLE_FRAMES as u64) as f32);
}

#[test]
fn a_block_longer_than_the_ring_keeps_its_tail() {
    let seg = Segment::new();
    let mut cur = Cursor::at_live_edge(seg.hdr());

    let huge = RING_FRAMES as usize + 777;
    unsafe { ring::push(seg.hdr(), seg.data(), &ramp(0, huge)) };

    /* The whole block is published -- in runs of MAX_BLOCK_FRAMES -- but only
     * its last RING_FRAMES can still be in the ring, and only USABLE_FRAMES of
     * those are safe to read. */
    assert_eq!(
        seg.hdr().write_frames.load(core::sync::atomic::Ordering::Acquire),
        huge as u64,
        "every frame handed in is accounted for, even the ones overwritten"
    );

    let mut out = vec![0f32; 16 * CHANNELS as usize];
    let got = unsafe { ring::read(seg.hdr(), seg.data(), &mut cur, &mut out) };
    assert_eq!(got.frames, 16);
    assert_eq!(
        out[0],
        (huge as u64 - USABLE_FRAMES as u64) as f32,
        "the tail is what survives, not the head"
    );
}

#[test]
fn a_new_epoch_throws_the_cursor_away() {
    let seg = Segment::new();
    let mut cur = Cursor::at_live_edge(seg.hdr());
    unsafe { ring::push(seg.hdr(), seg.data(), &ramp(0, 256)) };

    /* A sender claims the slot: new epoch, counter reset. */
    use core::sync::atomic::Ordering;
    seg.hdr().write_frames.store(0, Ordering::Release);
    seg.hdr().epoch.fetch_add(1, Ordering::AcqRel);

    let mut out = vec![0f32; 256 * CHANNELS as usize];
    let got = unsafe { ring::read(seg.hdr(), seg.data(), &mut cur, &mut out) };
    assert!(got.resynced, "a reader must notice the stream restarted");
    assert_eq!(got.frames, 0, "and must not splice across the seam");

    /* And carries on normally afterwards. */
    unsafe { ring::push(seg.hdr(), seg.data(), &ramp(9000, 128)) };
    let got = unsafe { ring::read(seg.hdr(), seg.data(), &mut cur, &mut out) };
    assert!(!got.resynced);
    assert_eq!(got.frames, 128);
    assert_eq!(out[0], 9000.0);
}

#[test]
fn a_label_survives_the_round_trip_and_is_clamped() {
    let seg = Segment::new();
    let hdr = seg.hdr();

    hdr.set_label(b"Bass");
    let got = hdr.label().expect("a settled label reads back");
    assert_eq!(&got[..4], b"Bass");
    assert_eq!(got[4], 0);

    /* 31 bytes plus a NUL, and the caller's 60 are not allowed to run off the
     * end of a 32-byte field. */
    hdr.set_label(&[b'x'; 60]);
    let got = hdr.label().expect("reads back");
    assert_eq!(got[crate::header::LABEL_BYTES - 1], 0, "always NUL-terminated");
    assert_eq!(got.iter().filter(|&&b| b == b'x').count(), 31);
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
 */
#[test]
fn a_reader_is_never_handed_a_splice() {
    use core::sync::atomic::{AtomicBool, Ordering};
    use std::sync::Arc;

    let seg = Segment::new();
    let stop = Arc::new(AtomicBool::new(false));

    let hdr_addr = seg.hdr() as *const Header as usize;
    let data_addr = seg.data() as usize;

    let stop_w = stop.clone();
    let writer = std::thread::spawn(move || {
        let hdr = unsafe { &*(hdr_addr as *const Header) };
        let data = data_addr as *mut f32;
        let mut written = 0u64;
        while !stop_w.load(Ordering::Relaxed) {
            let block = 512usize;
            unsafe { ring::push(hdr, data, &ramp(written, block)) };
            written += block as u64;
        }
        written
    });

    let hdr = unsafe { &*(hdr_addr as *const Header) };
    let data = data_addr as *const f32;
    let mut cur = Cursor::at_live_edge(hdr);
    let mut out = vec![0f32; 4096 * CHANNELS as usize];
    let mut blocks_seen = 0u32;

    for _ in 0..4000 {
        let got = unsafe { ring::read(hdr, data, &mut cur, &mut out) };
        if got.frames > 1 {
            blocks_seen += 1;
            let first = out[0];
            for i in 0..got.frames as usize {
                assert_eq!(
                    out[i * 2],
                    first + i as f32,
                    "SPLICE: frame {i} of a {}-frame block is not contiguous",
                    got.frames
                );
                assert_eq!(out[i * 2 + 1], -(first + i as f32));
            }
        }
        std::thread::yield_now();
    }

    stop.store(true, Ordering::Relaxed);
    let written = writer.join().unwrap();
    assert!(written > 0, "the writer never ran");
    assert!(blocks_seen > 0, "the reader never saw a block");
    drop(seg);
}

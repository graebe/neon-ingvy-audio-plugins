// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The ring: one writer, many readers, no coordination between them.
 *
 * NOTHING HERE KNOWS ABOUT SHARED MEMORY, and that is deliberate. These
 * functions take a `&Header` and a `&[AtomicF32]`, which a test can build on the
 * heap in three lines. The wrap, the lap detection and the resync -- every part
 * that can be subtly wrong -- is therefore testable without shm_open, without a
 * second process, and without a race to reproduce.
 *
 * `shm.rs` supplies those two arguments from a mapping. That is all it does.
 *
 * THE SAMPLES ARE ATOMICS: atomic_float's AtomicF32, which stores and loads a
 * float's bits through an AtomicU32. A reader copies while the writer may be
 * overwriting the same cells -- that is the seqlock's racy read, and the
 * re-check after the copy is what throws a torn result away. With plain floats
 * that overlap would be a data race and undefined behaviour however carefully
 * the result was discarded; relaxed atomic loads and stores compile to the same
 * plain moves and make the overlap merely a wrong answer that gets retried.
 */

use core::mem::{align_of, size_of};
use core::sync::atomic::{fence, AtomicU32, Ordering};

use atomic_float::AtomicF32;

use crate::header::{Header, CHANNELS, RING_FRAMES};

/// What a read actually managed to do.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct Read {
    /// Frames written into the caller's buffer.
    pub frames: u32,
    /// Frames that existed and were lost before this read could reach them.
    pub dropped: u64,
    /// The writer restarted (claimed the slot, or changed sample rate).
    pub resynced: bool,
}

/// A reader's place in the stream. It lives in the READER's memory, never in
/// the segment -- which is what lets any number of readers coexist without
/// knowing about each other.
#[derive(Debug, Clone, Copy)]
pub struct Cursor {
    frames: u64,
    epoch: u32,
}

impl Cursor {
    /// Start at the live edge. A reader joining a bus that has been running for
    /// an hour wants what is happening now, not an hour of backlog it would
    /// spend the next hour catching up on.
    ///
    /// The epoch is loaded FIRST. A restart stores the zeroed count and then
    /// bumps the epoch with Release, so a count read after an epoch belongs to
    /// that epoch or a later one -- and a later one is caught by the next read's
    /// epoch check. The other order could pair a new epoch with the old
    /// stream's huge count, and the reader would wait for the writer to reach
    /// it again.
    pub fn at_live_edge(hdr: &Header) -> Self {
        let epoch = hdr.epoch.load(Ordering::Acquire);
        Cursor {
            frames: hdr.write_frames.load(Ordering::Acquire),
            epoch,
        }
    }
}

const RF: u64 = RING_FRAMES as u64;
const MASK: u64 = RF - 1;
const _: () = assert!(RING_FRAMES.is_power_of_two());

/*
 * THE LARGEST RUN OF FRAMES A WRITER MAY PUBLISH IN ONE STEP, and the reason
 * the reader cannot use the whole ring.
 *
 * `push` copies samples and THEN stores the new count. Between those two
 * moments the samples are in memory and the counter still reads the old value
 * -- so a reader looking at that region sees NEW audio while every arithmetic
 * check it can make says the writer has not got there yet. No amount of
 * re-checking `write_frames` finds this: the check is asking a counter that is
 * deliberately behind.
 *
 * THAT is what handed a spliced buffer to the splice test, roughly one run in
 * three, and it is why the first attempt at a fix -- a fence before the second
 * load -- changed nothing. Ordering was never the problem. The problem was
 * reading inside the window the writer was in the middle of.
 *
 * So the reader stays clear of it. The writer publishes in runs of at most
 * MAX_BLOCK_FRAMES, which bounds that window; the reader treats the usable
 * history as RING_FRAMES minus that bound, and never reads within it. 8192
 * frames is four times the largest block any host here asks for, and costs 6%
 * of the ring.
 */
pub const MAX_BLOCK_FRAMES: u32 = 8192;

/// How much history a reader may actually use -- see MAX_BLOCK_FRAMES.
pub const USABLE_FRAMES: u32 = RING_FRAMES - MAX_BLOCK_FRAMES;
const USABLE: u64 = USABLE_FRAMES as u64;

/// Samples in the ring: `RING_FRAMES * CHANNELS`.
pub const RING_SAMPLES: usize = RING_FRAMES as usize * CHANNELS as usize;

/*
 * A SAMPLE IS FOUR BYTES OF THE SEGMENT, and AtomicF32 has to stay exactly
 * that. It is the f32 itself, reached through an AtomicU32's view of the same
 * bytes -- the AtomicU32 this ring stored bits in before it used the crate --
 * so a segment written by either build reads the same in the other, and
 * `segment_size` can count the ring in f32s. A crate's layout is its own
 * business until it is a shared-memory format, so it is asserted here.
 */
const _: () = assert!(size_of::<AtomicF32>() == size_of::<AtomicU32>());
const _: () = assert!(align_of::<AtomicF32>() == align_of::<AtomicU32>());
const _: () = assert!(size_of::<AtomicF32>() == size_of::<f32>());

/// Publish `src` (interleaved stereo, `src.len() / 2` frames).
///
/// `data` is `hdr`'s ring, `RING_SAMPLES` long. The caller must be the slot's
/// single writer: two concurrent pushers corrupt the stream (no undefined
/// behaviour -- everything is atomic -- but no meaningful audio either).
pub fn push(hdr: &Header, data: &[AtomicF32], src: &[f32]) {
    assert_eq!(data.len(), RING_SAMPLES);
    let ch = CHANNELS as usize;
    let frames = src.len() / ch;
    if frames == 0 {
        return;
    }

    /*
     * PUBLISHED IN RUNS OF AT MOST MAX_BLOCK_FRAMES, which is what lets a
     * reader know how far to stay clear. A single copy spanning more than that
     * would widen the window in which samples are written but uncounted, and
     * the reader's margin is computed from this bound.
     *
     * A host that hands us more than the whole ring keeps only its tail: no
     * host sends 2.7 seconds in one callback, but the alternative to handling
     * it is a copy that runs off the end of the mapping, and "unreachable in
     * practice" is not a memory-safety argument.
     */
    let skip = frames.saturating_sub(RING_FRAMES as usize);
    if skip > 0 {
        /*
         * ACCOUNTED FOR EVEN THOUGH THEY ARE NOT KEPT. The counter is the
         * stream's position, not a record of what happens to still be in the
         * ring -- so frames we cannot hold still advance it, and the reader
         * that snaps forward reports them as dropped. Leaving the counter
         * behind instead would lose them silently, which is the one thing this
         * whole design is arranged not to do.
         */
        let w = hdr.write_frames.load(Ordering::Relaxed);
        hdr.write_frames.store(w + skip as u64, Ordering::Release);
    }
    let mut off = skip;

    while off < frames {
        let n = core::cmp::min(MAX_BLOCK_FRAMES as usize, frames - off);
        let chunk = &src[off * ch..(off + n) * ch];

        /* Only the writer stores this, so Relaxed is enough to read our own
         * value back. The Release below is what publishes the samples. */
        let w = hdr.write_frames.load(Ordering::Relaxed);
        let start = (w & MASK) as usize;
        let first = core::cmp::min(n, RING_FRAMES as usize - start);

        store(&data[start * ch..(start + first) * ch], &chunk[..first * ch]);
        if first < n {
            store(&data[..(n - first) * ch], &chunk[first * ch..]);
        }

        /* RELEASE, and it must come after the copies: a reader that sees the
         * new count must see the samples it counts. */
        hdr.write_frames.store(w + n as u64, Ordering::Release);
        off += n;
    }
}

fn store(dst: &[AtomicF32], src: &[f32]) {
    for (d, &s) in dst.iter().zip(src) {
        d.store(s, Ordering::Relaxed);
    }
}

fn load(dst: &mut [f32], src: &[AtomicF32]) {
    for (d, s) in dst.iter_mut().zip(src) {
        *d = s.load(Ordering::Relaxed);
    }
}

/// Copy up to `out.len() / 2` frames into `out`, advancing `cur`. Only loads:
/// `data` may live in a read-only mapping.
pub fn read(hdr: &Header, data: &[AtomicF32], cur: &mut Cursor, out: &mut [f32]) -> Read {
    read_with(hdr, data, cur, out, || {})
}

/// `read`, with `during_copy` run between the copy and the re-check -- the
/// window a concurrent writer can act in. Tests use it to place a writer's
/// move exactly there instead of hoping a thread lands in it.
pub(crate) fn read_with(
    hdr: &Header,
    data: &[AtomicF32],
    cur: &mut Cursor,
    out: &mut [f32],
    during_copy: impl FnOnce(),
) -> Read {
    assert_eq!(data.len(), RING_SAMPLES);
    let ch = CHANNELS as usize;
    let mut result = Read::default();

    /*
     * THE EPOCH COMES FIRST. A writer that has just claimed the slot reset the
     * stream; frames measured before that are not older audio from this source,
     * they are audio from a DIFFERENT source, possibly at a different rate.
     * Splicing them would draw a transient that never happened.
     */
    let epoch = hdr.epoch.load(Ordering::Acquire);
    if epoch != cur.epoch {
        *cur = Cursor::at_live_edge(hdr);
        result.resynced = true;
        return result;
    }

    let w = hdr.write_frames.load(Ordering::Acquire);
    let mut avail = w.saturating_sub(cur.frames);

    /* ALREADY TOO FAR BEHIND before we even started. Snap to the oldest frame
     * that is both intact AND outside the writer's current run, and say how
     * much went past. USABLE rather than RF: see MAX_BLOCK_FRAMES. */
    if avail > USABLE {
        result.dropped = avail - USABLE;
        cur.frames = w - USABLE;
        avail = USABLE;
    }

    let want = core::cmp::min(avail, (out.len() / ch) as u64) as usize;
    if want == 0 {
        return result;
    }

    let start = (cur.frames & MASK) as usize;
    let first = core::cmp::min(want, RING_FRAMES as usize - start);
    load(&mut out[..first * ch], &data[start * ch..(start + first) * ch]);
    if first < want {
        load(&mut out[first * ch..want * ch], &data[..(want - first) * ch]);
    }
    during_copy();

    /*
     * AND NOW CHECK AGAIN, which is the part that is easy to leave out.
     *
     * The writer is on the audio thread and does not wait for us. It can lap
     * the reader DURING the copy above, in which case the early part of `out`
     * holds samples the writer has already overwritten with newer ones -- a
     * buffer that is half old and half new, spliced at an arbitrary point. It
     * looks like audio. It is not.
     *
     * So: if the writer advanced past our margin while we copied, throw the
     * whole copy away and resync. Reporting nothing is honest; reporting a
     * splice is not.
     *
     * THIS CHECK ALONE IS NOT ENOUGH, and believing it was cost an afternoon.
     * It can only see what `write_frames` admits to, and a writer that has
     * copied its samples but not yet stored the count is invisible to it --
     * which is what MAX_BLOCK_FRAMES exists to handle. The margin is what
     * makes this sound; this is the second line of defence, for a reader that
     * was simply too slow.
     *
     * The fence keeps the sample reads above from being deferred past these
     * loads: an acquire LOAD stops later accesses moving earlier, not earlier
     * ones moving later, which is the wrong direction for a check that has to
     * happen after the reads it validates.
     *
     * THE EPOCH IS RE-CHECKED TOO. A writer that restarted during the copy
     * reset `write_frames` to a small number and began overwriting the ring
     * from the start -- which the lap check reads as "no distance at all" and
     * would pass. A restart is the one overwrite the frame count cannot show,
     * so the epoch has to.
     */
    fence(Ordering::Acquire);
    if hdr.epoch.load(Ordering::Relaxed) != cur.epoch {
        *cur = Cursor::at_live_edge(hdr);
        result.resynced = true;
        return result;
    }
    let w2 = hdr.write_frames.load(Ordering::Relaxed);
    if w2.saturating_sub(cur.frames) > USABLE {
        result.dropped += w2 - cur.frames;
        cur.frames = w2;
        return result;
    }

    cur.frames += want as u64;
    result.frames = want as u32;
    result
}

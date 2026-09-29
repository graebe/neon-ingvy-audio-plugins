/*
 * The ring: one writer, many readers, no coordination between them.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * NOTHING HERE KNOWS ABOUT SHARED MEMORY, and that is deliberate. These
 * functions take a `&Header` and a `*mut f32`, which a test can build on the
 * heap in three lines. The wrap, the lap detection and the resync -- every part
 * that can be subtly wrong -- is therefore testable without shm_open, without a
 * second process, and without a race to reproduce.
 *
 * `shm.rs` supplies those two arguments from a mapping. That is all it does.
 */

use core::sync::atomic::Ordering;

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
    pub fn at_live_edge(hdr: &Header) -> Self {
        Cursor {
            frames: hdr.write_frames.load(Ordering::Acquire),
            epoch: hdr.epoch.load(Ordering::Acquire),
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

/// Publish `src` (interleaved stereo, `src.len() / 2` frames).
///
/// # Safety
/// `data` must point at `RING_FRAMES * CHANNELS` writable floats belonging to
/// `hdr`'s segment, and the caller must be the slot's single writer.
pub unsafe fn push(hdr: &Header, data: *mut f32, src: &[f32]) {
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

        core::ptr::copy_nonoverlapping(chunk.as_ptr(), data.add(start * ch), first * ch);
        if first < n {
            core::ptr::copy_nonoverlapping(
                chunk.as_ptr().add(first * ch),
                data,
                (n - first) * ch,
            );
        }

        /* RELEASE, and it must come after the copies: a reader that sees the
         * new count must see the samples it counts. */
        hdr.write_frames.store(w + n as u64, Ordering::Release);
        off += n;
    }

    hdr.heartbeat.fetch_add(1, Ordering::Relaxed);
}

/// Copy up to `out.len() / 2` frames into `out`, advancing `cur`.
///
/// # Safety
/// As `push`, but `data` need only be readable.
pub unsafe fn read(hdr: &Header, data: *const f32, cur: &mut Cursor, out: &mut [f32]) -> Read {
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
        cur.epoch = epoch;
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
    core::ptr::copy_nonoverlapping(data.add(start * ch), out.as_mut_ptr(), first * ch);
    if first < want {
        core::ptr::copy_nonoverlapping(
            data,
            out.as_mut_ptr().add(first * ch),
            (want - first) * ch,
        );
    }

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
     * The fence keeps the sample reads above from being deferred past this
     * load: an acquire LOAD stops later accesses moving earlier, not earlier
     * ones moving later, which is the wrong direction for a check that has to
     * happen after the reads it validates.
     */
    core::sync::atomic::fence(Ordering::Acquire);
    let w2 = hdr.write_frames.load(Ordering::Acquire);
    if w2.saturating_sub(cur.frames) > USABLE {
        result.dropped += w2 - cur.frames;
        cur.frames = w2;
        return result;
    }

    cur.frames += want as u64;
    result.frames = want as u32;
    result
}

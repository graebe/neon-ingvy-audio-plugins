// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * audio-bus -- a shared-memory audio bus between plugins in one host.
 *
 * A sender claims one of sixteen numbered slots and publishes stereo float
 * audio into it. Any number of receivers, in this process or another, open the
 * same slot and read it. Nobody blocks anybody: the sender never waits, and a
 * receiver that falls behind is told how much it missed instead of being handed
 * a splice.
 *
 * NOT A PRODUCT ENGINE. `docs/tech/structure.md` says a crate belongs to
 * exactly one product and the engines never depend on each other -- and that
 * rule still holds, because it is about PRODUCT engines. This is the house
 * transport, the Rust counterpart of the native UI kit: it exists so that two products
 * can share one thing, and it depends on no product in return.
 *
 * THE THREAD RULES ARE PART OF THE ABI, exactly as they are for spectro:
 *
 *   Writer::claim / drop / set_label / set_sample_rate      the main thread
 *   Pusher::push / push_at                                  the audio thread, and only it
 *   Reader::open / drop                                     the main thread
 *   Reader::reattach                                        any thread but the audio thread
 *   Reader::read / stamp_at                                 one thread, the same one each time
 *
 * A claim hands back TWO handles, and the split is what makes those rules the
 * compiler's business rather than a comment's: the audio thread owns the
 * `Pusher`, the main thread owns the `Writer`, and each mutates only through
 * `&mut self` -- so safe code cannot push from two threads, or set a label from
 * two, however the handles are passed around. The slot is released when the
 * last of the two is dropped.
 *
 * `push` allocates nothing and makes no system call. `claim` does both, which
 * is why it is not allowed near the audio thread.
 */

pub mod header;
pub mod ring;
pub mod shm;

#[cfg(test)]
mod claims;
#[cfg(test)]
mod slots;
#[cfg(test)]
mod tests;

pub use header::{segment_size, CHANNELS, LABEL_BYTES, RING_FRAMES};
pub use ring::{Read, Span};
pub use shm::MAX_SLOT;

use core::sync::atomic::{AtomicU32, Ordering};
use std::sync::Arc;

use header::{owner_count, owner_pid, owner_word, Header};

/// Why a claim did not succeed.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ClaimError {
    /// Slot number outside 1..=MAX_SLOT.
    BadSlot,
    /// The segment could not be created or mapped.
    Unavailable,
    /// Another live sender holds this slot.
    Taken,
}

/*
 * THE CLAIM ITSELF, and the liveness question it has to answer first.
 *
 * A host that crashed with a set open leaves the segment behind -- POSIX shm
 * outlives the process that made it, all the way to a reboot. So a held slot is
 * not necessarily a taken one, and refusing it forever would mean one crash
 * costs you a bus number until you restart the machine. (Windows takes a dead
 * process's names with its handles, so there a crashed holder is met only by
 * a claimer that already had the segment open.)
 *
 * Only `the pid is gone` -- kill(pid, 0) == ESRCH, or Windows knowing no such
 * process or its exit code -- concludes anything, and it concludes the writer
 * is GONE, never that one is alive: a pid can be recycled onto an unrelated
 * process, which then keeps the slot looking held. That is the conservative
 * failure and the one chosen.
 *
 * NO EXEMPTION FOR OUR OWN PID, and an earlier draft had one. Two Listen-Ins
 * on slot 3 in the same Live process share a pid, so "it is only me" would
 * have let the second instance silently STEAL the bus from the first --
 * exactly the collision the slot is supposed to report. A live holder is a
 * live holder.
 *
 * ONE COMPARE-AND-SWAP DECIDES, from the exact owner word that was judged to
 * the claimer's own. Two reclaimers of one dead pid both judge the same word;
 * whichever swaps first changes it, and the other's swap fails and re-judges a
 * holder that is now alive. See `header::owner_word` for why state and pid are
 * one word and why it carries a count.
 *
 * `is_gone` is a parameter so the tests can drive two claimers through any
 * interleaving on a heap header.
 */
pub(crate) fn acquire(hdr: &Header, me: u32, is_gone: impl Fn(u32) -> bool) -> Result<u64, ClaimError> {
    let mut seen = hdr.owner.load(Ordering::Acquire);
    /* Bounded: every lost swap means somebody else claimed or released in
     * between, and a slot that busy is taken for any practical purpose. */
    for _ in 0..8 {
        let holder = owner_pid(seen);
        if holder != 0 && !is_gone(holder) {
            return Err(ClaimError::Taken);
        }
        let mine = owner_word(owner_count(seen).wrapping_add(1), me);
        match hdr.owner.compare_exchange(seen, mine, Ordering::AcqRel, Ordering::Acquire) {
            Ok(_) => return Ok(mine),
            Err(now) => seen = now,
        }
    }
    Err(ClaimError::Taken)
}

/// Give the slot back -- only if `token` still holds it. Returns whether it did.
pub(crate) fn release(hdr: &Header, token: u64) -> bool {
    hdr.owner
        .compare_exchange(
            token,
            owner_word(owner_count(token), 0),
            Ordering::AcqRel,
            Ordering::Acquire,
        )
        .is_ok()
}

/// Does the slot's NAME still lead to the segment `map` is? False once it was
/// unlinked, or unlinked and created afresh.
fn still_named(map: &shm::Shm) -> bool {
    shm::Shm::open_existing(map.slot()).is_some_and(|now| {
        now.header().is_valid()
            && now.header().incarnation.load(Ordering::Relaxed)
                == map.header().incarnation.load(Ordering::Relaxed)
    })
}

/// What the two halves of a claim share. Dropping it -- when the last half
/// goes -- releases the slot.
struct Claim {
    map: shm::Shm,
    token: u64,
    /* A rate change the main thread asked for and the audio thread has not
     * applied yet; 0 when there is none. See `Writer::set_sample_rate`. */
    pending_rate: AtomicU32,
}

/// The main thread's half of a claimed slot. One per Listen-In instance.
pub struct Writer {
    claim: Arc<Claim>,
}

/// The audio thread's half of a claimed slot.
pub struct Pusher {
    claim: Arc<Claim>,
}

impl Writer {
    /// Take slot `slot`, or say why not.
    pub fn claim(slot: u32, sample_rate: u32) -> Result<(Writer, Pusher), ClaimError> {
        if slot == 0 || slot > MAX_SLOT {
            return Err(ClaimError::BadSlot);
        }
        let me = shm::pid();

        /*
         * A FEW ROUNDS, because two things can send a claimer back to the
         * start: a segment it cannot interpret, which it replaces, and a
         * segment that stopped being the one the name leads to while it was
         * claiming it. Each round opens the name afresh.
         */
        for _ in 0..4 {
            let (map, created) = match shm::Shm::create_or_open(slot) {
                Ok(opened) => opened,
                /* A SHORTER SEGMENT, an older format's -- the same case as the
                 * one below, met before it could be mapped. Replaced the same
                 * way: by taking its name. */
                Err(shm::Refused::Foreign) => {
                    shm::Shm::unlink_slot(slot);
                    continue;
                }
                Err(shm::Refused::Failed) => return Err(ClaimError::Unavailable),
            };
            let hdr = map.header();

            if created {
                hdr.initialise(sample_rate, shm::incarnation());
            } else if !hdr.is_valid() {
                /*
                 * A SEGMENT FROM A BUILD THAT IS NOT THIS ONE (or one still
                 * being initialised by another claimer, which then finds out
                 * below). It cannot be interpreted and it cannot be repaired
                 * in place -- a reader may be mid-copy inside it right now,
                 * under the old layout. Unlinking detaches the NAME from the
                 * object, so that reader keeps its mapping and simply sees a
                 * writer that stopped, while the next round creates a fresh
                 * segment under the same name.
                 */
                map.unlink();
                continue;
            }

            let token = acquire(hdr, me, shm::pid_is_gone)?;

            /*
             * AND THEN MAKE SURE IT IS STILL THE BUS. The name could have been
             * unlinked between our open and our claim -- by a departing writer,
             * or by a claimer that caught this segment half-initialised -- and
             * a claim on an orphaned segment is a sender nobody can hear: every
             * reader from now on opens whatever the name leads to instead. So
             * let go of the orphan and start again from the name.
             */
            if !still_named(&map) {
                release(hdr, token);
                continue;
            }

            hdr.sample_rate.store(sample_rate, Ordering::Relaxed);
            hdr.write_frames.store(0, Ordering::Relaxed);
            /* LAST, with Release: the bump is what tells every reader to throw
             * its cursor away, and a reader that sees it must also see the
             * reset above -- otherwise it resyncs onto the stream we are still
             * in the middle of clearing. */
            hdr.epoch.fetch_add(1, Ordering::Release);

            let claim = Arc::new(Claim {
                map,
                token,
                pending_rate: AtomicU32::new(0),
            });
            return Ok((
                Writer {
                    claim: claim.clone(),
                },
                Pusher { claim },
            ));
        }
        Err(ClaimError::Unavailable)
    }

    pub fn slot(&self) -> u32 {
        self.claim.map.slot()
    }

    /// The host's rate changed, so the stream restarts under a new epoch --
    /// samples either side of the change are not the same signal.
    ///
    /// POSTED, NOT APPLIED. The restart resets `write_frames`, which `push`
    /// read-modify-writes on the audio thread; a reset from here could land
    /// between its load and its store and be undone, or undo a block. So this
    /// leaves a request and the Pusher applies it at the top of its next
    /// `push`, the only place the count is written. Until then readers see the
    /// old rate -- which is also true: nothing has been published at the new
    /// one. A rate of 0 is not a rate and is ignored.
    pub fn set_sample_rate(&mut self, sample_rate: u32) {
        if sample_rate != 0 {
            self.claim.pending_rate.store(sample_rate, Ordering::Release);
        }
    }

    /// Set the display name. Anything past 31 bytes is dropped, at a
    /// character boundary.
    pub fn set_label(&mut self, text: &str) {
        self.claim.map.header().set_label(text);
    }
}

impl Pusher {
    pub fn slot(&self) -> u32 {
        self.claim.map.slot()
    }

    /// Publish one block. Interleaved stereo, `src.len() / 2` frames.
    ///
    /// THE AUDIO THREAD CALLS THIS. It allocates nothing, takes no lock and
    /// makes no system call.
    pub fn push(&mut self, src: &[f32]) {
        self.push_at(src, None);
    }

    /// `push`, for a block whose first frame is the host's timeline sample
    /// `timeline` -- `None` while the host gives no position. What lets a
    /// reader on another track line a frame up with its own (header::Stamp).
    pub fn push_at(&mut self, src: &[f32], timeline: Option<i64>) {
        let hdr = self.claim.map.header();
        if self.claim.pending_rate.load(Ordering::Relaxed) != 0 {
            let rate = self.claim.pending_rate.swap(0, Ordering::Acquire);
            if rate != 0 && rate != hdr.sample_rate.load(Ordering::Relaxed) {
                hdr.sample_rate.store(rate, Ordering::Relaxed);
                hdr.write_frames.store(0, Ordering::Relaxed);
                hdr.epoch.fetch_add(1, Ordering::Release);
            }
        }
        ring::push(hdr, self.claim.map.data(), self.claim.map.stamps(), src, timeline);
    }
}

impl Drop for Claim {
    fn drop(&mut self) {
        let hdr = self.map.header();
        /*
         * ONLY IF THE SLOT IS STILL OURS.
         *
         * A host that froze for long enough to look dead can come back, by
         * which time another sender may have reclaimed the slot legitimately.
         * Releasing unconditionally would then mark THEIR claim free and
         * unlink THEIR segment on our way out -- a departing instance taking a
         * working bus with it. The token is exact: the count in it changes on
         * every claim, so a later claim by this same process does not match.
         */
        if hdr.owner.load(Ordering::Acquire) != self.token {
            return;
        }
        /*
         * UNLINK FIRST, THEN LET GO, which is what keeps a clean quit from
         * leaving anything behind -- and the order matters. Marked free while
         * still named, the segment could be claimed by somebody who opened it a
         * moment earlier, and then unlinked out from under them. Unlinked
         * first, a claimer that opened it before the unlink finds it free only
         * after it has stopped being the bus, and `claim`'s name check sends
         * that claimer back to create a fresh one.
         *
         * The name is only removed if it still leads HERE: a segment replaced
         * by a build that could not read it is not ours to unlink any more.
         * Readers still mapped keep their mapping -- unlink removes the name,
         * not the object -- and see a writer that simply stopped until they
         * `reattach`.
         *
         * On Windows the unlink is a no-op and the name goes with this claim's
         * own handle, closed when `map` is dropped -- after the release, which
         * is safe there: a claimer that opened the segment first holds a
         * handle of its own, so the name stays on the segment it claims.
         */
        if still_named(&self.map) {
            self.map.unlink();
        }
        release(hdr, self.token);
    }
}

/// What a receiver can learn about a slot without reading any audio.
#[derive(Debug, Clone)]
pub struct Info {
    pub slot: u32,
    pub live: bool,
    pub sample_rate: u32,
    pub label: String,
}

/// The receiving end. Any number of these, in any number of processes.
pub struct Reader {
    map: shm::Shm,
    cursor: ring::Cursor,
    /* Set by `reattach`; the next `read` reports it as a resync. */
    moved: bool,
}

impl Reader {
    /// Open slot `slot` at the live edge. Succeeds even if nothing is sending
    /// yet -- a receiver's dropdown wants to show an idle bus, not hide it.
    pub fn open(slot: u32) -> Option<Reader> {
        if slot == 0 || slot > MAX_SLOT {
            return None;
        }
        let map = shm::Shm::open_existing(slot)?;
        if !map.header().is_valid() {
            return None;
        }
        let cursor = ring::Cursor::at_live_edge(map.header());
        Some(Reader {
            map,
            cursor,
            moved: false,
        })
    }

    pub fn slot(&self) -> u32 {
        self.map.slot()
    }

    /// Copy up to `out.len() / 2` frames out. Never blocks, never allocates.
    pub fn read(&mut self, out: &mut [f32]) -> Read {
        let mut got = ring::read(self.map.header(), self.map.data(), &mut self.cursor, out);
        if core::mem::take(&mut self.moved) {
            got.resynced = true;
        }
        got
    }

    /// The stream position of the next frame `read` delivers: a read of `n`
    /// frames delivered `position() - n ..`.
    pub fn position(&self) -> u64 {
        self.cursor.position()
    }

    /// Where frame `frame` of the stream sits on the sender's timeline: the
    /// stamped run that holds it, if its stamp is still there. `frame` is a
    /// stream position, as `Read::first` gives. Loads only, no allocation --
    /// the thread that reads may ask.
    pub fn stamp_at(&self, frame: u64) -> Option<ring::Span> {
        ring::stamp_at(self.map.header(), self.map.stamps(), self.cursor.epoch(), frame)
    }

    /*
     * A SEGMENT CAN BE REPLACED UNDER ITS NAME, and a mapping cannot notice.
     *
     * A sender that quits takes the name from its segment -- unlinks it, or on
     * Windows closes the handle that held it; the next sender on that slot
     * creates a new one. A reader still mapping the old object sees a writer
     * that stopped -- forever, because nothing will ever write there again.
     * The Spectrogram showed exactly that: a Listen-In re-added, a picture that
     * stayed "starved".
     *
     * So a reader that has gone quiet asks: does the name still lead to the
     * segment I have? Each segment carries its `incarnation`, and a different
     * one behind the name means ours is orphaned; the reader moves to the new
     * one at its live edge and the next read says `resynced`. This makes
     * system calls and maps memory, so it is never an audio-thread call -- a
     * receiver makes it for a source that has been silent a while, from the
     * thread that reads it, not per block.
     */

    /// Move to the segment the slot's name leads to now, if that is not the
    /// one this reader has. Returns true if it moved. **Not the audio thread.**
    pub fn reattach(&mut self) -> bool {
        let Some(fresh) = shm::Shm::open_existing(self.map.slot()) else {
            return false;
        };
        let (old, new) = (self.map.header(), fresh.header());
        if !new.is_valid()
            || new.incarnation.load(Ordering::Relaxed) == old.incarnation.load(Ordering::Relaxed)
        {
            return false;
        }
        self.cursor = ring::Cursor::at_live_edge(new);
        self.map = fresh;
        self.moved = true;
        true
    }

    /// The rate the sender publishes at. Allocation-free, unlike `info`.
    pub fn sample_rate(&self) -> u32 {
        self.map.header().sample_rate.load(Ordering::Acquire)
    }

    pub fn info(&self) -> Info {
        info_from(self.map.header(), self.map.slot())
    }
}

fn info_from(hdr: &Header, slot: u32) -> Info {
    let holder = owner_pid(hdr.owner.load(Ordering::Acquire));
    let live = holder != 0 && !shm::pid_is_gone(holder);
    let label = hdr
        .label()
        .map(|raw| {
            let end = raw.iter().position(|&b| b == 0).unwrap_or(0);
            String::from_utf8_lossy(&raw[..end]).into_owned()
        })
        .unwrap_or_default();
    Info {
        slot,
        live,
        sample_rate: hdr.sample_rate.load(Ordering::Acquire),
        label,
    }
}

/// Look at a slot without holding it open -- what a receiver's source list is
/// built from. Returns `None` for a slot nobody has ever used.
pub fn probe(slot: u32) -> Option<Info> {
    /* `open_existing`, never `create_or_open`: see the note in shm.rs. A
     * dropdown that walks sixteen slots must not bring sixteen buses into
     * existence to find out that none of them are there. */
    let map = shm::Shm::open_existing(slot)?;
    if !map.header().is_valid() {
        return None;
    }
    Some(info_from(map.header(), map.slot()))
}

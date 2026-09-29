/*
 * audio-bus -- a shared-memory audio bus between plugins in one host.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
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
 * transport, the Rust counterpart of `ui-kit`: it exists so that two products
 * can share one thing, and it depends on no product in return.
 *
 * THE THREAD RULES ARE PART OF THE ABI, exactly as they are for spectro:
 *
 *   Writer::claim / release / set_label / set_sample_rate   the main thread
 *   Writer::push                                            the audio thread, and only it
 *   Reader::open / close                                    the main thread
 *   Reader::read                                            one thread, the same one each time
 *
 * `push` allocates nothing and makes no system call. `claim` does both, which
 * is why it is not allowed near the audio thread.
 */

pub mod header;
pub mod ring;
pub mod shm;

#[cfg(test)]
mod slots;
#[cfg(test)]
mod tests;

pub use header::{segment_size, CHANNELS, LABEL_BYTES, RING_FRAMES};
pub use ring::Read;
pub use shm::MAX_SLOT;

use core::sync::atomic::Ordering;
use header::{STATE_CLAIMED, STATE_FREE};

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

/// The sending end. One per Listen-In instance.
pub struct Writer {
    map: shm::Mapping,
}

impl Writer {
    /// Take slot `slot`, or say why not.
    pub fn claim(slot: u32, sample_rate: u32) -> Result<Writer, ClaimError> {
        if slot == 0 || slot > MAX_SLOT {
            return Err(ClaimError::BadSlot);
        }
        let (map, created) = shm::Mapping::create_or_open(slot).ok_or(ClaimError::Unavailable)?;
        let me = shm::pid();

        if created {
            map.header().initialise(sample_rate, me);
        } else if !map.header().is_valid() {
            /*
             * A SEGMENT FROM A BUILD THAT IS NOT THIS ONE. It cannot be
             * interpreted and it cannot be repaired in place -- a reader may be
             * mid-copy inside it right now, under the old layout. Unlinking
             * detaches the NAME from the object, so that reader keeps its
             * mapping and simply sees a writer that stopped, while the next
             * claim creates a fresh segment under the same name.
             */
            map.unlink();
            drop(map);
            let (fresh, created_now) =
                shm::Mapping::create_or_open(slot).ok_or(ClaimError::Unavailable)?;
            if !created_now {
                return Err(ClaimError::Unavailable);
            }
            fresh.header().initialise(sample_rate, me);
            return Ok(Writer { map: fresh });
        }

        let hdr = map.header();

        /*
         * THE CLAIM ITSELF, and the liveness question it has to answer first.
         *
         * A host that crashed with a set open leaves the segment behind -- shm
         * outlives the process that made it, all the way to a reboot. So a
         * CLAIMED slot is not necessarily a taken one, and refusing it forever
         * would mean one crash costs you a bus number until you restart the
         * machine.
         *
         * The evidence is deliberately two-sided, because each half alone lies:
         * a pid can be recycled onto an unrelated process, and a heartbeat can
         * be stalled by a host that is merely paused rather than dead. Only
         * `the pid is gone` -- kill(pid, 0) == ESRCH -- concludes anything, and
         * it concludes the writer is GONE, never that one is alive.
         */
        let mut spins = 0;
        loop {
            match hdr.state.compare_exchange(
                STATE_FREE,
                STATE_CLAIMED,
                Ordering::AcqRel,
                Ordering::Acquire,
            ) {
                Ok(_) => break,
                Err(_) => {
                    let holder = hdr.writer_pid.load(Ordering::Acquire);
                    /*
                     * NO EXEMPTION FOR OUR OWN PID, and an earlier draft had
                     * one. Two Listen-Ins on slot 3 in the same Live process
                     * share a pid, so "it is only me" would have let the second
                     * instance silently STEAL the bus from the first -- exactly
                     * the collision the slot is supposed to report. A live
                     * holder is a live holder.
                     */
                    if !shm::pid_is_gone(holder) {
                        return Err(ClaimError::Taken);
                    }
                    /* The holder's process is gone. Put the slot back and go
                     * round; the CAS above is what settles a race between two
                     * reclaimers, so only one of them wins. */
                    if hdr
                        .state
                        .compare_exchange(
                            STATE_CLAIMED,
                            STATE_FREE,
                            Ordering::AcqRel,
                            Ordering::Acquire,
                        )
                        .is_err()
                    {
                        return Err(ClaimError::Taken);
                    }
                    spins += 1;
                    if spins > 4 {
                        return Err(ClaimError::Taken);
                    }
                }
            }
        }

        hdr.writer_pid.store(me, Ordering::Release);
        hdr.sample_rate.store(sample_rate, Ordering::Release);
        hdr.write_frames.store(0, Ordering::Release);
        /* LAST: the bump is what tells every reader to throw its cursor away.
         * Doing it before the resets above would let a reader resync onto the
         * stream we are still in the middle of clearing. */
        hdr.epoch.fetch_add(1, Ordering::AcqRel);

        Ok(Writer { map })
    }

    pub fn slot(&self) -> u32 {
        self.map.slot()
    }

    /// Publish one block. Interleaved stereo, `src.len() / 2` frames.
    ///
    /// THE AUDIO THREAD CALLS THIS. It allocates nothing, takes no lock and
    /// makes no system call.
    pub fn push(&self, src: &[f32]) {
        unsafe { ring::push(self.map.header(), self.map.data(), src) }
    }

    /// The host's rate changed, so the stream restarts under a new epoch --
    /// samples either side of the change are not the same signal.
    pub fn set_sample_rate(&self, sample_rate: u32) {
        let hdr = self.map.header();
        if hdr.sample_rate.load(Ordering::Acquire) == sample_rate {
            return;
        }
        hdr.sample_rate.store(sample_rate, Ordering::Release);
        hdr.write_frames.store(0, Ordering::Release);
        hdr.epoch.fetch_add(1, Ordering::AcqRel);
    }

    pub fn set_label(&self, text: &str) {
        self.map.header().set_label(text.as_bytes());
    }
}

impl Drop for Writer {
    fn drop(&mut self) {
        let hdr = self.map.header();
        /*
         * ONLY IF THE SLOT IS STILL OURS.
         *
         * A host that froze for long enough to look dead can come back, by
         * which time another sender may have reclaimed the slot legitimately.
         * Releasing unconditionally would then mark THEIR claim free and
         * unlink THEIR segment on our way out -- a departing instance taking a
         * working bus with it.
         */
        if hdr.writer_pid.load(Ordering::Acquire) != shm::pid() {
            return;
        }
        hdr.state.store(STATE_FREE, Ordering::Release);
        hdr.writer_pid.store(0, Ordering::Release);
        /*
         * UNLINK ON THE WAY OUT, which is what keeps a clean quit from leaving
         * anything behind for the reclaim path above to have to reason about.
         * Readers still mapped keep their mapping -- unlink removes the name,
         * not the object -- and see a writer that simply stopped.
         */
        self.map.unlink();
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
    map: shm::Mapping,
    cursor: ring::Cursor,
}

impl Reader {
    /// Open slot `slot` at the live edge. Succeeds even if nothing is sending
    /// yet -- a receiver's dropdown wants to show an idle bus, not hide it.
    pub fn open(slot: u32) -> Option<Reader> {
        if slot == 0 || slot > MAX_SLOT {
            return None;
        }
        let map = shm::Mapping::open_existing(slot)?;
        if !map.header().is_valid() {
            return None;
        }
        let cursor = ring::Cursor::at_live_edge(map.header());
        Some(Reader { map, cursor })
    }

    /// Copy up to `out.len() / 2` frames out. Never blocks, never allocates.
    pub fn read(&mut self, out: &mut [f32]) -> Read {
        unsafe { ring::read(self.map.header(), self.map.data(), &mut self.cursor, out) }
    }

    pub fn info(&self) -> Info {
        info_from(self.map.header(), self.map.slot())
    }
}

fn info_from(hdr: &header::Header, slot: u32) -> Info {
    let live = hdr.state.load(Ordering::Acquire) == STATE_CLAIMED
        && !shm::pid_is_gone(hdr.writer_pid.load(Ordering::Acquire));
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
    let map = shm::Mapping::open_existing(slot)?;
    if !map.header().is_valid() {
        return None;
    }
    Some(info_from(map.header(), map.slot()))
}

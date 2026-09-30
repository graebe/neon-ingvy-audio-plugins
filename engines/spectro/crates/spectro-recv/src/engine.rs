/*
 * The pump: every source's audio into its analyzer, in step.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * Everything the transforms touch lives here, owned by whichever thread pumps
 * -- the receiver's worker (worker.rs), or the caller of `Receiver::pump` when
 * no worker was started. The message thread keeps only the other ends: each
 * analyzer's `Consumer`, and a `BusStatus` per bus it can read without asking.
 */
use core::sync::atomic::{AtomicBool, AtomicU64, AtomicUsize, Ordering};
use std::sync::Arc;
use std::thread::Thread;

use bus_core::Reader;
use spectro_core::Producer;

use crate::ring::MonoConsumer;

/// The most frames one `pump` will move per source; it sizes the scratch.
pub const PUMP_FRAMES: usize = 4096;

/// How long a bus may come up short before it is treated as silent rather
/// than waited for: long enough that ordinary jitter -- a bus a block behind on
/// one tick -- waits and catches up, short enough that a muted Listen-In does
/// not stall the picture for anything a person would notice.
///
/// WALL TIME, NOT FRAMES, and that is the fix rather than a detail: a bus with
/// nothing at all contributes no frames to count, so a frame-based clock never
/// advances and the grace period never ends. The first version of this froze
/// exactly as hard as the bug it replaced.
pub const GRACE_US: u64 = 200_000;

/// How long a bus may deliver nothing before the receiver asks whether its
/// segment was replaced. A sender that quit and came back made a NEW segment,
/// and a reader still on the old one would call the bus starved forever.
/// Asking is a few system calls, so it is done once a second for a quiet bus.
pub const REATTACH_US: u64 = 1_000_000;

/// What the pumping thread reports about a bus, for the message thread to
/// read whenever it likes.
#[derive(Default)]
pub struct BusStatus {
    /// The sender's rate disagrees with ours, so it is not analysed.
    pub rate_mismatch: AtomicBool,
    /// Zero-filled because its sender has gone quiet.
    pub starved: AtomicBool,
    pub resynced: AtomicBool,
    pub dropped: AtomicU64,
    /// Columns this bus's analyzer had produced when the last pump finished.
    /// See `Engine::publish`.
    pub settled: AtomicUsize,
}

/// One bus, as the pump sees it.
pub struct BusFeed {
    pub slot: u32,
    pub reader: Reader,
    pub tx: Producer,
    pub status: Arc<BusStatus>,
    /*
     * MONO, AND WHAT COULD NOT BE USED YET.
     *
     * A bus hands over whatever happens to be there, and the sources are only
     * in step if every analyzer is given the SAME number of frames -- so the
     * surplus from a bus that ran ahead is kept here until the others catch up.
     * Without it, the fast source would pull ahead a little every tick and the
     * per-cell clash would slowly start comparing two different moments.
     *
     * Twice a pump, so a bus that ran ahead has somewhere to wait rather than
     * being thrown away. Sized when the bus is opened and never resized.
     */
    pub stage: Box<[f32]>,
    pub have: usize,
    /*
     * HOW LONG THIS BUS HAS BEEN SHORT. A Listen-In on a muted track, or one
     * whose host has stopped calling it, publishes nothing -- and the lockstep
     * below would then hold EVERY source still, including the plugin's own
     * track. One idle bus froze the whole picture, which is the worst kind of
     * failure here because the editor goes on saying it is live.
     */
    pub short_us: u64,
    /* Since the reader last delivered anything -- the clock for asking whether
     * its segment was replaced. */
    pub quiet_us: u64,
}

impl BusFeed {
    pub fn new(slot: u32, reader: Reader, tx: Producer, status: Arc<BusStatus>) -> Self {
        Self {
            slot,
            reader,
            tx,
            status,
            stage: vec![0.0; PUMP_FRAMES * 2].into_boxed_slice(),
            have: 0,
            short_us: 0,
            quiet_us: 0,
        }
    }
}

/*
 * A CHANGE OF SOURCES, worked out on the main thread and applied by the
 * pumping thread.
 *
 * The main thread opens the new readers (that allocates and maps memory) and
 * sizes every vector here; the pumping thread only moves buses between them,
 * within their capacity, so applying a plan allocates and frees nothing. The
 * buses it lets go of come back in `retired` and are dropped by the main thread.
 */
pub struct Plan {
    /// The slots to keep or add, in the order the caller asked for them.
    pub order: Vec<u32>,
    pub fresh: Vec<BusFeed>,
    pub retired: Vec<BusFeed>,
    /// Woken once the plan has been applied.
    pub waiter: Option<Thread>,
}

/// Everything `pump` touches.
pub struct Engine {
    pub sample_rate: f32,
    pub own_ring: MonoConsumer,
    pub own: Producer,
    /// The own channel's `BusStatus::settled`.
    pub own_settled: Arc<AtomicUsize>,
    pub buses: Vec<BusFeed>,
    /* Scratch, sized once. `pump` allocates nothing. */
    interleaved: Box<[f32]>,
    own_take: Box<[f32]>,
}

impl Engine {
    pub fn new(
        sample_rate: f32,
        own_ring: MonoConsumer,
        own: Producer,
        own_settled: Arc<AtomicUsize>,
        max_buses: usize,
    ) -> Self {
        Self {
            sample_rate,
            own_ring,
            own,
            own_settled,
            buses: Vec::with_capacity(max_buses),
            interleaved: vec![0.0; PUMP_FRAMES * 2].into_boxed_slice(),
            own_take: vec![0.0; PUMP_FRAMES].into_boxed_slice(),
        }
    }

    /// Adopt a change of sources. Allocates and frees nothing: see `Plan`.
    pub fn apply(&mut self, plan: &mut Plan) {
        let mut i = 0;
        while i < self.buses.len() {
            if plan.order.contains(&self.buses[i].slot) {
                i += 1;
            } else {
                plan.retired.push(self.buses.swap_remove(i));
            }
        }
        self.buses.append(&mut plan.fresh);
        /* Unstable, because it never allocates; the slots are unique anyway. */
        let order = &plan.order;
        self.buses
            .sort_unstable_by_key(|b| order.iter().position(|&s| s == b.slot).unwrap_or(usize::MAX));
        /* The frames in flight belong to the answer that just changed. */
        self.own_ring.clear();
    }

    /// Move audio into every analyzer, in step. `dt_us` is the time since the
    /// previous pump. Returns the frames each source was given. Allocates
    /// nothing, except that a bus quiet for a second is asked to reattach.
    pub fn pump(&mut self, dt_us: u64) -> usize {
        /*
         * THE COMMON FRAME COUNT IS THE WHOLE POINT. Feeding one analyzer more
         * than another is how column k stops being the same moment for both,
         * and a per-cell clash between two moments is not a clash at all -- it
         * is a coincidence drawn in orange.
         *
         * A bus cannot be asked how much it holds without taking it, so each is
         * drained into its own staging buffer first and the SMALLEST of those,
         * with the own channel, decides how much everybody gets. The surplus
         * waits where it is.
         */
        let own_rate = self.sample_rate;
        /* The own channel's count is taken BEFORE the buses are read, so every
         * bus has had at least as long as the own channel to deliver it. */
        let mut n = self.own_ring.available().min(PUMP_FRAMES);
        for b in &mut self.buses {
            let room = (b.stage.len() - b.have).min(PUMP_FRAMES);
            if room == 0 {
                continue;
            }
            let r = b.reader.read(&mut self.interleaved[..room * 2]);
            b.status.dropped.fetch_add(r.dropped, Ordering::Relaxed);
            if r.resynced {
                b.status.resynced.store(true, Ordering::Relaxed);
                /* A restart is when a sender's rate can change, so it is when
                 * the verdict is taken again. */
                let rate = b.reader.sample_rate();
                b.status
                    .rate_mismatch
                    .store(rate != 0 && rate as f32 != own_rate, Ordering::Relaxed);
                b.have = 0;
            }

            if r.frames == 0 {
                b.quiet_us += dt_us;
                if b.quiet_us >= REATTACH_US {
                    b.quiet_us = 0;
                    b.reader.reattach();
                }
            } else {
                b.quiet_us = 0;
            }

            /* READ AND DISCARDED, never analysed. Still read, so the reader
             * stays at the live edge and sees the restart that may bring the
             * rates back into agreement. */
            if b.status.rate_mismatch.load(Ordering::Relaxed) {
                continue;
            }

            /* Stereo interleaved in, mono out -- halved, the same sum the own
             * channel takes, so a centred source does not read 6 dB hot on one
             * picture and correct on the other. */
            let got = r.frames as usize;
            for (m, lr) in b.stage[b.have..b.have + got]
                .iter_mut()
                .zip(self.interleaved.chunks_exact(2))
            {
                *m = 0.5 * (lr[0] + lr[1]);
            }
            b.have += got;
        }

        /*
         * THE OWN CHANNEL SETS THE PACE, and a bus that cannot keep up is given
         * SILENCE rather than being waited for.
         *
         * Taking the minimum across everything was the obvious reading of "feed
         * them equally" and it was wrong in the one case that matters: a
         * Listen-In on a muted track publishes nothing, the minimum is nought,
         * and NO source advances -- the plugin's own picture stops dead because
         * something else went quiet. A frozen picture that still says "live" is
         * the worst way for this to fail.
         *
         * So the own track decides how much everybody gets. A bus that has it
         * keeps step exactly as before; one that does not is zero-filled for
         * the shortfall, which is both true (nothing was published, so nothing
         * was heard) and keeps column k the same moment for every source, which
         * is what the comparison rests on.
         *
         * The grace period exists so ordinary jitter waits rather than punching
         * a hole in its own picture. Only a source that has been empty for
         * GRACE_US is called silent, and `starved` says so out loud.
         */
        for b in &mut self.buses {
            if b.status.rate_mismatch.load(Ordering::Relaxed) {
                /* Not in the picture, so not in the pacing either. */
                continue;
            }
            if b.have >= n {
                /* Keeping up. */
                b.short_us = 0;
                b.status.starved.store(false, Ordering::Relaxed);
                continue;
            }
            if b.short_us < GRACE_US {
                /* Behind, but recently enough that it is probably just jitter:
                 * wait for it. The clock advances whether or not anything is
                 * drawn, which is what lets the grace period end. */
                b.short_us += dt_us;
                n = n.min(b.have);
            } else {
                /* Out of grace. It is not coming; do not let it hold the others. */
                b.status.starved.store(true, Ordering::Relaxed);
            }
        }
        if n == 0 {
            return 0;
        }

        let got = self.own_ring.take(&mut self.own_take[..n]);
        self.own.push(&self.own_take[..got]);

        for b in &mut self.buses {
            if b.status.rate_mismatch.load(Ordering::Relaxed) {
                continue;
            }
            let have = b.have.min(n);
            /* Zero the shortfall in place, so column k of this source is still
             * the same moment as column k of every other. */
            b.stage[have..n].fill(0.0);
            b.tx.push(&b.stage[..n]);
            /* Keep what nobody was ready for, inside the buffer already there. */
            b.stage.copy_within(have..b.have, 0);
            b.have -= have;
        }
        self.publish();
        n
    }

    /*
     * A PUMP'S COLUMNS APPEAR TOGETHER OR NOT AT ALL.
     *
     * The analyzers are fed one after another, so while a pump runs the own
     * channel can hold a column the buses do not have yet. A drain landing
     * there took it from one channel and not the others, and from then on
     * column k of one was paired with k+1 of the other. So the message thread
     * reads each channel only up to the count published here, after every
     * analyzer has been fed -- a count all of them reached in the same pump.
     */
    fn publish(&self) {
        self.own_settled.store(self.own.produced(), Ordering::Release);
        for b in &self.buses {
            b.status.settled.store(b.tx.produced(), Ordering::Release);
        }
    }
}

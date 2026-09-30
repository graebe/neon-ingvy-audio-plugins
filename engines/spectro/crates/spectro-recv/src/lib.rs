/*
 * spectro-recv -- many sources into one picture.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * The Spectrogram analyses the track it sits on. This is the part that lets it
 * also read a Listen-In bus, so a bass and a pad can be looked at in one window
 * instead of two -- and so the places they are fighting can be marked.
 *
 * THREE THREADS, AND EACH OWNS ITS OWN PART.
 *
 *   audio      `OwnFeed::push`: the track's mono sum into a ring (ring.rs).
 *   worker     the pump: drains that ring and every bus, runs the transforms,
 *              pushes finished columns (worker.rs, engine.rs).
 *   message    everything on `Receiver`: choosing sources, draining columns,
 *              summing and comparing them.
 *
 * `bus_core::Reader::read` is documented "one thread, the same one each time",
 * and opening a reader allocates and mmaps -- so the buses cannot be drained
 * from the audio thread. Rather than split the sources across threads and
 * synchronise them, ALL of the analysis happens in one place, the pump, and
 * the own channel arrives there through a ring.
 *
 * Two things fall out of that, both wanted:
 *
 *   ALIGNMENT BY CONSTRUCTION. The pump feeds every analyzer exactly the same
 *   number of frames. Every analyzer therefore shares one rate, one hop and
 *   one phase, so column k of every source is the same moment -- which is what
 *   makes a per-cell clash mean anything. Nothing has to guess at a timestamp.
 *
 *   THE FFTs LEAVE THE AUDIO THREAD. An analyzer that stutters draws a
 *   stuttering picture; one that overruns the audio thread makes a noise. For a
 *   thing whose entire output is a picture, that is the right way round.
 *
 * The pump used to run on the message thread, from the editor's idle timer.
 * `start` moves it to a worker of the receiver's own, so a host's UI never
 * waits behind four 16k-point transforms; without it, `pump` runs the same
 * code on the caller's thread, which is what the deterministic tests do.
 *
 * WHAT IT REFUSES TO DO. A source whose sample rate differs from this
 * receiver's is not analysed at all: a different rate picks a different window
 * (8192 at 48 kHz, 16384 at 96), and therefore a different group delay, so the
 * two pictures would be quietly offset from each other. `rate_mismatch` says so
 * and the editor prints it, which beats drawing an offset nobody can see and
 * nobody ordered.
 *
 * THE AUDIO THREAD'S ONE ENTRY POINT IS A SEPARATE VALUE. `Receiver::new` hands
 * back the receiver and an `OwnFeed`; the feed goes to the audio thread and
 * everything else stays with the receiver. Each works through `&mut self`, and
 * what crosses between the three threads is lock-free rings and atomics.
 */
mod engine;
mod ring;
mod worker;

pub use ring::{mono_ring, MonoConsumer, MonoProducer, RingStats, CAPACITY as RING_FRAMES};
pub use worker::TICK as WORKER_TICK;

use core::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Arc;

use bus_core::{Reader, MAX_SLOT};
use spectro_core::{clash_column, db_span_to_byte, db_to_byte, Analyzer, Config, Consumer, PowerTable};

use engine::{BusFeed, BusStatus, Engine, Plan};
use worker::Worker;

/// Sources a receiver will draw at once, the own channel included.
///
/// Each one past the first is a whole analysis chain -- a 16384-point
/// transform about 47 times a second at 96 kHz. Four is where a picture stops
/// being readable anyway, so the cost and the legibility run out together.
pub const MAX_SOURCES: usize = 4;

/// Channel 0 is always the track the plugin is inserted on.
pub const OWN: usize = 0;

/// The time one `Receiver::pump` stands for when the caller pumps by hand:
/// the editor's idle timer, which is what pumped before the worker existed.
pub const PUMP_INTERVAL_US: u64 = 20_000;

/// What the editor's source list is built from.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SlotInfo {
    pub slot: u32,
    pub live: bool,
    pub sample_rate: u32,
    pub label: String,
}

/// One bus, as the message thread sees it: where its columns come out, and
/// what the pump last said about it.
struct BusView {
    slot: u32,
    rx: Consumer,
    status: Arc<BusStatus>,
}

/// The audio thread's handle: the own track's samples go in here.
pub struct OwnFeed {
    tx: MonoProducer,
}

impl OwnFeed {
    /// **Audio thread.** The track's own mono sum. Copies and returns;
    /// allocates nothing, locks nothing.
    pub fn push(&mut self, mono: &[f32]) {
        self.tx.push(mono);
    }
}

pub struct Receiver {
    cfg: Config,
    own: Consumer,
    own_settled: Arc<AtomicUsize>,
    own_stats: RingStats,
    buses: Vec<BusView>,

    /* Exactly one of these is Some: the engine while the caller pumps, the
     * worker once it pumps instead. */
    engine: Option<Box<Engine>>,
    worker: Option<Worker>,

    clash_floor: u8,
    clash_balance: u8,
    /// The byte scale as power tables, for `sum_into`.
    power: PowerTable,
}

impl Receiver {
    /// A receiver, and the feed its own channel arrives through. The feed is
    /// the audio thread's; the receiver is the message thread's. Nothing is
    /// analysed until `pump` is called or `start` hands pumping to a thread.
    pub fn new(cfg: Config) -> (Self, OwnFeed) {
        /* The analyzer clamps what it was given, so ITS config is the effective
         * one -- taking the caller's would let this crate's idea of `bands`
         * drift from the buffers the analyzer actually produces. */
        let own = Analyzer::new(cfg);
        let cfg = own.config();
        let (own_tx, own_rx) = own.split();
        let (tx, own_ring) = mono_ring();
        let own_stats = own_ring.stats();
        let own_settled = Arc::new(AtomicUsize::new(0));
        let engine = Engine::new(cfg.sample_rate, own_ring, own_tx, own_settled.clone(), MAX_SOURCES - 1);
        let rx = Self {
            cfg,
            own: own_rx,
            own_settled,
            own_stats,
            buses: Vec::with_capacity(MAX_SOURCES - 1),
            engine: Some(Box::new(engine)),
            worker: None,
            /* -60 dB and 12 dB: loud enough to matter, close enough to fight.
             * Both are settable; these are what the picture opens with. */
            clash_floor: db_to_byte(-60.0, cfg.db_floor, cfg.db_ceil),
            clash_balance: db_span_to_byte(12.0, cfg.db_floor, cfg.db_ceil),
            power: PowerTable::new(cfg.db_floor, cfg.db_ceil),
        };
        (rx, OwnFeed { tx })
    }

    /// **Main thread.** Hand pumping to a thread of this receiver's own, so the
    /// transforms never run on the caller's. Returns whether that thread is
    /// running; if it could not be created the caller goes on pumping.
    ///
    /// The thread lives until the receiver is dropped, which stops and joins
    /// it -- so dropping a receiver can wait for up to one pump.
    pub fn start(&mut self) -> bool {
        if let Some(engine) = self.engine.take() {
            match Worker::start(engine) {
                Ok(w) => self.worker = Some(w),
                Err(engine) => self.engine = Some(engine),
            }
        }
        self.worker.is_some()
    }

    fn mismatched(&self, rate: u32) -> bool {
        rate != 0 && rate as f32 != self.cfg.sample_rate
    }

    pub fn bands(&self) -> usize {
        self.cfg.bands
    }

    /// The band centre frequencies every source is measured on.
    ///
    /// One axis, not one per source: they share a configuration, which is
    /// exactly what lets their columns be compared cell by cell.
    pub fn band_hz_into(&self, out: &mut [f32]) -> usize {
        self.own.band_hz_into(out)
    }

    /// Channels currently drawable: the own channel plus each open bus.
    pub fn channels(&self) -> usize {
        1 + self.buses.len()
    }

    /// The slot behind a channel, or `None` for the own channel.
    pub fn slot_of(&self, ch: usize) -> Option<u32> {
        if ch == OWN {
            None
        } else {
            self.buses.get(ch - 1).map(|b| b.slot)
        }
    }

    fn status(&self, ch: usize) -> Option<&BusStatus> {
        if ch == OWN {
            None
        } else {
            self.buses.get(ch - 1).map(|b| &*b.status)
        }
    }

    pub fn rate_mismatch(&self, ch: usize) -> bool {
        self.status(ch).is_some_and(|s| s.rate_mismatch.load(Ordering::Relaxed))
    }

    /// Frames the own channel could not be given because nothing drained it.
    pub fn own_dropped(&self) -> u64 {
        self.own_stats.dropped()
    }

    /// **Main thread.** Choose which buses to listen to. Allocates; opens and
    /// closes readers; never call it near the audio thread. With a worker
    /// running it waits for the worker to adopt the change -- at most a tick
    /// and one pump.
    ///
    /// Slots already open are KEPT rather than reopened, so re-selecting a set
    /// that merely gained a member does not restart the ones that were already
    /// running -- a reopened reader starts at the live edge and would put a
    /// seam in a picture that had no reason to have one.
    pub fn set_sources(&mut self, slots: &[u32]) {
        let mut wanted: Vec<u32> = Vec::with_capacity(MAX_SOURCES - 1);
        for &s in slots {
            if s >= 1 && s <= MAX_SLOT && !wanted.contains(&s) && wanted.len() < MAX_SOURCES - 1 {
                wanted.push(s);
            }
        }

        /*
         * The plan is worked out here, where opening a reader is allowed, and
         * sized so that adopting it allocates nothing: the pumping thread only
         * moves buses between these vectors.
         */
        let mut plan = Box::new(Plan {
            order: Vec::with_capacity(MAX_SOURCES - 1),
            fresh: Vec::with_capacity(MAX_SOURCES - 1),
            retired: Vec::with_capacity(MAX_SOURCES - 1),
            waiter: None,
        });
        let mut views: Vec<BusView> = Vec::with_capacity(MAX_SOURCES - 1);
        for &slot in &wanted {
            if let Some(i) = self.buses.iter().position(|b| b.slot == slot) {
                views.push(self.buses.swap_remove(i));
                plan.order.push(slot);
                continue;
            }
            let Some(reader) = Reader::open(slot) else {
                continue;
            };
            let status = Arc::new(BusStatus::default());
            status.rate_mismatch.store(self.mismatched(reader.sample_rate()), Ordering::Relaxed);
            let (tx, rx) = Analyzer::new(self.cfg).split();
            plan.fresh.push(BusFeed::new(slot, reader, tx, status.clone()));
            views.push(BusView { slot, rx, status });
            plan.order.push(slot);
        }
        /* Whatever is left in `self.buses` was not wanted; its views go now,
         * its feeds come back in `plan.retired`. Both are dropped here. */
        self.buses = views;

        let plan = match (&mut self.engine, &self.worker) {
            (Some(engine), _) => {
                engine.apply(&mut plan);
                plan
            }
            (None, Some(worker)) => worker.apply(plan),
            (None, None) => plan,
        };
        drop(plan);
    }

    /// The range every source is measured over. Allocation-free and safe while
    /// audio runs, exactly as `Analyzer::set_range` is.
    pub fn set_range(&self, f_min: f32, f_max: f32) {
        self.own.set_range(f_min, f_max);
        for b in &self.buses {
            b.rx.set_range(f_min, f_max);
        }
    }

    /// The two numbers that decide what counts as a clash, in dB.
    pub fn set_clash(&mut self, floor_db: f32, balance_db: f32) {
        self.clash_floor = db_to_byte(floor_db, self.cfg.db_floor, self.cfg.db_ceil);
        self.clash_balance = db_span_to_byte(balance_db, self.cfg.db_floor, self.cfg.db_ceil);
    }

    /// Move audio into every analyzer, in step, on the calling thread -- for a
    /// receiver whose worker was not started. Returns the frames each source
    /// was given; always 0 while a worker runs, which pumps on its own.
    /// Allocates nothing.
    pub fn pump(&mut self) -> usize {
        match &mut self.engine {
            Some(engine) => engine.pump(PUMP_INTERVAL_US),
            None => 0,
        }
    }

    /// Whether a channel is being zero-filled because its sender has gone quiet.
    /// The editor says so rather than drawing a black stripe and letting the
    /// reader think the track is silent when the bus is simply absent.
    pub fn starved(&self, ch: usize) -> bool {
        self.status(ch).is_some_and(|s| s.starved.load(Ordering::Relaxed))
    }

    /// Frames a bus lost before this receiver could reach them, and whether its
    /// sender restarted. Both are worth saying out loud: a gap that passes as
    /// silence is a picture lying about the music.
    pub fn bus_dropped(&self, ch: usize) -> u64 {
        if ch == OWN {
            self.own_stats.dropped()
        } else {
            self.status(ch).map_or(0, |s| s.dropped.load(Ordering::Relaxed))
        }
    }

    pub fn bus_resynced(&self, ch: usize) -> bool {
        self.status(ch).is_some_and(|s| s.resynced.load(Ordering::Relaxed))
    }

    /// How many columns every drawn channel has ready: take this many from
    /// each and they are the same moments.
    ///
    /// Counted only up to the last FINISHED pump -- see `Engine::publish` --
    /// so a drain that lands while the worker is between one channel and the
    /// next cannot see a column in one that the other does not have yet.
    ///
    /// Left out: a channel refused for its sample rate, which draws nothing,
    /// and one that has not produced its first column yet -- a bus added a
    /// moment ago is still filling its window, and waiting for it would hold
    /// the whole picture still for that long.
    pub fn ready(&self) -> usize {
        let waiting = |rx: &Consumer, settled: &AtomicUsize| {
            let end = settled.load(Ordering::Acquire);
            (end != 0).then(|| end.wrapping_sub(rx.position()))
        };
        self.buses
            .iter()
            .filter(|b| !b.status.rate_mismatch.load(Ordering::Relaxed))
            .filter_map(|b| waiting(&b.rx, &b.status.settled))
            .chain(waiting(&self.own, &self.own_settled))
            .min()
            .unwrap_or(0)
    }

    /// **Message thread.** Drain one channel's finished columns, as of the
    /// last finished pump. Pass `ready()` as `max_cols` to keep the channels
    /// in step.
    pub fn take_columns(&mut self, ch: usize, out: &mut [u8], max_cols: usize) -> usize {
        match ch {
            OWN => {
                let end = self.own_settled.load(Ordering::Acquire);
                self.own.take_columns_until(out, max_cols, end)
            }
            _ => match self.buses.get_mut(ch - 1) {
                Some(b) if !b.status.rate_mismatch.load(Ordering::Relaxed) => {
                    let end = b.status.settled.load(Ordering::Acquire);
                    b.rx.take_columns_until(out, max_cols, end)
                }
                _ => 0,
            },
        }
    }

    /// Probe every slot, for the editor's source list. **Main thread.**
    ///
    /// Probing creates nothing: walking all sixteen leaves the machine exactly
    /// as it found it.
    pub fn slots() -> Vec<SlotInfo> {
        let mut out = Vec::new();
        for slot in 1..=MAX_SLOT {
            if let Some(i) = bus_core::probe(slot) {
                out.push(SlotInfo {
                    slot,
                    live: i.live,
                    sample_rate: i.sample_rate,
                    label: i.label,
                });
            }
        }
        out
    }

    /// Add several channels' columns into one, in POWER -- see
    /// `spectro_core::PowerTable` for why it cannot be done in byte space.
    ///
    /// Takes columns the caller already drained, for the reason `clash_into`
    /// does: `take_columns` is destructive, so draining again to add would be
    /// adding one source's present to another's future.
    pub fn sum_into(&self, srcs: &[&[u8]], out: &mut [u8]) {
        let bands = self.cfg.bands;
        if bands == 0 || srcs.is_empty() {
            for slot in out.iter_mut() {
                *slot = 0;
            }
            return;
        }
        let cols = srcs
            .iter()
            .map(|s| s.len() / bands)
            .min()
            .unwrap_or(0)
            .min(out.len() / bands);

        /* One column's worth of borrows, reused across the batch. Sized by
         * MAX_SOURCES so nothing is allocated per column. */
        let mut view: [&[u8]; MAX_SOURCES] = [&[]; MAX_SOURCES];
        for c in 0..cols {
            let r = c * bands..c * bands + bands;
            let n = srcs.len().min(MAX_SOURCES);
            for (i, src) in srcs.iter().take(n).enumerate() {
                view[i] = &src[r.clone()];
            }
            self.power.sum_column(&view[..n], &mut out[r]);
        }
        for slot in out.iter_mut().skip(cols * bands) {
            *slot = 0;
        }
    }

    pub fn clash_settings(&self) -> (u8, u8) {
        (self.clash_floor, self.clash_balance)
    }

    /*
     * THE CLASH IS COMPUTED FROM THE COLUMNS THE SHELL ALREADY HAS, not from a
     * second drain. `take_columns` is destructive -- a column handed out is
     * gone -- so a receiver that drained again to compare would be comparing
     * one source's present against another's future.
     *
     * The shell drains each channel once, sends it, and hands the same bytes
     * back here. That also keeps the metric a pure function of two columns,
     * which is why it is tested in spectro-core rather than through a
     * Receiver.
     */

    /// Clash strength for a batch of columns of `a` against `b`, using this
    /// receiver's current floor and balance. All three are `cols * bands`.
    pub fn clash_into(&self, a: &[u8], b: &[u8], out: &mut [u8]) {
        let bands = self.cfg.bands;
        if bands == 0 {
            return;
        }
        let cols = (a.len() / bands).min(b.len() / bands).min(out.len() / bands);
        for c in 0..cols {
            let r = c * bands..c * bands + bands;
            clash_column(
                &a[r.clone()],
                &b[r.clone()],
                &mut out[r],
                self.clash_floor,
                self.clash_balance,
            );
        }
    }
}

/* ------------------------------------------------------------------ wire --
 *
 * The source list, as the editor reads it. Encoded HERE rather than in the C++
 * shell for the reason the rest of this crate exists: it is behaviour, cargo
 * covers it, and the shell stays a thing that only calls.
 */

/// `"<slot>:<live>:<rate>:<label>"` per source, `\n` between.
///
/// A label may hold anything a person typed, so the two characters that would
/// break the framing are dropped rather than escaped -- a name is a name, and a
/// receiver that showed `Bass\n2:1:...` because somebody pressed return would
/// be a worse answer than one that showed `Bass`.
pub fn encode_slots(slots: &[SlotInfo]) -> String {
    let mut out = String::with_capacity(slots.len() * 48);
    for s in slots {
        if !out.is_empty() {
            out.push('\n');
        }
        let label: String = s
            .label
            .chars()
            .filter(|&c| c != '\n' && c != ':' && !c.is_control())
            .collect();
        out.push_str(&format!(
            "{}:{}:{}:{}",
            s.slot,
            u8::from(s.live),
            s.sample_rate,
            label
        ));
    }
    out
}

#[cfg(test)]
mod wire_tests {
    use super::*;

    fn s(slot: u32, live: bool, rate: u32, label: &str) -> SlotInfo {
        SlotInfo { slot, live, sample_rate: rate, label: label.to_string() }
    }

    #[test]
    fn a_source_list_is_one_line_each() {
        let got = encode_slots(&[s(1, true, 48000, "Bass"), s(3, false, 44100, "Pad")]);
        assert_eq!(got, "1:1:48000:Bass\n3:0:44100:Pad");
    }

    #[test]
    fn nothing_is_not_an_error() {
        assert_eq!(encode_slots(&[]), "");
    }

    #[test]
    fn a_label_cannot_break_the_framing() {
        /* A colon would invent a field and a newline a whole extra source. */
        let got = encode_slots(&[s(2, true, 48000, "Ba:ss\nDI\u{7}")]);
        assert_eq!(got, "2:1:48000:BassDI");
    }

    #[test]
    fn a_label_keeps_its_accents() {
        /* The bus carries UTF-8 and truncates on a character boundary; nothing
         * here may undo that by filtering non-ASCII. */
        let got = encode_slots(&[s(4, true, 48000, "Bässe")]);
        assert_eq!(got, "4:1:48000:Bässe");
    }
}

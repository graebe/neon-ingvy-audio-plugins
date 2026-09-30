/*
 * spectro-recv -- many sources into one picture.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * The Spectrogram analyses the track it sits on. This is the part that lets it
 * also read a Listen-In bus, so a bass and a pad can be looked at in one window
 * instead of two -- and so the places they are fighting can be marked.
 *
 * ONE THREAD OWNS EVERYTHING HERE EXCEPT THE PUSH, and that is a decision
 * rather than a convenience.
 *
 * `bus_core::Reader::read` is documented "one thread, the same one each time",
 * and opening a reader allocates and mmaps -- so the buses cannot be drained
 * from the audio thread. Rather than split the sources across two threads and
 * synchronise them, ALL of the analysis happens on the message thread: the own
 * channel arrives through a ring (see ring.rs) and meets the buses there.
 *
 * Two things fall out of that, both wanted:
 *
 *   ALIGNMENT BY CONSTRUCTION. `pump` takes min(available) across every source
 *   and feeds each analyzer exactly that many frames. Every analyzer therefore
 *   shares one rate, one hop and one phase, so column k of every source is the
 *   same moment -- which is what makes a per-cell clash mean anything. Nothing
 *   has to guess at a timestamp, and the hop-phase offset that would otherwise
 *   sit between two independently started analyzers cannot arise.
 *
 *   THE FFTs LEAVE THE AUDIO THREAD. An analyzer that stutters draws a
 *   stuttering picture; one that overruns the audio thread makes a noise. For a
 *   thing whose entire output is a picture, that is the right way round.
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
 * everything else stays on the message thread. Each works through `&mut self`,
 * so the one thing that crosses threads is the ring between them.
 */
mod ring;

pub use ring::{mono_ring, MonoConsumer, MonoProducer, CAPACITY as RING_FRAMES};

use bus_core::{Reader, MAX_SLOT};
use spectro_core::{clash_column, db_span_to_byte, db_to_byte, sum_column, Analyzer, Config};

/// Sources a receiver will draw at once, the own channel included.
///
/// Each one past the first is a whole analysis chain -- an 8192-point transform
/// about 47 times a second, roughly 1.5% of a core. Four is where a picture
/// stops being readable anyway, so the cost and the legibility run out
/// together.
pub const MAX_SOURCES: usize = 4;

/// Channel 0 is always the track the plugin is inserted on.
pub const OWN: usize = 0;

/// What the editor's source list is built from.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SlotInfo {
    pub slot: u32,
    pub live: bool,
    pub sample_rate: u32,
    pub label: String,
}

/// One bus being listened to.
struct Bus {
    slot: u32,
    reader: Reader,
    analyzer: Analyzer,
    /// The sender's rate disagrees with ours, so it is not analysed.
    rate_mismatch: bool,
    dropped: u64,
    resynced: bool,
    /*
     * MONO, AND WHAT COULD NOT BE USED YET.
     *
     * A bus hands over whatever happens to be there, and the sources are only
     * in step if every analyzer is given the SAME number of frames -- so the
     * surplus from a bus that ran ahead is kept here until the others catch up.
     * Without it, the fast source would pull ahead a little every tick and the
     * per-cell clash would slowly start comparing two different moments.
     */
    stage: Vec<f32>,
    have: usize,
    /*
     * HOW LONG THIS BUS HAS HAD NOTHING TO GIVE.
     *
     * A Listen-In on a muted track, or one whose host has stopped calling it,
     * publishes nothing -- and the lockstep below would then hold EVERY source
     * still, including the plugin's own track. One idle bus froze the whole
     * picture, which is the worst kind of failure here because the editor goes
     * on saying it is live.
     */
    short: usize,
    starved: bool,
    /* Pumps since the reader last delivered anything -- the clock for asking
     * whether its segment was replaced. See `REATTACH_PUMPS`. */
    quiet: usize,
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
    own_ring: MonoConsumer,
    own: Analyzer,
    buses: Vec<Bus>,

    /* Scratch, sized once. `pump` allocates nothing. */
    interleaved: Vec<f32>,
    own_take: Vec<f32>,

    clash_floor: u8,
    clash_balance: u8,
}

/// The most frames one `pump` will move per source. Sized so a 50 Hz idle timer
/// keeps up with 96 kHz with room to spare, and so the scratch below is fixed.
const PUMP_FRAMES: usize = 4096;

/// How many pumps in a row a bus may come up short before it is treated as
/// silent rather than waited for. At the editor's ~50 Hz idle timer this is
/// about a fifth of a second: long enough that ordinary jitter -- a bus a block
/// behind on one tick -- waits and catches up, short enough that a muted
/// Listen-In does not stall the picture for anything a person would notice.
///
/// COUNTED IN PUMPS, NOT FRAMES, and that is the fix rather than a detail: a
/// bus with nothing at all contributes no frames to count, so a frame-based
/// clock never advances and the grace period never ends. The first version of
/// this froze exactly as hard as the bug it replaced.
const GRACE_PUMPS: usize = 10;

/// How many pumps in a row a bus may deliver nothing before the receiver asks
/// whether its segment was replaced -- about a second at the idle timer. A
/// sender that quit and came back made a NEW segment, and a reader still on the
/// old one would call the bus starved forever. Asking is a few system calls,
/// so it is done once a second for a quiet bus, never per pump.
const REATTACH_PUMPS: usize = 50;

impl Receiver {
    /// A receiver, and the feed its own channel arrives through. The feed is
    /// the audio thread's; the receiver is the message thread's.
    pub fn new(cfg: Config) -> (Self, OwnFeed) {
        /* The analyzer clamps what it was given, so ITS config is the effective
         * one -- taking the caller's would let this crate's idea of `bands`
         * drift from the buffers the analyzer actually produces. */
        let own = Analyzer::new(cfg);
        let cfg = own.config();
        let (tx, own_ring) = mono_ring();
        let rx = Self {
            own,
            cfg,
            own_ring,
            buses: Vec::with_capacity(MAX_SOURCES - 1),
            interleaved: vec![0.0; PUMP_FRAMES * 2],
            own_take: vec![0.0; PUMP_FRAMES],
            /* -60 dB and 12 dB: loud enough to matter, close enough to fight.
             * Both are settable; these are what the picture opens with. */
            clash_floor: db_to_byte(-60.0, cfg.db_floor, cfg.db_ceil),
            clash_balance: db_span_to_byte(12.0, cfg.db_floor, cfg.db_ceil),
        };
        (rx, OwnFeed { tx })
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

    pub fn rate_mismatch(&self, ch: usize) -> bool {
        if ch == OWN {
            false
        } else {
            self.buses.get(ch - 1).is_some_and(|b| b.rate_mismatch)
        }
    }

    /// Frames the own channel could not be given because nothing drained it.
    pub fn own_dropped(&self) -> u64 {
        self.own_ring.dropped()
    }

    /// **Main thread.** Choose which buses to listen to. Allocates; opens and
    /// closes readers; never call it near the audio thread.
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

        self.buses.retain(|b| wanted.contains(&b.slot));

        for &slot in &wanted {
            if self.buses.iter().any(|b| b.slot == slot) {
                continue;
            }
            let Some(reader) = Reader::open(slot) else {
                continue;
            };
            let rate_mismatch = self.mismatched(reader.sample_rate());
            self.buses.push(Bus {
                slot,
                reader,
                analyzer: Analyzer::new(self.cfg),
                rate_mismatch,
                dropped: 0,
                resynced: false,
                /* Twice a pump, so a bus that ran ahead has somewhere to wait
                 * rather than being thrown away. Sized here, on the main
                 * thread, and never resized after. */
                stage: vec![0.0; PUMP_FRAMES * 2],
                have: 0,
                short: 0,
                starved: false,
                quiet: 0,
            });
        }

        /* The frames in flight belong to the answer that just changed. */
        self.own_ring.clear();
        /* Order the buses the way the caller asked for them, so a channel index
         * means the same thing to both sides. */
        self.buses.sort_by_key(|b| wanted.iter().position(|&w| w == b.slot).unwrap_or(usize::MAX));
    }

    /// The range every source is measured over. Allocation-free and safe while
    /// audio runs, exactly as `Analyzer::set_range` is.
    pub fn set_range(&self, f_min: f32, f_max: f32) {
        self.own.set_range(f_min, f_max);
        for b in &self.buses {
            b.analyzer.set_range(f_min, f_max);
        }
    }

    /// The two numbers that decide what counts as a clash, in dB.
    pub fn set_clash(&mut self, floor_db: f32, balance_db: f32) {
        self.clash_floor = db_to_byte(floor_db, self.cfg.db_floor, self.cfg.db_ceil);
        self.clash_balance = db_span_to_byte(balance_db, self.cfg.db_floor, self.cfg.db_ceil);
    }

    /// **Message thread.** Move audio into every analyzer, in step.
    ///
    /// Returns the frames each source was given. Allocates nothing.
    pub fn pump(&mut self) -> usize {
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
        let own_rate = self.cfg.sample_rate;
        for b in &mut self.buses {
            let room = (b.stage.len() - b.have).min(PUMP_FRAMES);
            if room == 0 {
                continue;
            }
            let r = b.reader.read(&mut self.interleaved[..room * 2]);
            b.dropped += r.dropped;
            b.resynced |= r.resynced;

            /* A restart is when a sender's rate can change, so it is when the
             * verdict is taken again. */
            if r.resynced {
                let rate = b.reader.sample_rate();
                b.rate_mismatch = rate != 0 && rate as f32 != own_rate;
                b.have = 0;
            }

            if r.frames == 0 {
                b.quiet += 1;
                if b.quiet % REATTACH_PUMPS == 0 && b.reader.reattach() {
                    b.quiet = 0;
                }
            } else {
                b.quiet = 0;
            }

            /* READ AND DISCARDED, never analysed. Still read, so the reader
             * stays at the live edge and sees the restart that may bring the
             * rates back into agreement. */
            if b.rate_mismatch {
                continue;
            }

            /* Stereo interleaved in, mono out -- halved, the same sum the own
             * channel takes, so a centred source does not read 6 dB hot on one
             * picture and correct on the other. */
            let got = r.frames as usize;
            for i in 0..got {
                let l = self.interleaved[i * 2];
                let rr = self.interleaved[i * 2 + 1];
                b.stage[b.have + i] = 0.5 * (l + rr);
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
         * The grace period exists so ordinary jitter -- a bus a block behind on
         * one tick -- waits rather than punching a hole in its own picture.
         * Only a source that has been empty for a fifth of a second is called
         * silent, and `starved` says so out loud.
         */
        let mut n = self.own_ring.available().min(PUMP_FRAMES);
        for b in &mut self.buses {
            if b.rate_mismatch {
                /* Not in the picture, so not in the pacing either. */
                continue;
            }
            if b.have >= n {
                /* Keeping up. */
                b.short = 0;
                b.starved = false;
                continue;
            }
            if b.short < GRACE_PUMPS {
                /* Behind, but recently enough that it is probably just jitter:
                 * wait for it this tick. The counter advances whether or not
                 * anything is drawn, which is what lets the grace period end. */
                b.short += 1;
                n = n.min(b.have);
            } else {
                /* Out of grace. It is not coming; do not let it hold the others. */
                b.starved = true;
            }
        }
        if n == 0 {
            return 0;
        }

        let got = self.own_ring.take(&mut self.own_take[..n]);
        self.own.push(&self.own_take[..got]);

        for b in &mut self.buses {
            if b.rate_mismatch {
                continue;
            }
            let have = b.have.min(n);
            if have < n {
                /* Zero the shortfall in place -- the staging buffer is already
                 * this long and nothing is allocated. Filled rather than
                 * skipped, so column k of this source is still the same moment
                 * as column k of every other. */
                for slot in b.stage[have..n].iter_mut() {
                    *slot = 0.0;
                }
            }
            b.analyzer.push(&b.stage[..n]);
            /* Keep what nobody was ready for. copy_within moves inside the
             * buffer that is already there; nothing is allocated. */
            b.stage.copy_within(have..b.have, 0);
            b.have -= have;
        }
        n
    }

    /// Whether a channel is being zero-filled because its sender has gone quiet.
    /// The editor says so rather than drawing a black stripe and letting the
    /// reader think the track is silent when the bus is simply absent.
    pub fn starved(&self, ch: usize) -> bool {
        ch != OWN && self.buses.get(ch - 1).is_some_and(|b| b.starved)
    }

    /// Frames a bus lost before this receiver could reach them, and whether its
    /// sender restarted. Both are worth saying out loud: a gap that passes as
    /// silence is a picture lying about the music.
    pub fn bus_dropped(&self, ch: usize) -> u64 {
        if ch == OWN {
            self.own_ring.dropped()
        } else {
            self.buses.get(ch - 1).map_or(0, |b| b.dropped)
        }
    }

    pub fn bus_resynced(&self, ch: usize) -> bool {
        ch != OWN && self.buses.get(ch - 1).is_some_and(|b| b.resynced)
    }

    /// **Message thread.** Drain one channel's finished columns.
    pub fn take_columns(&mut self, ch: usize, out: &mut [u8], max_cols: usize) -> usize {
        match ch {
            OWN => self.own.take_columns(out, max_cols),
            _ => match self.buses.get_mut(ch - 1) {
                Some(b) if !b.rate_mismatch => b.analyzer.take_columns(out, max_cols),
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
    /// `spectro_core::sum_column` for why it cannot be done in byte space.
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
            sum_column(&view[..n], &mut out[r], self.cfg.db_floor, self.cfg.db_ceil);
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

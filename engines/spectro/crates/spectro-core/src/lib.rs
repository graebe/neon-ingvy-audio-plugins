// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * spectro-core -- a short-time Fourier analyzer that hands finished
 * spectrogram columns to a UI.
 *
 * TWO HALVES, ONE PER THREAD. The `Producer` is fed samples and runs one
 * transform per hop, on whichever thread feeds it; the finished column -- one
 * byte per band -- goes into a wait-free single-producer/single-consumer ring
 * (rtrb) that the `Consumer` drains. Through spectro_capi's `spectro_push_f32`
 * the producing thread is the audio thread, and the comments below call it
 * that. In the Spectrogram it is not: spectro-recv feeds every analyzer from a
 * worker thread of its own, so several sources stay in step and the host's
 * audio and UI threads run no transforms at all.
 *
 * The cost is bounded and constant: ONE transform per hop, and the hop is a
 * fixed number of samples -- at 96 kHz a 16384-point real FFT every 21 ms,
 * about 26 us on an M1 Pro (fft.rs's `fft_timings`).
 *
 * NOTHING HERE ALLOCATES AFTER `configure`. `push` and `take_columns` touch
 * preallocated buffers and a handful of atomics, and tests/no_alloc.rs fails
 * the build if that stops being true.
 */

mod bands;
mod fft;
mod window;

#[cfg(test)]
mod reference;

pub use bands::{
    amplitude_to_byte, byte_to_db, centres_for, clash_cell, clash_column, db_span_to_byte,
    db_to_byte, power_to_byte, Band, Bands, PowerTable,
};

use core::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Arc;

use atomic_float::AtomicF32;

use fft::Fft;
use window::Window;

/// How many columns the ring holds: ~5 s at the default hop, which is far more
/// than the one a 60 Hz OnIdle leaves behind. It matters only when the host
/// stops calling OnIdle at all, and then dropping is the right answer.
pub const COLUMN_CAPACITY: usize = 256;

/*
 * THE TWO NUMBERS THE WINDOW LENGTH IS ACTUALLY ABOUT, and they are here rather
 * than in the plugin because they are decisions about SOUND, not about a host.
 *
 * A spectrogram that starts at 10 Hz needs bins finer than 10 Hz, and an FFT's
 * bins are sample_rate / fft_size apart -- so the window length is not a
 * preference, it is arithmetic the sample rate settles. At 48 kHz, 6 Hz bins
 * means 8192 points; at 96 kHz the same 6 Hz means 16384.
 *
 * The cap is 16384. Past 96 kHz the bins widen again rather than the window
 * growing without limit: a 32768-point transform per source for a picture is
 * not a trade worth making, and 192 kHz sessions are rare enough
 * that 11.7 Hz bins there is the right compromise.
 */
pub const TARGET_BIN_HZ: f32 = 6.0;
pub const MAX_FFT_SIZE: usize = 16384;

/*
 * COLUMNS A SECOND, HELD CONSTANT ACROSS SAMPLE RATES.
 *
 * The hop is derived from this rather than from the window, so the picture
 * scrolls at the same speed and holds the same THIRTEEN SECONDS whether the
 * session runs at 44.1 kHz or at 96 kHz. Tie the hop to the window instead and
 * the same plugin shows half as much time in a 96 kHz session, which is the
 * sort of difference nobody attributes to the sample rate.
 */
pub const TARGET_COLUMNS_PER_S: f32 = 47.0;

/// The window that resolves [`TARGET_BIN_HZ`] at this rate, as a power of two,
/// clamped to [`MAX_FFT_SIZE`]. 8192 at 44.1/48 kHz, 16384 at 88.2/96 kHz.
pub fn pick_fft_size(sample_rate: f32) -> usize {
    if !sample_rate.is_finite() || sample_rate <= 0.0 {
        return 8192;
    }
    let wanted = (sample_rate / TARGET_BIN_HZ).ceil().max(1.0) as usize;
    wanted.next_power_of_two().clamp(1024, MAX_FFT_SIZE)
}

/// The hop that yields [`TARGET_COLUMNS_PER_S`] at this rate, as a power of
/// two, never longer than the window.
pub fn pick_hop(sample_rate: f32, fft_size: usize) -> usize {
    if !sample_rate.is_finite() || sample_rate <= 0.0 {
        return 1024;
    }
    let wanted = (sample_rate / TARGET_COLUMNS_PER_S).round().max(1.0) as usize;
    /*
     * THE NEAREST POWER OF TWO, not the next one either way. 44.1 kHz wants a
     * hop of 938: rounding up to 1024 scrolls at 43 columns a second, rounding
     * DOWN to 512 scrolls at 86 -- nearly twice the target, and half the history
     * on screen. Both neighbours are legal and one of them is wrong by a factor
     * of two, which is exactly the case a round-in-one-direction rule gets
     * wrong.
     */
    let up = wanted.next_power_of_two();
    let down = if up > wanted { up / 2 } else { up };
    let near = if wanted - down <= up - wanted { down } else { up };
    near.clamp(64, fft_size.max(64))
}

#[derive(Clone, Copy, Debug)]
pub struct Config {
    pub sample_rate: f32,
    pub fft_size: usize,
    pub hop: usize,
    pub bands: usize,
    pub f_min: f32,
    pub f_max: f32,
    pub db_floor: f32,
    pub db_ceil: f32,
}

impl Default for Config {
    fn default() -> Self {
        Self {
            sample_rate: 48_000.0,
            /*
             * 8192 at 48 kHz is 171 ms of window and bins 5.9 Hz apart, and the
             * second number is why: A SPECTROGRAM THAT STARTS AT 10 Hz NEEDS
             * BINS FINER THAN 10 Hz. At the 1024 this used to be, the bins were
             * 46.9 Hz apart and `Bands` clamped the bottom of the axis up to
             * meet them -- the picture began at 47 Hz however low f_min was set,
             * and said so in the editor's hint bar without anyone reading it as
             * a fault.
             *
             * It is paid for in TIME resolution: a 171 ms window smears a kick
             * transient across a sixth of a second. That is the trade a long
             * window is, not a defect -- the way out is a multi-resolution
             * analysis, not a shorter window.
             */
            fft_size: 8192,
            /* An eighth of the window: ~47 columns a second, which fills a
             * 606-column view in thirteen seconds -- a whole phrase at once. */
            hop: 1024,
            /*
             * 256 bands over 10 Hz .. 20 kHz is about 23 to the octave, two per
             * semitone -- and one per pixel in the editor's 256 px well, so the
             * picture is drawn with no vertical resampling at all.
             */
            bands: 256,
            f_min: 10.0,
            f_max: 20_000.0,
            /* -96 dB is 16-bit silence: a floor deeper than that draws dither
             * and room noise as a permanent violet haze. */
            db_floor: -96.0,
            db_ceil: 0.0,
        }
    }
}

/// The finest hop a window is allowed: `fft_size / MIN_HOP_DIVISOR`, 97%
/// overlap. Every hop `pick_hop` chooses is far coarser.
pub const MIN_HOP_DIVISOR: usize = 32;

impl Config {
    /// Clamp everything into a range the analyzer can actually honour, rather
    /// than trusting a caller across a C ABI. `fft_size` is rounded DOWN to a
    /// power of two.
    fn sanitised(mut self) -> Self {
        if !self.sample_rate.is_finite() || self.sample_rate < 8_000.0 {
            self.sample_rate = 48_000.0;
        }
        self.fft_size = self.fft_size.clamp(64, MAX_FFT_SIZE);
        if !self.fft_size.is_power_of_two() {
            self.fft_size = self.fft_size.next_power_of_two() / 2;
        }
        /* No finer than a thirty-second of the window. A hop of 1 is one
         * transform per SAMPLE on the audio thread -- far past what any machine
         * sustains -- for columns 32 times denser than the window resolves.
         * fft_size is a power of two >= 64 here, so this is a whole number. */
        self.hop = self.hop.clamp(self.fft_size / MIN_HOP_DIVISOR, self.fft_size);
        self.bands = self.bands.clamp(1, 1024);
        if !self.f_min.is_finite() || self.f_min < 1.0 {
            self.f_min = 10.0;
        }
        if !self.f_max.is_finite() || self.f_max <= self.f_min * 2.0 {
            self.f_max = (self.f_min * 2.0).max(20_000.0);
        }
        if !self.db_floor.is_finite() || !self.db_ceil.is_finite() || self.db_ceil <= self.db_floor {
            self.db_floor = -96.0;
            self.db_ceil = 0.0;
        }
        self
    }
}

/* ------------------------------------------------------------------ columns */

/*
 * THE HANDOVER: a bounded rtrb ring of byte columns.
 *
 * rtrb is a wait-free single-producer/single-consumer ring. Its two ends are
 * separate values -- the producing one lives in `Producer`, the draining one in
 * `Consumer` -- and its buffer is allocated once, when the ring is made. A
 * column crosses as ONE RECORD (see `STAMP`), written and read whole, so
 * neither side ever sees half of one.
 *
 * FULL MEANS DROP THE NEW COLUMN, and the count is kept rather than hidden.
 * rtrb refuses a record it has no room for rather than overwriting the oldest,
 * which is the rule this ring has always had: overwriting would need the
 * writer to move the reader's position, exactly the kind of "small" shared
 * write that makes a lock-free queue subtly wrong.
 */

/*
 * THE RANGE EACH COLUMN WAS MEASURED AGAINST, stamped in front of it.
 *
 * Changing the range is a request the audio thread picks up at its next
 * frame, so for up to one hop -- 21 ms -- it is still producing columns on the
 * OLD axis while the editor has already redrawn its scale for the new one.
 * Those columns are not wrong, they are answers to a different question, and
 * drawing them under the new scale puts a stripe of mislabelled data at the
 * very moment the user is looking to see what changed.
 *
 * So every record opens with the epoch its column was measured under, and
 * `take` drops the stale ones. It is a fault with no symptom until you are the
 * one reading the picture.
 */
const STAMP: usize = core::mem::size_of::<u64>();

/// A ring for `bands`-byte columns, as its two ends.
fn columns(bands: usize) -> (ColumnTx, ColumnRx) {
    let (ring_tx, ring_rx) = rtrb::RingBuffer::new(COLUMN_CAPACITY * (STAMP + bands));
    (
        ColumnTx { ring: ring_tx, bands, pushed: 0 },
        ColumnRx { ring: ring_rx, bands, taken: 0 },
    )
}

/// The producing end, owned by `Producer`.
struct ColumnTx {
    ring: rtrb::Producer<u8>,
    bands: usize,
    /// Columns queued so far: a ring position, dropped ones not counted.
    pushed: usize,
}

impl ColumnTx {
    /// Queue a column measured under `epoch`. False when the ring was full and
    /// the column was dropped instead.
    fn push(&mut self, col: &[u8], epoch: usize) -> bool {
        debug_assert_eq!(col.len(), self.bands);
        let Ok(record) = self.ring.write_chunk_uninit(STAMP + col.len()) else {
            return false;
        };
        let stamp = (epoch as u64).to_ne_bytes();
        record.fill_from_iter(stamp.into_iter().chain(col.iter().copied()));
        self.pushed = self.pushed.wrapping_add(1);
        true
    }
}

/// The draining end, owned by `Consumer`.
struct ColumnRx {
    ring: rtrb::Consumer<u8>,
    bands: usize,
    /// Columns read past so far, kept or discarded: the position `Producer`'s
    /// `pushed` counts towards.
    taken: usize,
}

impl ColumnRx {
    /// Returns the columns written to `out`, which must hold `max_cols * bands`
    /// bytes, reading no further than ring position `end` when one is given.
    /// Columns measured against an earlier range are consumed and discarded
    /// rather than returned.
    fn take(&mut self, out: &mut [u8], max_cols: usize, epoch: usize, end: Option<usize>) -> usize {
        let record = STAMP + self.bands;
        let mut available = self.ring.slots() / record;
        if let Some(end) = end {
            available = available.min(end.wrapping_sub(self.taken));
        }
        let available = available.min(out.len() / self.bands.max(1));

        let mut kept = 0;
        let mut seen = 0;
        while seen < available && kept < max_cols {
            /* Counted above, and the producer only ever adds. */
            let Ok(chunk) = self.ring.read_chunk(record) else {
                break;
            };
            seen += 1;
            /* rtrb hands a chunk out as two slices, the second one non-empty
             * only where the chunk wraps past the end of the buffer. No record
             * does in a ring of whole records, and read in order the pair
             * would be right even if one did. */
            let (first, second) = chunk.as_slices();
            let mut bytes = first.iter().chain(second).copied();
            let stamp: [u8; STAMP] = core::array::from_fn(|_| bytes.next().unwrap_or(0));
            if u64::from_ne_bytes(stamp) == epoch as u64 {
                let slot = &mut out[kept * self.bands..(kept + 1) * self.bands];
                for (to, from) in slot.iter_mut().zip(bytes) {
                    *to = from;
                }
                kept += 1;
            }
            /* Kept or another range's answer, it is read now -- see `STAMP`. */
            chunk.commit_all();
        }
        self.taken = self.taken.wrapping_add(seen);
        kept
    }
}

/* ---------------------------------------------------------------------- dsp */

/// Audio-thread state. Everything in it is sized in `Analyzer::new`.
struct Dsp {
    fft: Fft,
    window: Window,
    bands: Bands,
    /// The last `fft_size` samples, oldest at `pos`.
    ring: Box<[f32]>,
    pos: usize,
    since_hop: usize,
    /// Frames are not emitted until the ring has been filled once, so the first
    /// column is a window of audio rather than a window of startup zeroes.
    primed: usize,
    /// The windowed frame, oldest sample first.
    frame: Box<[f32]>,
    /// Bins 0..=fft_size/2.
    re: Box<[f32]>,
    im: Box<[f32]>,
    /// |bin|^2, scaled so a full-scale sine is 1.0.
    power: Box<[f32]>,
    col: Box<[u8]>,
    /// The range epoch this band table was built for.
    epoch: usize,
}

/// What the two halves share besides the ring: the configuration, the range
/// request, and how many columns the ring had to drop.
struct Shared {
    cfg: Config,
    /*
     * THE RANGE, AS A REQUEST RATHER THAN AS SHARED STATE.
     *
     * The band table lives on the audio thread and is rebuilt BY the audio
     * thread; all that crosses is these three atomics. That is what lets a
     * dropdown change the picture's frequency range while audio is running with
     * no lock, no reallocation and no pointer swap -- and why `set_range` is
     * safe where `configure` is not.
     */
    req_f_min: AtomicF32,
    req_f_max: AtomicF32,
    /// Bumped on every range change. Stamped onto each column, so the consumer
    /// can tell an answer to the old question from an answer to the new one.
    epoch: AtomicUsize,
    /// Columns the ring had no room for: counted by the producer, read by the
    /// consumer.
    dropped: AtomicUsize,
}

impl Shared {
    fn range(&self) -> (f32, f32) {
        (self.req_f_min.load(Ordering::Relaxed), self.req_f_max.load(Ordering::Relaxed))
    }
}

/*
 * TWO HALVES, BECAUSE THERE ARE TWO THREADS.
 *
 * The audio thread owns the `Producer` and the message thread the `Consumer`.
 * Neither is Clone, each does its work through `&mut self`, and the ring ends
 * inside them are Send but not Sync -- so safe code cannot push from two
 * threads, or drain from two, however the halves are passed around. That is
 * the whole thread contract, stated to the compiler rather than in a comment.
 */

/// The audio thread's half: feed it samples.
pub struct Producer {
    dsp: Dsp,
    cols: ColumnTx,
    shared: Arc<Shared>,
}

/// The message thread's half: drain columns, change the range, read the axis.
pub struct Consumer {
    cols: ColumnRx,
    shared: Arc<Shared>,
}

/// Both halves in one value, for a caller that feeds and drains on one thread
/// -- spectro-recv's analyzers, and the tests. `split` hands them to two.
pub struct Analyzer {
    tx: Producer,
    rx: Consumer,
}

impl Analyzer {
    pub fn new(cfg: Config) -> Self {
        let cfg = cfg.sanitised();
        let n = cfg.fft_size;
        let n_bins = n / 2 + 1;
        let bands = Bands::new(cfg.bands, n_bins, cfg.sample_rate, cfg.f_min, cfg.f_max);

        let shared = Arc::new(Shared {
            cfg,
            req_f_min: AtomicF32::new(cfg.f_min),
            req_f_max: AtomicF32::new(cfg.f_max),
            epoch: AtomicUsize::new(0),
            dropped: AtomicUsize::new(0),
        });
        let (cols_tx, cols_rx) = columns(cfg.bands);
        Self {
            tx: Producer {
                dsp: Dsp {
                    fft: Fft::new(n),
                    window: Window::hann(n),
                    bands,
                    ring: vec![0.0; n].into_boxed_slice(),
                    pos: 0,
                    since_hop: 0,
                    primed: 0,
                    frame: vec![0.0; n].into_boxed_slice(),
                    re: vec![0.0; n / 2 + 1].into_boxed_slice(),
                    im: vec![0.0; n / 2 + 1].into_boxed_slice(),
                    power: vec![0.0; n / 2 + 1].into_boxed_slice(),
                    col: vec![0u8; cfg.bands].into_boxed_slice(),
                    epoch: 0,
                },
                cols: cols_tx,
                shared: shared.clone(),
            },
            rx: Consumer { cols: cols_rx, shared },
        }
    }

    /// The two halves, for two threads.
    pub fn split(self) -> (Producer, Consumer) {
        (self.tx, self.rx)
    }

    pub fn config(&self) -> Config {
        self.rx.config()
    }

    pub fn bands(&self) -> usize {
        self.rx.bands()
    }

    pub fn range(&self) -> (f32, f32) {
        self.rx.range()
    }

    pub fn set_range(&self, f_min: f32, f_max: f32) {
        self.rx.set_range(f_min, f_max)
    }

    pub fn band_hz_into(&self, out: &mut [f32]) -> usize {
        self.rx.band_hz_into(out)
    }

    pub fn dropped(&self) -> usize {
        self.rx.dropped()
    }

    pub fn push(&mut self, mono: &[f32]) {
        self.tx.push(mono)
    }

    pub fn take_columns(&mut self, out: &mut [u8], max_cols: usize) -> usize {
        self.rx.take_columns(out, max_cols)
    }
}

impl Consumer {
    pub fn config(&self) -> Config {
        self.shared.cfg
    }

    pub fn bands(&self) -> usize {
        self.shared.cfg.bands
    }

    /// The frequency range the picture currently covers, as requested.
    pub fn range(&self) -> (f32, f32) {
        self.shared.range()
    }

    /// Change the frequency range the bands are spread over. **Message thread.**
    ///
    /// Unlike `Analyzer::new`, this allocates nothing and is safe to call while
    /// audio is running: it stores a request, and the audio thread rebuilds its
    /// own band table at the next frame. Columns already queued from before the
    /// change are dropped rather than handed out -- see `STAMP`.
    pub fn set_range(&self, f_min: f32, f_max: f32) {
        if !f_min.is_finite() || !f_max.is_finite() || f_min < 1.0 || f_max <= f_min * 1.5 {
            return; /* a range that cannot be drawn is not a range */
        }
        let s = &self.shared;
        s.req_f_min.store(f_min, Ordering::Relaxed);
        s.req_f_max.store(f_max, Ordering::Relaxed);
        /* Release: the two stores above must be visible to the audio thread
         * before the epoch that tells it to read them. */
        s.epoch.fetch_add(1, Ordering::Release);
    }

    /// Band centre frequencies in Hz, ascending, written into `out`. Returns how
    /// many were written. **Derived from the requested range, never read from
    /// the audio thread's table** -- see `bands::centres_for`.
    pub fn band_hz_into(&self, out: &mut [f32]) -> usize {
        let (f_min, f_max) = self.range();
        let cfg = &self.shared.cfg;
        centres_for(out, cfg.bands, cfg.fft_size / 2 + 1, cfg.sample_rate, f_min, f_max)
    }

    /// Columns the ring had to throw away because nothing drained it.
    pub fn dropped(&self) -> usize {
        self.shared.dropped.load(Ordering::Relaxed)
    }

    /// Ring positions this consumer has read past -- the same count as
    /// `Producer::produced`, so the difference is what is waiting.
    pub fn position(&self) -> usize {
        self.cols.taken
    }

    /// Drain finished columns into `out`, `bands()` bytes each, oldest first.
    /// **Message thread.** Returns the number of columns written.
    pub fn take_columns(&mut self, out: &mut [u8], max_cols: usize) -> usize {
        let epoch = self.shared.epoch.load(Ordering::Acquire);
        self.cols.take(out, max_cols, epoch, None)
    }

    /// `take_columns`, reading no column past `end` -- a value `produced`
    /// returned. What lets several analyzers be drained as of one instant of
    /// their producer's, rather than of whatever each has reached by now.
    pub fn take_columns_until(&mut self, out: &mut [u8], max_cols: usize, end: usize) -> usize {
        let epoch = self.shared.epoch.load(Ordering::Acquire);
        self.cols.take(out, max_cols, epoch, Some(end))
    }
}

impl Producer {
    /// Columns pushed so far (a ring position; dropped ones are not counted).
    /// **The producing thread.**
    pub fn produced(&self) -> usize {
        self.cols.pushed
    }

    /// Feed mono samples. **Audio thread.** Allocates nothing, locks nothing,
    /// and takes a bounded amount of time per sample.
    pub fn push(&mut self, mono: &[f32]) {
        let hop = self.shared.cfg.hop;
        let n = self.dsp.ring.len();

        for &s in mono {
            let dsp = &mut self.dsp;
            /* A NaN in the ring would poison every frame it appears in for the
             * next fft_size samples, not just its own column. */
            dsp.ring[dsp.pos] = if s.is_finite() { s } else { 0.0 };
            dsp.pos = if dsp.pos + 1 == n { 0 } else { dsp.pos + 1 };
            if dsp.primed < n {
                dsp.primed += 1;
            }
            dsp.since_hop += 1;
            if dsp.since_hop >= hop {
                dsp.since_hop = 0;
                if dsp.primed >= n {
                    self.frame();
                }
            }
        }
    }

    /// One transform, one column. Audio thread, from `push` only.
    fn frame(&mut self) {
        let shared = &*self.shared;
        let cfg = &shared.cfg;
        let dsp = &mut self.dsp;
        let n = dsp.ring.len();

        /*
         * THE RANGE CHANGE IS ADOPTED HERE, by the thread that owns the table.
         * Acquire pairs with the Release in `set_range`, so the two frequencies
         * are visible before the epoch that announces them.
         */
        let epoch = shared.epoch.load(Ordering::Acquire);
        if epoch != dsp.epoch {
            dsp.epoch = epoch;
            let (f_min, f_max) = shared.range();
            dsp.bands.rebuild(cfg.bands, n / 2 + 1, cfg.sample_rate, f_min, f_max);
        }

        /* Oldest sample first: the ring's write cursor is also its start, which
         * is the whole reason a ring needs no memmove. */
        let (newer, older) = dsp.ring.split_at(dsp.pos);
        let (head, tail) = dsp.frame.split_at_mut(older.len());
        let (g_head, g_tail) = dsp.window.gain.split_at(older.len());
        for ((f, &s), &g) in head.iter_mut().zip(older).zip(g_head) {
            *f = s * g;
        }
        for ((f, &s), &g) in tail.iter_mut().zip(newer).zip(g_tail) {
            *f = s * g;
        }

        dsp.fft.forward(&dsp.frame, &mut dsp.re, &mut dsp.im);

        /*
         * POWER, SCALED TO AMPLITUDE SQUARED: |X|^2 * scale^2, so a full-scale
         * sine reads 0 dB. Bands are reduced in power and a square root is
         * never taken -- 10*log10(p) is the same decibel as 20*log10(sqrt p).
         */
        let gain = dsp.window.amplitude_scale * dsp.window.amplitude_scale;
        for ((p, &r), &i) in dsp.power.iter_mut().zip(dsp.re.iter()).zip(dsp.im.iter()) {
            *p = (r * r + i * i) * gain;
        }
        /* Every bin but DC and Nyquist has a conjugate twin, and the amplitude
         * scale counts both. Nyquist has none, so it would read 6 dB hot -- a
         * permanent bright line along the top of the picture. */
        if let Some(p) = dsp.power.last_mut() {
            *p *= 0.25;
        }

        let power = &dsp.power;
        /* -400 dB is the old 1e-20 amplitude floor: a silent neighbour pulls an
         * interpolation towards the floor rather than producing a NaN. */
        let db = |k: usize| (10.0 * power[k].log10()).max(-400.0);
        for (out, range) in dsp.col.iter_mut().zip(dsp.bands.ranges.iter()) {
            *out = match range.frac {
                /* PEAK over the band's bins -- see bands.rs. */
                None => {
                    let peak = power[range.lo..range.hi].iter().fold(0.0f32, |a, &p| a.max(p));
                    power_to_byte(peak, cfg.db_floor, cfg.db_ceil)
                }
                /*
                 * NARROWER THAN A BIN: interpolated between the two either
                 * side along a straight line in dB, which is the axis the
                 * picture draws. Snapping to the nearer bin instead gave
                 * consecutive bands identical bytes and stacked the bottom of
                 * the picture into a staircase.
                 */
                Some(x) => {
                    let t = (x - range.lo as f32).clamp(0.0, 1.0);
                    let (lo, hi) = (db(range.lo), db(range.lo + 1));
                    db_to_byte(lo + (hi - lo) * t, cfg.db_floor, cfg.db_ceil)
                }
            };
        }

        if !self.cols.push(&dsp.col, dsp.epoch) {
            shared.dropped.fetch_add(1, Ordering::Relaxed);
        }
    }
}

/// The axis as a Vec, for tests only. The real API writes into a caller's
/// buffer because the C ABI does, and because a spectrogram's axis is asked for
/// once a range change rather than once a frame.
#[cfg(test)]
impl Analyzer {
    fn band_hz(&self) -> Vec<f32> {
        let mut v = vec![0.0f32; self.bands()];
        let n = self.band_hz_into(&mut v);
        v.truncate(n);
        v
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const SR: f32 = 48_000.0;

    fn sine(freq: f32, n: usize, amp: f32) -> Vec<f32> {
        (0..n)
            .map(|i| {
                (amp as f64
                    * (2.0 * core::f64::consts::PI * freq as f64 * i as f64 / SR as f64).sin())
                    as f32
            })
            .collect()
    }

    /// The band whose centre is nearest `hz`.
    fn nearest_band(a: &Analyzer, hz: f32) -> usize {
        let mut best = 0;
        for (i, &c) in a.band_hz().iter().enumerate() {
            if (c - hz).abs() < (a.band_hz()[best] - hz).abs() {
                best = i;
            }
        }
        best
    }

    #[test]
    fn a_1k_sine_lights_the_1k_band_and_leaves_the_rest_dark() {
        let mut a = Analyzer::new(Config { sample_rate: SR, ..Default::default() });
        a.push(&sine(1000.0, 8192, 1.0));

        let mut out = vec![0u8; a.bands() * 16];
        let cols = a.take_columns(&mut out, 16);
        assert!(cols > 0, "no column came out of 8192 samples");

        /* The last full column, so the window is entirely inside the tone. */
        let col = &out[(cols - 1) * a.bands()..cols * a.bands()];
        let peak_band = col.iter().enumerate().max_by_key(|(_, &v)| v).map(|(i, _)| i).unwrap();
        let want = nearest_band(&a, 1000.0);
        assert!(
            (peak_band as i32 - want as i32).abs() <= 1,
            "peak in band {peak_band} ({} Hz), expected {want} ({} Hz)",
            a.band_hz()[peak_band], a.band_hz()[want]
        );

        /* Full scale reaches the ceiling. */
        assert!(col[peak_band] > 250, "a full-scale sine read {}", col[peak_band]);

        /* And it is a LINE, not a smear: three bands away is already 20 dB
         * down, which on a -96..0 ramp is 53 bytes. */
        let span = 255.0 / 96.0; /* bytes per dB */
        for (b, &v) in col.iter().enumerate() {
            if (b as i32 - peak_band as i32).abs() >= 3 {
                assert!(
                    f32::from(v) < 255.0 - 20.0 * span,
                    "band {b} ({} Hz) read {v}: the tone smeared", a.band_hz()[b]
                );
            }
        }
    }

    #[test]
    fn the_picked_window_resolves_ten_hertz_at_every_ordinary_rate() {
        /*
         * THE TEST THE OLD DEFAULTS WOULD HAVE FAILED, and nothing caught it:
         * at 1024 points the bins were 46.9 Hz apart, `Bands` clamped the bottom
         * of the axis up to meet them, and the picture began at 47 Hz however
         * low f_min was set.
         */
        for sr in [44_100.0f32, 48_000.0, 88_200.0, 96_000.0] {
            let n = pick_fft_size(sr);
            let bin_hz = sr / n as f32;
            assert!(n.is_power_of_two(), "{sr} Hz picked {n}, not a power of two");
            assert!(n <= MAX_FFT_SIZE);
            assert!(bin_hz <= TARGET_BIN_HZ, "{sr} Hz: bins {bin_hz} apart");
            /* And the whole point of that: a 10 Hz band edge survives. */
            let a = Analyzer::new(Config {
                sample_rate: sr, fft_size: n, hop: pick_hop(sr, n), ..Default::default()
            });
            let first = a.band_hz()[0];
            assert!(
                (first - 10.0).abs() < 1.0,
                "{sr} Hz: the axis starts at {first}, not 10"
            );
        }

        /* Past the cap the bins widen rather than the window growing without
         * limit -- stated here so the compromise is visible rather than
         * discovered. */
        assert_eq!(pick_fft_size(192_000.0), MAX_FFT_SIZE);
    }

    #[test]
    fn the_picture_scrolls_at_one_speed_whatever_the_sample_rate() {
        /* Tie the hop to the window instead of to a column rate and a 96 kHz
         * session shows half as much time in the same window -- a difference
         * nobody would attribute to the sample rate. */
        for sr in [44_100.0f32, 48_000.0, 88_200.0, 96_000.0, 192_000.0] {
            let n = pick_fft_size(sr);
            let hop = pick_hop(sr, n);
            let cols_per_s = sr / hop as f32;
            assert!(hop.is_power_of_two() && hop <= n, "{sr} Hz picked hop {hop}");
            assert!(
                (40.0..=95.0).contains(&cols_per_s),
                "{sr} Hz scrolls at {cols_per_s} columns a second"
            );
        }
        /* At the ordinary rates it is the same 47 to within rounding. */
        for sr in [44_100.0f32, 48_000.0, 88_200.0, 96_000.0] {
            let cols = sr / pick_hop(sr, pick_fft_size(sr)) as f32;
            assert!((cols - TARGET_COLUMNS_PER_S).abs() < 8.0, "{sr} Hz: {cols}");
        }
    }

    /*
     * THE BOTTOM OF THE PICTURE IS A GRADIENT, NOT A STAIRCASE.
     *
     * A band is 3% wide and a bin is 5.86 Hz at 48 kHz with N=8192, so every
     * band below ~194 Hz -- 100 of the 256 -- is narrower than the grid it is
     * measured on. Snapping each to its nearest bin gave four and five
     * consecutive bands the SAME byte:
     *
     *     [128,128,128,128,128, 115,115,115,115,115, 130,130,130,130, ...]
     *
     * which drew the bottom two fifths of the picture as flat blocks with steps
     * between them, and read as the ANALYSIS being patchy. Interpolating
     * between the two bins either side gives the same information on the axis
     * the picture actually draws:
     *
     *     [126,127,128,126,123,121,118,115,118,122,125,129,130,130,130, ...]
     *
     * JUDGED OVER EIGHT DRAWS OF NOISE, NOT ONE. Snapping is a structure: it
     * scores 68 to 71 repeats in every draw, with a run of 13 at the bottom.
     * Interpolation's score depends on the draw -- where two neighbouring bins
     * happen to sit within a fraction of a dB, even a straight line between
     * them is a run of equal bytes -- and over 64 draws it ranged from 1 to 36
     * repeats and from 2 to 20 bands the longest run. A single draw therefore
     * says little, and one generator's lucky draw passed a threshold another's
     * misses. The total and the median do not wander like that, and the
     * thresholds sit halfway between the regimes.
     */
    #[test]
    fn the_bands_below_one_bin_are_interpolated_rather_than_repeated() {
        let sr = 48_000.0f32;
        let fft = pick_fft_size(sr);
        let hop = pick_hop(sr, fft);
        let cfg = Config {
            sample_rate: sr,
            fft_size: fft,
            hop,
            bands: 256,
            f_min: 10.0,
            f_max: 20_000.0,
            db_floor: -96.0,
            db_ceil: 0.0,
        };

        let draws = 8;
        let mut repeats = 0;
        let mut longest_runs = Vec::with_capacity(draws);
        for seed in 1..=draws as u64 {
            /* White noise, so every band sits well above the floor and a
             * repeat means the mapping repeated rather than that two bands
             * were both silent. */
            let mut rng = fastrand::Rng::with_seed(seed);
            let buf: Vec<f32> = (0..fft + hop * 4).map(|_| (rng.f32() * 2.0 - 1.0) * 0.25).collect();
            let mut a = Analyzer::new(cfg);
            a.push(&buf);

            let mut out = vec![0u8; 256 * 8];
            assert!(a.take_columns(&mut out, 8) > 0, "the noise produced no column");
            let col = &out[..256];

            let (mut longest, mut run) = (1, 1);
            for b in 1..100 {
                if col[b] == col[b - 1] {
                    repeats += 1;
                    run += 1;
                    longest = longest.max(run);
                } else {
                    run = 1;
                }
                assert!(col[b] > 0, "seed {seed}: band {b} sat on the floor; the noise was too quiet");
            }
            longest_runs.push(longest);
        }
        longest_runs.sort_unstable();
        let typical_longest = longest_runs[draws / 2];

        /* Over these eight, snapping scores 551 repeats and a run of 13 in every
         * draw; interpolating scores 152, and 6 the median longest run. */
        assert!(
            repeats <= draws * 34,
            "{repeats} of {} bands repeat their neighbour -- the staircase is back",
            draws * 99
        );
        assert!(
            typical_longest <= 9,
            "the typical longest run is {typical_longest} identical bands ({longest_runs:?}): flat blocks"
        );
    }

    #[test]
    fn a_thirty_hertz_sine_lands_on_a_thirty_hertz_band() {
        /*
         * THE CLAIM THE LONGER WINDOW EXISTS FOR. At the old 1024 points the
         * nearest bin to 30 Hz was bin 1 at 46.9 Hz, so a bass note and a kick
         * fundamental were the same row of pixels.
         */
        let mut a = Analyzer::new(Config { sample_rate: SR, ..Default::default() });
        a.push(&sine(30.0, 48_000, 1.0));

        let mut out = vec![0u8; a.bands() * 32];
        let cols = a.take_columns(&mut out, 32);
        assert!(cols > 0, "no column came out of a second of audio");

        let col = &out[(cols - 1) * a.bands()..cols * a.bands()];
        let peak = *col.iter().max().unwrap();
        let want = nearest_band(&a, 30.0);
        /*
         * THE PEAK DOWN HERE IS A PLATEAU, NOT A POINT. Bands are 3% apart --
         * under a hertz at 30 Hz -- while bins are 5.9 Hz apart, so half a dozen
         * neighbouring bands read the same bin and carry the same byte. Which of
         * them an argmax returns is a tie-break, not a measurement. So: the band
         * nearest 30 Hz is AT the maximum, which no tie-break can change.
         */
        assert_eq!(
            col[want], peak,
            "the 30 Hz band ({} Hz) read {} against a peak of {peak}",
            a.band_hz()[want], col[want]
        );
        /* And it is distinguishable from 60 Hz, which is the whole point: the
         * band holding 60 Hz is well down the ramp. */
        let sixty = nearest_band(&a, 60.0);
        assert!(col[sixty] < peak / 2, "60 Hz read {} against {peak}", col[sixty]);
    }

    #[test]
    fn a_range_change_moves_the_axis_at_once_and_the_table_follows() {
        let mut a = Analyzer::new(Config::default());
        assert!((a.band_hz()[0] - 10.0).abs() < 0.5, "the full axis starts at 10 Hz");

        /* The editor's scale is derived from the request, so it is correct
         * immediately -- before the audio thread has run a single frame. */
        a.set_range(200.0, 4000.0);
        let hz = a.band_hz();
        assert!((hz[0] - 200.0).abs() < 4.0, "the zoomed axis starts at {}", hz[0]);
        assert!((*hz.last().unwrap() - 4000.0).abs() < 80.0, "it ends at {}", hz.last().unwrap());

        /* And the audio thread's own table follows within one hop, which is what
         * makes the columns agree with the scale. */
        let cfg = a.config();
        a.push(&sine(1000.0, cfg.fft_size + cfg.hop * 8, 1.0));
        let mut out = vec![0u8; a.bands() * 16];
        let cols = a.take_columns(&mut out, 16);
        assert!(cols > 0, "no column after a range change");

        let col = &out[(cols - 1) * a.bands()..cols * a.bands()];
        let peak = col.iter().enumerate().max_by_key(|(_, &v)| v).map(|(i, _)| i).unwrap();
        let want = nearest_band(&a, 1000.0);
        assert!(
            (peak as i32 - want as i32).abs() <= 1,
            "1 kHz peaked in band {peak} ({} Hz) under the Mid range, expected {want}",
            hz[peak]
        );
    }

    #[test]
    fn columns_from_before_a_range_change_are_never_handed_out_after_it() {
        /*
         * THE FAULT WITH NO SYMPTOM UNTIL YOU ARE READING THE PICTURE.
         *
         * A range change is a request the audio thread adopts at its next frame,
         * so columns measured against the OLD range can already be sitting in
         * the ring when the editor redraws its scale for the new one. They are
         * not wrong, they are answers to a different question -- and drawn under
         * the new scale they are a stripe of mislabelled data at exactly the
         * moment someone is looking to see what changed.
         */
        let cfg = Config::default();
        let mut a = Analyzer::new(cfg);
        a.push(&sine(1000.0, cfg.fft_size + cfg.hop * 4, 1.0));

        a.set_range(200.0, 4000.0);

        let mut out = vec![0u8; a.bands() * 16];
        assert_eq!(
            a.take_columns(&mut out, 16), 0,
            "a column measured against the old range survived the change"
        );

        /* The ring is not stuck: new columns arrive as usual. */
        a.push(&sine(1000.0, cfg.hop * 4, 1.0));
        assert!(a.take_columns(&mut out, 16) > 0, "nothing came after the change");
    }

    #[test]
    fn a_range_that_cannot_be_drawn_is_ignored() {
        let a = Analyzer::new(Config::default());
        let before = a.range();
        for (lo, hi) in [(0.0, 100.0), (100.0, 100.0), (f32::NAN, 1000.0),
                         (100.0, f32::INFINITY), (1000.0, 1100.0)] {
            a.set_range(lo, hi);
            assert_eq!(a.range(), before, "set_range({lo}, {hi}) was accepted");
        }
    }

    #[test]
    fn the_axis_matches_the_table_the_audio_thread_builds() {
        /*
         * The scale is derived on the message thread and the bands are built on
         * the audio thread, from the same inputs by the same arithmetic. That is
         * what makes sharing nothing between them safe -- and it holds only as
         * long as the two really are the same arithmetic.
         */
        let cfg = Config::default();
        for (lo, hi) in [(10.0, 20_000.0), (10.0, 200.0), (2_000.0, 20_000.0)] {
            let a = Analyzer::new(Config { f_min: lo, f_max: hi, ..cfg });
            let table = Bands::new(
                cfg.bands, cfg.fft_size / 2 + 1, cfg.sample_rate, lo, hi,
            );
            let axis = a.band_hz();
            assert_eq!(axis.len(), table.centres.len());
            for (i, (&a_hz, &t_hz)) in axis.iter().zip(table.centres.iter()).enumerate() {
                assert!((a_hz - t_hz).abs() < 1e-3, "band {i}: axis {a_hz}, table {t_hz}");
            }
        }
    }

    #[test]
    fn silence_is_exactly_the_floor() {
        let mut a = Analyzer::new(Config::default());
        a.push(&vec![0.0f32; 8192]);
        let mut out = vec![9u8; a.bands() * 8];
        let cols = a.take_columns(&mut out, 8);
        assert!(cols > 0);
        for &v in &out[..cols * a.bands()] {
            assert_eq!(v, 0, "silence drew a colour");
        }
    }

    #[test]
    fn the_first_column_waits_for_a_full_window() {
        let cfg = Config::default();
        let mut a = Analyzer::new(cfg);
        /* One hop short of a full window: a column now would be mostly the
         * startup zeroes. */
        a.push(&sine(1000.0, cfg.fft_size - 1, 1.0));
        let mut out = vec![0u8; a.bands() * 4];
        assert_eq!(a.take_columns(&mut out, 4), 0, "a column escaped un-primed");

        a.push(&sine(1000.0, cfg.hop + 1, 1.0));
        assert!(a.take_columns(&mut out, 4) > 0, "no column once primed");
    }

    #[test]
    fn columns_arrive_at_one_per_hop() {
        let cfg = Config { hop: 256, ..Default::default() };
        let mut a = Analyzer::new(cfg);
        a.push(&sine(440.0, cfg.fft_size, 0.5)); /* primes, emits one */
        let mut out = vec![0u8; a.bands() * 64];
        let primed = a.take_columns(&mut out, 64);
        a.push(&sine(440.0, cfg.hop * 10, 0.5));
        assert_eq!(a.take_columns(&mut out, 64), 10, "primed was {primed}");
    }

    #[test]
    fn a_nan_in_the_audio_does_not_reach_the_picture() {
        let mut a = Analyzer::new(Config::default());
        let mut buf = sine(1000.0, 8192, 0.5);
        buf[100] = f32::NAN;
        buf[101] = f32::INFINITY;
        a.push(&buf);
        let mut out = vec![0u8; a.bands() * 16];
        let cols = a.take_columns(&mut out, 16);
        assert!(cols > 0);
        /* Nothing asserted about the values -- only that they are bytes at all,
         * which they cannot be if a NaN reached amplitude_to_byte. The real
         * claim is the absence of a panic and of a 255 stripe. */
        let last = &out[(cols - 1) * a.bands()..cols * a.bands()];
        assert!(last.iter().any(|&v| v > 0), "the tone vanished with the NaN");
    }

    #[test]
    fn the_halves_run_on_two_threads_and_lose_nothing() {
        /* The shape the C ABI uses: the producer on one thread, the consumer
         * on another, the ring between them. Every column produced arrives,
         * once, and none is dropped while the consumer keeps draining.
         *
         * The feeder stays at most half a ring ahead of what the consumer has
         * taken, so a descheduled consumer cannot overflow the ring: dropping
         * when full is correct behaviour, and not what this test measures. */
        use std::sync::atomic::{AtomicUsize, Ordering};
        use std::sync::Arc;
        let cfg = Config { fft_size: 1024, hop: 256, bands: 64, ..Config::default() };
        let (mut tx, mut rx) = Analyzer::new(cfg).split();
        let hops = 4000usize;
        let taken = Arc::new(AtomicUsize::new(0));
        let feeder_taken = Arc::clone(&taken);
        let feeder = std::thread::spawn(move || {
            let block = sine(1000.0, cfg.hop, 0.5);
            let warmup = cfg.fft_size / cfg.hop;
            for i in 0..hops + warmup {
                while i >= warmup
                    && i - warmup >= feeder_taken.load(Ordering::Acquire) + COLUMN_CAPACITY / 2
                {
                    std::thread::yield_now();
                }
                tx.push(&block);
            }
        });
        let mut out = vec![0u8; cfg.bands * 32];
        let mut got = 0usize;
        while !feeder.is_finished() {
            got += rx.take_columns(&mut out, 32);
            taken.store(got, Ordering::Release);
        }
        feeder.join().unwrap();
        got += rx.take_columns(&mut out, 32);
        while got < hops + 1 {
            let n = rx.take_columns(&mut out, 32);
            if n == 0 {
                break;
            }
            got += n;
        }
        assert_eq!(rx.dropped(), 0, "the ring overflowed with a consumer draining it");
        assert_eq!(got, hops + 1, "columns were lost or duplicated between threads");
    }

    #[test]
    fn the_ring_drops_rather_than_blocks_when_nothing_drains_it() {
        let cfg = Config::default();
        let mut a = Analyzer::new(cfg);
        a.push(&sine(1000.0, cfg.hop * (COLUMN_CAPACITY + 64) + cfg.fft_size, 0.5));
        assert!(a.dropped() > 0, "the ring never filled");

        let mut out = vec![0u8; a.bands() * COLUMN_CAPACITY];
        let cols = a.take_columns(&mut out, COLUMN_CAPACITY);
        assert_eq!(cols, COLUMN_CAPACITY, "a full ring gave back {cols}");
        /* And it recovers: space is free again. */
        a.push(&sine(1000.0, cfg.hop * 4, 0.5));
        assert_eq!(a.take_columns(&mut out, COLUMN_CAPACITY), 4);
    }

    #[test]
    fn take_columns_respects_a_short_buffer() {
        let cfg = Config::default();
        let mut a = Analyzer::new(cfg);
        a.push(&sine(1000.0, cfg.fft_size + cfg.hop * 8, 0.5));
        let mut out = vec![0u8; a.bands() * 3];
        /* max_cols says 8; the buffer holds 3. The buffer wins, and the rest
         * stay queued rather than being written past the end. */
        assert_eq!(a.take_columns(&mut out, 8), 3);
        let mut big = vec![0u8; a.bands() * 16];
        assert!(a.take_columns(&mut big, 16) >= 5);
    }

    #[test]
    fn a_hop_below_a_thirty_second_of_the_window_is_raised_to_it() {
        /*
         * A hop of 1 is a whole FFT per SAMPLE -- 8192-point transforms 48,000
         * times a second on the audio thread, which no machine keeps up with,
         * for columns 32 times denser than the window can resolve anyway.
         */
        for (fft, hop, want) in [(8192, 1, 256), (8192, 255, 256), (8192, 256, 256), (1024, 7, 32), (1024, 4096, 1024)] {
            let a = Analyzer::new(Config { fft_size: fft, hop, ..Default::default() });
            assert_eq!(a.config().hop, want, "fft {fft} hop {hop}");
        }
    }

    #[test]
    fn a_wild_config_is_clamped_rather_than_trusted() {
        let a = Analyzer::new(Config {
            sample_rate: -1.0,
            fft_size: 999,       /* not a power of two */
            hop: 0,
            bands: 0,
            f_min: 0.0,
            f_max: 10.0,         /* below f_min */
            db_floor: 0.0,
            db_ceil: -50.0,      /* inverted */
        });
        let c = a.config();
        assert_eq!(c.fft_size, 512);
        assert!(c.hop >= 1 && c.bands >= 1);
        assert!(c.sample_rate > 0.0 && c.db_ceil > c.db_floor);
        assert_eq!(a.band_hz().len(), c.bands);
    }
}

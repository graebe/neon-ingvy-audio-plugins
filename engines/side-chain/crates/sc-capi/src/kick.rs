// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The `sc_kick_*` C ABI: the kick behind the duck.

The editor's plot shows one cycle of the ducker -- every sample filed under the
SWEEP it had, so the shape the user drags sits over the audio it shaped. The
kick the user ducks against comes from ANOTHER track, through a Listen-In bus,
and has to be filed under the same sweep: the one the ducker had when that kick
sample sounded.

WHEN IT ARRIVED CANNOT SAY. Live runs tracks on parallel threads, so the kick's
block may have been published before this track's block ran or after it, and a
kick placed by arrival wanders by a block from one cycle to the next. The bus
stamps every run with the host's timeline (audio-bus ABI 3), and the shell
passes its own block's timeline to `sc_kick_note`: so this keeps the sweep of
its own recent frames by timeline sample and files each kick frame under the
sweep of the same sample. Exact, whichever track ran first.

  audio thread, per chunk, while an editor shows the plot
  -------------------------------------------------------
  sc_kick_note(k, timeline, has_timeline, sweep, n);   the ducker's own frames
  n = sc_kick_drain(k, reader, kick, sweep, cap);      the bus's, filed
  (repeat drain while it fills `cap`)

A kick frame newer than anything noted yet waits -- a track with more delay
compensation than this one can run ahead -- and one older than the history is
dropped. With no timeline on either side (a stopped transport) the kick is
filed BY ARRIVAL: its newest frame under this track's newest, and the status
says so, because that picture can be a block off.

THREADS. `create`, `destroy` and `prepare` are the main thread's, with no block
running; `note` and `drain` the audio thread's, and allocate nothing, take no
lock and make no system call; `status` any thread's.

The Move module has no bus and no editor, and builds without this (the `shell`
feature).
*/

use bus_capi::AbusReader;
use bus_core::Reader;
use std::cell::UnsafeCell;
use std::ffi::c_int;
use std::sync::atomic::{AtomicI32, Ordering};

/// Frames of this track's own sweep remembered: 0.34 s at 48 kHz, which a
/// kick can lag behind by -- a block, or the delay compensation between two
/// tracks -- and still be filed.
pub(crate) const HISTORY: usize = 16384;
/// Kick frames that may wait for this track to catch up.
pub(crate) const PENDING: usize = 16384;
/// Frames read off the bus at a time.
const READ: usize = 2048;
/// Reads per drain at most, so a backlog is worked off over a few blocks
/// rather than in one.
const READS_PER_DRAIN: usize = 8;
/// Runs of this track's frames remembered; contiguous runs merge, so a
/// running transport is one.
const RUNS: usize = 256;

/// What the kick in the picture is, for the editor's caption: `sc_kick_status`
/// answers one of the SC_KICK_* below.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ScKickStatus {
    /// No bus chosen.
    Off = 0,
    /// A bus chosen, nothing from it yet.
    Waiting = 1,
    /// Filed by the timeline: exact.
    Aligned = 2,
    /// Filed by arrival: the transport is stopped, so within a block.
    ByArrival = 3,
    /// The bus has sent nothing for half a second.
    Silent = 4,
    /// The bus runs at another sample rate, so it is not drawn.
    OtherRate = 5,
}

/// A frame's place: a timeline sample, or -- by arrival -- the index of the
/// ducker frame it sounded with.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Place {
    Timeline(i64),
    Frame(u64),
}

/// One run of this track's frames: `frames` from absolute frame `at`, from
/// timeline sample `timeline` if the host said.
#[derive(Clone, Copy, Default)]
struct Run {
    at: u64,
    frames: u64,
    timeline: Option<i64>,
}

#[derive(Clone, Copy)]
struct Waiting {
    x: f32,
    place: Place,
    /// `noted` when it arrived: how long it has waited.
    since: u64,
}

/// The kick tap without the bus: frames and timelines in, kick samples and
/// sweeps out. Everything it holds is allocated by `new`.
pub struct Tap {
    sweep: Box<[f32]>,
    noted: u64,
    runs: Box<[Run]>,
    run_count: usize,
    run_head: usize,
    pending: Box<[Waiting]>,
    pend_head: usize,
    pend_len: usize,
    scratch: Box<[f32]>,
    rate: u32,
    quiet: u64,
    mode: ScKickStatus,
    dropped: u64,
}

impl Default for Tap {
    fn default() -> Self {
        Self::new()
    }
}

impl Tap {
    pub fn new() -> Tap {
        let none = Waiting { x: 0.0, place: Place::Frame(0), since: 0 };
        Tap {
            sweep: vec![0.0; HISTORY].into_boxed_slice(),
            noted: 0,
            runs: vec![Run::default(); RUNS].into_boxed_slice(),
            run_count: 0,
            run_head: 0,
            pending: vec![none; PENDING].into_boxed_slice(),
            pend_head: 0,
            pend_len: 0,
            scratch: vec![0.0; READ * 2].into_boxed_slice(),
            rate: 0,
            quiet: 0,
            mode: ScKickStatus::Waiting,
            dropped: 0,
        }
    }

    /// Everything forgotten, at the host's rate. No block running.
    pub fn prepare(&mut self, sample_rate: u32) {
        self.rate = sample_rate;
        self.noted = 0;
        self.run_count = 0;
        self.run_head = 0;
        self.forget();
    }

    /// The kick frames waiting forgotten: another source, or a restart.
    pub fn forget(&mut self) {
        self.pend_head = 0;
        self.pend_len = 0;
        self.quiet = 0;
        self.mode = ScKickStatus::Waiting;
    }

    /// Kick frames dropped so far: arrived too late for the history, or
    /// pushed out of the waiting room.
    pub fn dropped(&self) -> u64 {
        self.dropped
    }

    /// This track's next `sweep.len()` frames, from timeline sample
    /// `timeline` if the host said.
    pub fn note(&mut self, timeline: Option<i64>, sweep: &[f32]) {
        let n = sweep.len() as u64;
        if n == 0 {
            return;
        }
        for (i, &s) in sweep.iter().enumerate() {
            self.sweep[((self.noted + i as u64) % HISTORY as u64) as usize] = s;
        }
        /* One run while the timeline goes on: the last one grows. */
        let last = (self.run_count > 0).then(|| self.runs[self.run_head]);
        let continues = last.is_some_and(|r| {
            r.at + r.frames == self.noted
                && match (r.timeline, timeline) {
                    (Some(a), Some(b)) => a.wrapping_add(r.frames as i64) == b,
                    (None, None) => true,
                    _ => false,
                }
        });
        if continues {
            self.runs[self.run_head].frames += n;
        } else {
            self.run_head = (self.run_head + 1) % RUNS;
            self.runs[self.run_head] = Run { at: self.noted, frames: n, timeline };
            self.run_count = (self.run_count + 1).min(RUNS);
        }
        self.noted += n;
        self.quiet += n;
    }

    /// The newest run this track noted, if any.
    fn newest(&self) -> Option<Run> {
        (self.run_count > 0).then(|| self.runs[self.run_head])
    }

    /// Kick frames from the bus, interleaved stereo, the first at timeline
    /// sample `timeline` if the sender said. Filed by the timeline when both
    /// sides have one, by arrival otherwise.
    pub fn offer(&mut self, stereo: &[f32], timeline: Option<i64>) {
        let m = stereo.len() / 2;
        if m == 0 {
            return;
        }
        self.quiet = 0;
        let timed = timeline.is_some() && self.newest().is_some_and(|r| r.timeline.is_some());
        for i in 0..m {
            let x = 0.5 * (stereo[2 * i] + stereo[2 * i + 1]);
            let place = match timeline {
                Some(t) if timed => Place::Timeline(t.wrapping_add(i as i64)),
                /* By arrival: the newest kick frame sounded with this track's
                 * newest. */
                _ => Place::Frame((self.noted + i as u64).wrapping_sub(m as u64)),
            };
            self.wait(Waiting { x, place, since: self.noted });
        }
    }

    fn wait(&mut self, w: Waiting) {
        if self.pend_len == PENDING {
            self.pend_head = (self.pend_head + 1) % PENDING;
            self.pend_len -= 1;
            self.dropped += 1;
        }
        self.pending[(self.pend_head + self.pend_len) % PENDING] = w;
        self.pend_len += 1;
    }

    /// The sweep of this track's frame `frame`, if it is still remembered.
    fn sweep_of(&self, frame: u64) -> Option<f32> {
        (frame < self.noted && self.noted - frame <= HISTORY as u64)
            .then(|| self.sweep[(frame % HISTORY as u64) as usize])
    }

    /// The frame of this track that sounded at timeline sample `t`: the
    /// newest run holding it, so a loop's second pass wins over its first.
    fn frame_at(&self, t: i64) -> Option<u64> {
        for back in 0..self.run_count {
            let r = self.runs[(self.run_head + RUNS - back) % RUNS];
            if self.noted - r.at > HISTORY as u64 + r.frames {
                break;
            }
            if let Some(start) = r.timeline {
                let off = t.wrapping_sub(start);
                if off >= 0 && (off as u64) < r.frames {
                    return Some(r.at + off as u64);
                }
            }
        }
        None
    }

    /// Whether a kick at timeline sample `t` may still find its frame: it is
    /// newer than anything noted, and has not waited longer than the
    /// history.
    fn may_come(&self, t: i64, since: u64) -> bool {
        let ahead = self
            .newest()
            .and_then(|r| r.timeline.map(|s| t >= s.wrapping_add(r.frames as i64)))
            .unwrap_or(false);
        ahead && self.noted - since < HISTORY as u64
    }

    /// Every waiting kick frame that can be placed now, in order, into `kick`
    /// and `sweep`: how many. A frame that cannot be placed yet stops it.
    pub fn file(&mut self, kick: &mut [f32], sweep: &mut [f32]) -> usize {
        let cap = kick.len().min(sweep.len());
        let mut n = 0;
        while self.pend_len > 0 && n < cap {
            let w = self.pending[self.pend_head];
            let (found, mode) = match w.place {
                Place::Timeline(t) => match self.frame_at(t) {
                    Some(f) => (self.sweep_of(f), ScKickStatus::Aligned),
                    None if self.may_come(t, w.since) => break,
                    None => (None, ScKickStatus::Aligned),
                },
                Place::Frame(f) => (self.sweep_of(f), ScKickStatus::ByArrival),
            };
            self.pend_head = (self.pend_head + 1) % PENDING;
            self.pend_len -= 1;
            match found {
                Some(s) => {
                    kick[n] = w.x;
                    sweep[n] = s;
                    n += 1;
                    self.mode = mode;
                }
                None => self.dropped += 1,
            }
        }
        n
    }

    /// Read what `reader` has and file what can be placed. `cap` frames at
    /// most; call again while it fills them.
    pub fn drain(&mut self, reader: &mut Reader, kick: &mut [f32], sweep: &mut [f32]) -> usize {
        if self.rate != 0 && reader.sample_rate() != self.rate {
            /* Read and thrown away, so the bus does not pile up behind us. */
            for _ in 0..READS_PER_DRAIN {
                if reader.read(&mut self.scratch).frames == 0 {
                    break;
                }
            }
            self.forget();
            self.mode = ScKickStatus::OtherRate;
            return 0;
        }
        if self.mode == ScKickStatus::OtherRate {
            self.mode = ScKickStatus::Waiting;
        }
        for _ in 0..READS_PER_DRAIN {
            let mut scratch = std::mem::take(&mut self.scratch);
            let got = reader.read(&mut scratch);
            if got.resynced {
                self.pend_head = 0;
                self.pend_len = 0;
            }
            let frames = got.frames as usize;
            let mut i = 0;
            while i < frames {
                let at = got.first + i as u64;
                let span = reader.stamp_at(at);
                let run = span.map_or(frames - i, |s| ((s.start + s.frames as u64 - at) as usize).min(frames - i));
                let timeline = span.and_then(|s| s.timeline_of(at));
                self.offer(&scratch[i * 2..(i + run) * 2], timeline);
                i += run;
            }
            self.scratch = scratch;
            if frames < READ {
                break;
            }
        }
        self.file(kick, sweep)
    }

    /// What the kick in the picture is now.
    pub fn status(&self) -> ScKickStatus {
        if self.rate != 0 && self.quiet > self.rate as u64 / 2 && self.mode != ScKickStatus::OtherRate {
            return ScKickStatus::Silent;
        }
        self.mode
    }
}

/// No bus chosen.
pub const SC_KICK_OFF: c_int = 0;
/// A bus chosen, nothing from it yet.
pub const SC_KICK_WAITING: c_int = 1;
/// Filed by the timeline: exact.
pub const SC_KICK_ALIGNED: c_int = 2;
/// Filed by arrival -- the transport is stopped -- so within a block.
pub const SC_KICK_BY_ARRIVAL: c_int = 3;
/// The bus has sent nothing for half a second.
pub const SC_KICK_SILENT: c_int = 4;
/// The bus runs at another sample rate, so it is not drawn.
pub const SC_KICK_OTHER_RATE: c_int = 5;

const _: () = assert!(
    SC_KICK_OFF == ScKickStatus::Off as c_int
        && SC_KICK_WAITING == ScKickStatus::Waiting as c_int
        && SC_KICK_ALIGNED == ScKickStatus::Aligned as c_int
        && SC_KICK_BY_ARRIVAL == ScKickStatus::ByArrival as c_int
        && SC_KICK_SILENT == ScKickStatus::Silent as c_int
        && SC_KICK_OTHER_RATE == ScKickStatus::OtherRate as c_int
);

/// The tap and the status other threads read.
pub struct ScKick {
    tap: UnsafeCell<Tap>,
    reader: UnsafeCell<*const AbusReader>,
    status: AtomicI32,
}

/* The tap is the audio thread's alone, by the ABI's thread rules; the status is
 * an atomic any thread may read. */
unsafe impl Sync for ScKick {}

/// A tap with everything it will hold allocated. The main thread.
#[no_mangle]
pub extern "C" fn sc_kick_create() -> *mut ScKick {
    Box::into_raw(Box::new(ScKick {
        tap: UnsafeCell::new(Tap::new()),
        reader: UnsafeCell::new(std::ptr::null()),
        status: AtomicI32::new(SC_KICK_OFF),
    }))
}

/// # Safety
/// `k` is null or from `sc_kick_create`, and no thread uses it afterwards.
#[no_mangle]
pub unsafe extern "C" fn sc_kick_destroy(k: *mut ScKick) {
    if !k.is_null() {
        drop(Box::from_raw(k));
    }
}

/// Everything forgotten, at the host's rate. While no block runs (the host's
/// prepare).
///
/// # Safety
/// `k` is null or live, and no `note` or `drain` runs.
#[no_mangle]
pub unsafe extern "C" fn sc_kick_prepare(k: *mut ScKick, sample_rate: u32) {
    if let Some(k) = k.as_ref() {
        (*k.tap.get()).prepare(sample_rate);
    }
}

/// This track's next `frames` frames: their sweep, as
/// sc_core_process_f32_split_tap gave it, and where the first sits on the
/// host's timeline (`has_timeline` 0 while the transport is stopped). The
/// audio thread.
///
/// # Safety
/// `k` is null or live; `sweep` holds `frames` floats.
#[no_mangle]
pub unsafe extern "C" fn sc_kick_note(k: *mut ScKick, timeline: i64, has_timeline: c_int, sweep: *const f32, frames: c_int) {
    let Some(k) = k.as_ref() else { return };
    if sweep.is_null() || frames <= 0 {
        return;
    }
    let sweep = std::slice::from_raw_parts(sweep, frames as usize);
    (*k.tap.get()).note((has_timeline != 0).then_some(timeline), sweep);
}

/// The kick frames `reader` has that can be placed now: up to `cap` into
/// `kick` (the mid of the two sides) and `sweep` (where each sounded in this
/// track's cycle). Returns how many; call again while it returns `cap`. A NULL
/// reader is no bus; a different one than last time starts afresh. The audio
/// thread.
///
/// # Safety
/// `k` is null or live; `reader` is null or from `abus_reader_open`, read on
/// this thread only; `kick` and `sweep` hold `cap` floats.
#[no_mangle]
pub unsafe extern "C" fn sc_kick_drain(
    k: *mut ScKick,
    reader: *mut AbusReader,
    kick: *mut f32,
    sweep: *mut f32,
    cap: c_int,
) -> c_int {
    let Some(k) = k.as_ref() else { return 0 };
    let tap = &mut *k.tap.get();
    let last = &mut *k.reader.get();
    if reader.cast_const() != *last {
        *last = reader.cast_const();
        tap.forget();
    }
    let Some(reader) = reader.as_mut() else {
        k.status.store(SC_KICK_OFF, Ordering::Relaxed);
        return 0;
    };
    if kick.is_null() || sweep.is_null() || cap <= 0 {
        return 0;
    }
    let n = tap.drain(
        reader.reader_mut(),
        std::slice::from_raw_parts_mut(kick, cap as usize),
        std::slice::from_raw_parts_mut(sweep, cap as usize),
    );
    k.status.store(tap.status() as c_int, Ordering::Relaxed);
    n as c_int
}

/// What the kick in the picture is, as of the last drain: one of the
/// SC_KICK_* -- SC_KICK_OFF for a NULL tap. Any thread.
///
/// # Safety
/// `k` is null or live.
#[no_mangle]
pub unsafe extern "C" fn sc_kick_status(k: *const ScKick) -> c_int {
    k.as_ref().map_or(SC_KICK_OFF, |k| k.status.load(Ordering::Relaxed))
}

#[cfg(test)]
mod tests;

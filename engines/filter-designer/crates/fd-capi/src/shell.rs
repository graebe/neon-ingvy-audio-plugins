// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
`fd_shell_*`: the engine as the plugin holds it, behind shell-core's bridge,
and the picture its editor draws.

THREE THREADS AND WHAT EACH MAY CALL:

```text
  construction   fd_shell_create, _destroy
  the message    fd_shell_post_sample_rate,   applied at the top of the
  thread         fd_shell_post_reset,         next block
                 fd_shell_post_watching
                 fd_shell_read                the picture, worked out here
  the audio      fd_shell_begin, fd_core_*,   one block, in that order
  thread         fd_shell_end
```

THE PICTURE IS WORKED OUT ON THE THREAD THAT ASKS FOR IT. The audio thread
publishes only what it alone has -- its rate and, while a window shows the
analyzer, the last [`fd_core::RING`] samples in and out -- about a hundred
times a second (shell-core's cadence). `fd_shell_read` then designs every
node from the values the HOST holds, with the function the engine runs
(fd_core::design::design), and evaluates each cascade's own response at
[`FD_POINTS`] frequencies, 10 Hz to 22 kHz in equal steps of pitch: what is
drawn is the filter that plays, and a handle dragged moves its curve in the
same frame. A node's curve is worked out again only when its settings moved.

THE TOTAL is drawn for one lane, the one the editor asks for: the left (and
right) of a stereo signal, or its mid or side. Every Stereo node is in every
lane; a Left, Right, Mid or Side node in its own. The isolator and Output
are in all of them.

THE ANALYZER: the input's and the output's spectrum, mono, through a Hann
window, as dB per band of the picture's frequencies -- the loudest bin inside
a band, or the spectrum between two bins where a band holds none. A band
rises at once and falls over 300 ms, so the picture reads as a level rather
than a flicker.
*/

use crate::{FdCore, FD_NODES};
use fd_core::design::{channel_of, design, edges, globals_of, handle, is_heard, settings_of, Channel, Globals, Settings, Shape};
use fd_core::{isolator, Param, PARAM_COUNT, RING};
use ni_filter::Complex64;
use realfft::num_complex::Complex32;
use realfft::{RealFftPlanner, RealToComplex};
use ni_dsp::ffi::drop_box;
use shell_core::{capi, Bridge, Model};
use std::sync::{Arc, Mutex};

/// How many frequencies the picture has.
pub const FD_POINTS: usize = 512;
/// The picture's lowest and highest frequency, in Hz.
pub const FD_LOWEST_HZ: f64 = 10.0;
pub const FD_HIGHEST_HZ: f64 = 22_000.0;

/// The lanes a total can be drawn for.
pub const FD_LANE_LEFT: u32 = 0;
pub const FD_LANE_RIGHT: u32 = 1;
pub const FD_LANE_MID: u32 = 2;
pub const FD_LANE_SIDE: u32 = 3;

/// How a band of the analyzer falls, in seconds.
const RELEASE_S: f64 = 0.3;
/// The analyzer's floor, in dBFS.
const FLOOR_DB: f32 = -150.0;

/// What the editor draws, in dB at each of `hz`.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct FdPicture {
    /// Changes whenever new audio arrived.
    pub serial: u32,
    /// The rate the curves are drawn at.
    pub sample_rate: f64,
    /// The frequencies, in Hz, lowest first.
    pub hz: [f32; FD_POINTS],
    /// Each node alone, Empty ones flat; a bypassed node's too, as it would
    /// sound.
    pub node_db: [[f32; FD_POINTS]; FD_NODES],
    /// Whether the rate clamps a node's edge (its band narrows).
    pub node_clamped: [bool; FD_NODES],
    /// Where each node's handle sits, in Hz and dB (design::handle); NaN
    /// for an Empty node.
    pub handle_hz: [f64; FD_NODES],
    pub handle_db: [f64; FD_NODES],
    /// A Bell's, band pass's or notch's -3 dB edges, in Hz; NaN for others.
    pub edge_hz: [[f64; 2]; FD_NODES],
    /// Everything heard, in the lane asked for: nodes, isolator, Output.
    pub total_db: [f32; FD_POINTS],
    /// The isolator's three bands, each at its level, and their sum.
    pub iso_db: [[f32; FD_POINTS]; 3],
    pub iso_total_db: [f32; FD_POINTS],
    /// Whether the analyzer below is live (a window watching).
    pub analyzer: bool,
    /// The analyzer: the input's and the output's level per band, dBFS.
    pub pre_db: [f32; FD_POINTS],
    pub post_db: [f32; FD_POINTS],
}

impl FdPicture {
    fn empty() -> FdPicture {
        FdPicture {
            serial: 0,
            sample_rate: 0.0,
            hz: [0.0; FD_POINTS],
            node_db: [[0.0; FD_POINTS]; FD_NODES],
            node_clamped: [false; FD_NODES],
            handle_hz: [f64::NAN; FD_NODES],
            handle_db: [f64::NAN; FD_NODES],
            edge_hz: [[f64::NAN; 2]; FD_NODES],
            total_db: [0.0; FD_POINTS],
            iso_db: [[0.0; FD_POINTS]; 3],
            iso_total_db: [0.0; FD_POINTS],
            analyzer: false,
            pre_db: [FLOOR_DB; FD_POINTS],
            post_db: [FLOOR_DB; FD_POINTS],
        }
    }
}

/// The picture's frequencies: equally spaced in pitch.
fn frequencies() -> [f64; FD_POINTS] {
    ni_dsp::axis::log_points(FD_LOWEST_HZ, FD_HIGHEST_HZ)
}

/* ------------------------------------------------- the published frame -- */

/// What the audio thread publishes.
#[derive(Clone)]
pub struct FdFrame {
    serial: u32,
    sample_rate: f64,
    watching: bool,
    pre: Box<[f32]>,
    post: Box<[f32]>,
}

/// What another thread may ask of the engine.
#[derive(Clone, Copy, Debug)]
pub enum Command {
    SampleRate(f64),
    /// What a shell posts when the host (re)activates it.
    Reset,
    /// Whether a window shows the analyzer.
    Watching(bool),
}

/// Commands between two blocks.
const QUEUE: usize = 16;

impl Model for FdCore {
    type Command = Command;
    type Frame = FdFrame;

    fn new_frame(&self) -> FdFrame {
        FdFrame {
            serial: 0,
            sample_rate: self.engine.sample_rate(),
            watching: false,
            pre: vec![0.0; RING].into_boxed_slice(),
            post: vec![0.0; RING].into_boxed_slice(),
        }
    }

    fn apply(&mut self, cmd: &Command) {
        match *cmd {
            Command::SampleRate(rate) => self.engine.set_sample_rate(rate),
            Command::Reset => self.engine.reset(),
            Command::Watching(on) => {
                self.watching = on;
                self.engine.set_watching(on);
            }
        }
    }

    fn publish(&self, f: &mut FdFrame) {
        f.serial = self.serial;
        f.sample_rate = self.engine.sample_rate();
        f.watching = self.watching;
        if self.watching {
            self.engine.rings(&mut f.pre, &mut f.post);
        }
    }

    /* Every parameter reaches the engine from the shell on the audio
     * thread, so there is no view to rebuild. */
    fn restore(&mut self, _: &FdFrame) {}
}

/* ------------------------------------------------------- the reader -- */

/// A node's curve, and what it was worked out from.
#[derive(Clone, Copy)]
struct Curve {
    from: (Settings, Globals, f64),
    db: [f32; FD_POINTS],
    clamped: bool,
    handle: Option<(f64, f64)>,
    edges: Option<(f64, f64)>,
}

/// The editor thread's half: the analyzer's transform and what it keeps,
/// and every node's last curve.
struct Reader {
    hz: [f64; FD_POINTS],
    fft: Arc<dyn RealToComplex<f32>>,
    window: Vec<f32>,
    norm: f32,
    input: Vec<f32>,
    spectrum: Vec<Complex32>,
    scratch: Vec<Complex32>,
    bins: Vec<f32>,
    pre_ring: Vec<f32>,
    post_ring: Vec<f32>,
    pre: [f32; FD_POINTS],
    post: [f32; FD_POINTS],
    last_serial: Option<u32>,
    curves: [Option<Curve>; FD_NODES],
}

impl Reader {
    fn new() -> Reader {
        let fft = RealFftPlanner::<f32>::new().plan_fft_forward(RING);
        /* Periodic Hann: a window's sum, the level a full-scale sine reads. */
        let window: Vec<f32> = (0..RING)
            .map(|i| (0.5 - 0.5 * (2.0 * core::f64::consts::PI * i as f64 / RING as f64).cos()) as f32)
            .collect();
        let sum: f32 = window.iter().sum();
        Reader {
            hz: frequencies(),
            input: fft.make_input_vec(),
            spectrum: fft.make_output_vec(),
            scratch: fft.make_scratch_vec(),
            fft,
            window,
            norm: 2.0 / sum,
            bins: vec![FLOOR_DB; RING / 2 + 1],
            pre_ring: vec![0.0; RING],
            post_ring: vec![0.0; RING],
            pre: [FLOOR_DB; FD_POINTS],
            post: [FLOOR_DB; FD_POINTS],
            last_serial: None,
            curves: [None; FD_NODES],
        }
    }

    /// `ring`'s spectrum, as dBFS per bin, into `bins`.
    fn spectrum_of(&mut self, post: bool) {
        let ring = if post { &self.post_ring } else { &self.pre_ring };
        for ((x, s), w) in self.input.iter_mut().zip(ring.iter()).zip(self.window.iter()) {
            *x = s * w;
        }
        if self.fft.process_with_scratch(&mut self.input, &mut self.spectrum, &mut self.scratch).is_err() {
            return;
        }
        for (b, c) in self.bins.iter_mut().zip(self.spectrum.iter()) {
            *b = 20.0 * (c.norm() * self.norm).max(1e-9).log10();
        }
    }

    /// The bins, banded at the picture's frequencies, at the rate `fs`.
    fn banded(&self, fs: f64, out: &mut [f32; FD_POINTS]) {
        let df = fs / RING as f64;
        let last = self.bins.len() - 1;
        for (i, o) in out.iter_mut().enumerate() {
            let hz = self.hz[i];
            if hz >= 0.5 * fs {
                *o = FLOOR_DB;
                continue;
            }
            let lo = if i == 0 { hz } else { (self.hz[i - 1] * hz).sqrt() };
            let hi = if i + 1 == FD_POINTS { hz } else { (self.hz[i + 1] * hz).sqrt() };
            let (k0, k1) = ((lo / df).ceil() as usize, ((hi / df).floor() as usize).min(last));
            *o = if k1 >= k0 && k0 <= last {
                self.bins[k0..=k1].iter().copied().fold(FLOOR_DB, f32::max)
            } else {
                let k = hz / df;
                let (a, t) = ((k.floor() as usize).min(last), (k - k.floor()) as f32);
                let b = (a + 1).min(last);
                self.bins[a] + t * (self.bins[b] - self.bins[a])
            };
        }
    }

    /// A new frame's analyzer: each band up at once, down over RELEASE_S.
    fn analyse(&mut self, fs: f64, elapsed_s: f64) {
        let fall = (1.0 - (-elapsed_s / RELEASE_S).exp()) as f32;
        let mut fresh = [FLOOR_DB; FD_POINTS];
        for post in [false, true] {
            self.spectrum_of(post);
            self.banded(fs, &mut fresh);
            let kept = if post { &mut self.post } else { &mut self.pre };
            for (k, f) in kept.iter_mut().zip(fresh.iter()) {
                *k = if *f > *k { *f } else { *k + (*f - *k) * fall };
            }
        }
    }

    /// Node `slot`'s curve, worked out again only when its settings moved.
    fn curve(&mut self, values: &[f64], slot: usize, fs: f64) -> &Curve {
        let s = settings_of(values, slot as u8);
        let g = globals_of(values);
        let from = (s, g, fs);
        let (hz, cached) = (&self.hz, &mut self.curves[slot]);
        if cached.as_ref().is_some_and(|c| c.from != from) {
            *cached = None;
        }
        cached.get_or_insert_with(|| {
            let mut db = [0.0f32; FD_POINTS];
            let mut clamped = false;
            let mut at = None;
            if s.shape != Shape::Empty {
                let (c, cl) = design(&s, None, g, fs);
                clamped = cl;
                for (d, hz) in db.iter_mut().zip(hz.iter()) {
                    *d = c.db(hz.min(0.4999 * fs), fs) as f32;
                }
                at = handle(&s, g, &c, fs);
            }
            Curve { from, db, clamped, handle: at, edges: edges(&s) }
        })
    }
}

fn in_lane(channel: Channel, lane: u32) -> bool {
    matches!(
        (channel, lane),
        (Channel::Stereo, _) | (Channel::Left, FD_LANE_LEFT) | (Channel::Right, FD_LANE_RIGHT) | (Channel::Mid, FD_LANE_MID) | (Channel::Side, FD_LANE_SIDE)
    )
}

/// The picture for `values` (the host's, in table order) at the frame's
/// rate, the total in `lane`.
fn draw(reader: &mut Reader, values: &[f64], lane: u32, fs: f64, out: &mut FdPicture) {
    for (h, hz) in out.hz.iter_mut().zip(reader.hz.iter()) {
        *h = *hz as f32;
    }
    out.sample_rate = fs;
    let output = values[Param::Output.index()] as f32;
    out.total_db = [output; FD_POINTS];
    for slot in 0..FD_NODES {
        let heard = is_heard(values, slot as u8) && in_lane(channel_of(values, slot as u8), lane);
        let c = reader.curve(values, slot, fs);
        out.node_db[slot] = c.db;
        out.node_clamped[slot] = c.clamped;
        (out.handle_hz[slot], out.handle_db[slot]) = c.handle.unwrap_or((f64::NAN, f64::NAN));
        out.edge_hz[slot] = c.edges.map_or([f64::NAN; 2], |(lo, hi)| [lo, hi]);
        if heard {
            for (t, d) in out.total_db.iter_mut().zip(c.db.iter()) {
                *t += *d;
            }
        }
    }
    /* The isolator: each band's path, at its level, summed as the signal
     * sums them -- complex, so the crossovers' phase is in the total. */
    let (paths, levels) = isolator::picture(values, fs);
    let iso_on = values[Param::IsoOn.index()] >= 0.5;
    let db = |h: Complex64| (20.0 * h.norm().max(1e-15).log10()) as f32;
    for (i, hz) in reader.hz.iter().enumerate() {
        let w = 2.0 * core::f64::consts::PI * hz.min(0.4999 * fs) / fs;
        let mut sum = Complex64::new(0.0, 0.0);
        for b in 0..3 {
            let h = paths[b][0].response(w) * paths[b][1].response(w) * levels[b];
            out.iso_db[b][i] = db(h);
            sum += h;
        }
        out.iso_total_db[i] = db(sum);
        if iso_on {
            out.total_db[i] += out.iso_total_db[i];
        }
    }
    out.pre_db = reader.pre;
    out.post_db = reader.post;
}

/* -------------------------------------------------------- the handle -- */

/// The handle a plugin instance holds.
pub struct FdShell {
    bridge: Bridge<FdCore>,
    reader: Mutex<Reader>,
}

/// A new engine at `sample_rate` (44.1 kHz if that is not positive), every
/// parameter at its default. Construction only: it allocates.
#[no_mangle]
pub extern "C" fn fd_shell_create(sample_rate: f64) -> *mut FdShell {
    let core = FdCore::new(sample_rate);
    let rate = core.engine.sample_rate();
    let bridge = capi::create(core, rate, QUEUE);
    Box::into_raw(Box::new(FdShell { bridge, reader: Mutex::new(Reader::new()) }))
}

/// # Safety
/// `shell` is null or from `fd_shell_create`, and no thread uses it after.
#[no_mangle]
pub unsafe extern "C" fn fd_shell_destroy(shell: *mut FdShell) {
    drop_box(shell);
}

/// The host's rate, applied at the top of the next block. Not the audio
/// thread. A rate that is not positive is ignored.
///
/// # Safety
/// `shell` is null or live.
#[no_mangle]
pub unsafe extern "C" fn fd_shell_post_sample_rate(shell: *const FdShell, sample_rate: f64) {
    if let Some(shell) = shell.as_ref() {
        capi::post_sample_rate(&shell.bridge, sample_rate, Command::SampleRate);
    }
}

/// Every filter empties, at the top of the next block. Not the audio thread.
///
/// # Safety
/// `shell` is null or live.
#[no_mangle]
pub unsafe extern "C" fn fd_shell_post_reset(shell: *const FdShell) {
    if let Some(shell) = shell.as_ref() {
        shell.bridge.post(Command::Reset);
    }
}

/// Whether a window shows the analyzer, from the top of the next block: the
/// rings are kept and published only while one does. Not the audio thread.
///
/// # Safety
/// `shell` is null or live.
#[no_mangle]
pub unsafe extern "C" fn fd_shell_post_watching(shell: *const FdShell, watching: bool) {
    if let Some(shell) = shell.as_ref() {
        shell.bridge.post(Command::Watching(watching));
    }
}

/// The audio thread, at the top of a block: the engine, for this block only.
///
/// # Safety
/// `shell` is null or live; the audio thread only, one block at a time; the
/// pointer is not used after `fd_shell_end`.
#[no_mangle]
pub unsafe extern "C" fn fd_shell_begin(shell: *const FdShell) -> *mut FdCore {
    capi::begin(shell.as_ref().map(|s| &s.bridge))
}

/// The audio thread, at the end of a block of `frames`: publishes when a
/// frame is due.
///
/// # Safety
/// As `fd_shell_begin`, after it.
#[no_mangle]
pub unsafe extern "C" fn fd_shell_end(shell: *const FdShell, frames: u32) {
    if let Some(shell) = shell.as_ref() {
        shell.bridge.end(frames);
    }
}

/// THE PICTURE for `values` -- the host's, `count` of them in table order
/// (FD_PARAM_COUNT) -- with the total in `lane` (FD_LANE_*), into `out`.
/// Not the audio thread: the curves and the analyzer are worked out here.
/// False, and nothing written, without a shell, an `out` or a whole table.
///
/// # Safety
/// `shell` is null or live; `values` is null or holds `count` doubles;
/// `out` is null or writable.
#[no_mangle]
pub unsafe extern "C" fn fd_shell_read(shell: *const FdShell, values: *const f64, count: usize, lane: u32, out: *mut FdPicture) -> bool {
    let (Some(shell), Some(out)) = (shell.as_ref(), out.as_mut()) else {
        return false;
    };
    if values.is_null() || count != PARAM_COUNT {
        return false;
    }
    let values = std::slice::from_raw_parts(values, count);
    let Ok(mut reader) = shell.reader.lock() else { return false };
    let reader = &mut *reader;
    let (serial, fs, watching, fresh) = shell.bridge.read(|r| {
        let f = r.frame;
        let fresh = f.watching && reader.last_serial != Some(f.serial);
        if fresh {
            reader.pre_ring.copy_from_slice(&f.pre);
            reader.post_ring.copy_from_slice(&f.post);
        }
        (f.serial, f.sample_rate, f.watching, fresh)
    });
    let fs = if fs > 0.0 { fs } else { fd_core::DEFAULT_SAMPLE_RATE };
    if fresh {
        let elapsed = reader.last_serial.map_or(1.0, |s| f64::from(serial.wrapping_sub(s)) / fs);
        reader.analyse(fs, elapsed);
        reader.last_serial = Some(serial);
    }
    out.serial = serial;
    out.analyzer = watching;
    draw(reader, values, lane, fs, out);
    true
}

/// An empty picture, for a shell to hold before its first read. Any thread.
///
/// # Safety
/// `out` is null or writable.
#[no_mangle]
pub unsafe extern "C" fn fd_picture_init(out: *mut FdPicture) {
    if let Some(out) = out.as_mut() {
        *out = FdPicture::empty();
        for (h, hz) in out.hz.iter_mut().zip(frequencies().iter()) {
            *h = *hz as f32;
        }
    }
}

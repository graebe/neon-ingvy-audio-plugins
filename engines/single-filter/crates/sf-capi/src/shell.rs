// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
`sf_shell_*`: the engine as a plugin holds it, behind shell-core's bridge,
and the picture its editor draws.

THREE THREADS AND WHAT EACH MAY CALL:

```text
  construction   sf_shell_create, _destroy
  the message    sf_shell_post_sample_rate,   applied at the top of the
  thread         sf_shell_post_reset,         next block
                 sf_shell_post_watching
                 sf_shell_read                the picture, worked out here;
                                              a new lane is posted too
  the audio      sf_shell_begin, sf_core_*,   one block, in that order
  thread         sf_shell_end
```

THE PICTURE IS WORKED OUT ON THE THREAD THAT ASKS FOR IT. The audio thread
publishes only what it alone has -- its rate and, while a window shows the
analyzer, the last [`RING`] samples in and out -- about a hundred times a
second (shell-core's cadence). `sf_shell_read` then designs the filter from
the values the HOST holds, with the function the engine runs
(ni_filter_node::design), and evaluates its response at [`SF_POINTS`]
frequencies, 10 Hz to 22 kHz in equal steps of pitch: what is drawn is the
filter that plays, and a handle dragged moves its curve in the same frame.

ONE FILTER, ONE LANE. The picture is of the half the filter works on: a
Left filter's in the left, a Mid filter's in the mid, a Stereo one's in the
mid (what a centred source hears). In its own lane a filter is its own
transfer, so the curve is exact there, and the analyzer reads the same lane
-- the input and output of what is filtered (ni_analyzer::Analyzer).
*/

use crate::engine::{shape_of, values_of};
use crate::SfCore;
use ni_analyzer::{Analyzer, FLOOR_DB};
use ni_dsp::ffi::drop_box;
use ni_filter_node::design::{design, edges, handle};
use ni_filter_node::picture::{angle, decibels};
use ni_filter_node::{Channel, Globals, Settings};
use sf_core::params::{self, Field};
use sf_core::{target_of, Lane, Shape, RING};
use shell_core::{capi, Bridge, Model};
use std::sync::Mutex;

/// How many frequencies the picture has.
pub const SF_POINTS: usize = 512;
/// The picture's lowest and highest frequency, in Hz.
pub const SF_LOWEST_HZ: f64 = 10.0;
pub const SF_HIGHEST_HZ: f64 = 22_000.0;

/// The lane a filter of `channel` is drawn and analysed in.
fn lane_of(channel: Channel) -> Lane {
    match channel {
        Channel::Left => Lane::Left,
        Channel::Right => Lane::Right,
        Channel::Side => Lane::Side,
        Channel::Stereo | Channel::Mid => Lane::Mid,
    }
}

/// What the editor draws, in dB at each of `hz`.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct SfPicture {
    /// Changes whenever new audio arrived.
    pub serial: u32,
    /// The rate the curve is drawn at.
    pub sample_rate: f64,
    /// The frequencies, in Hz, lowest first.
    pub hz: [f32; SF_POINTS],
    /// The filter alone.
    pub curve_db: [f32; SF_POINTS],
    /// The filter and Output: what its lane hears.
    pub total_db: [f32; SF_POINTS],
    /// The filter's phase, in degrees, wrapped to -180..180: what an All
    /// Pass, whose magnitude is flat, is drawn by.
    pub phase_deg: [f32; SF_POINTS],
    /// Whether the rate clamps the filter's edge (its band narrows).
    pub clamped: bool,
    /// Where the handle sits, in Hz and dB (on the filter alone).
    pub handle_hz: f64,
    pub handle_db: f64,
    /// A Bell's, band pass's or notch's -3 dB edges, in Hz; NaN for others.
    pub edge_hz: [f64; 2],
    /// Whether the analyzer below is live (a window watching).
    pub analyzer: bool,
    /// The analyzer: the input's and the output's level per band, dBFS, in
    /// the filter's lane.
    pub pre_db: [f32; SF_POINTS],
    pub post_db: [f32; SF_POINTS],
}

impl SfPicture {
    fn empty() -> SfPicture {
        let mut p = SfPicture {
            serial: 0,
            sample_rate: 0.0,
            hz: [0.0; SF_POINTS],
            curve_db: [0.0; SF_POINTS],
            total_db: [0.0; SF_POINTS],
            phase_deg: [0.0; SF_POINTS],
            clamped: false,
            handle_hz: f64::NAN,
            handle_db: f64::NAN,
            edge_hz: [f64::NAN; 2],
            analyzer: false,
            pre_db: [FLOOR_DB; SF_POINTS],
            post_db: [FLOOR_DB; SF_POINTS],
        };
        for (h, hz) in p.hz.iter_mut().zip(frequencies().iter()) {
            *h = *hz as f32;
        }
        p
    }
}

/// The picture's frequencies: equally spaced in pitch.
fn frequencies() -> [f64; SF_POINTS] {
    ni_dsp::axis::log_points(SF_LOWEST_HZ, SF_HIGHEST_HZ)
}

/* ------------------------------------------------- the published frame -- */

/// What the audio thread publishes.
#[derive(Clone)]
pub struct SfFrame {
    serial: u32,
    sample_rate: f64,
    watching: bool,
    /// The lane the rings below keep.
    lane: Lane,
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
    /// The lane the window draws, which the analyzer's rings keep.
    Lane(Lane),
}

/// Commands between two blocks.
const QUEUE: usize = 16;

impl Model for SfCore {
    type Command = Command;
    type Frame = SfFrame;

    fn new_frame(&self) -> SfFrame {
        SfFrame {
            serial: 0,
            sample_rate: self.engine.sample_rate(),
            watching: false,
            lane: self.lane,
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
            Command::Lane(lane) => {
                self.lane = lane;
                self.engine.set_lane(lane);
            }
        }
    }

    fn publish(&self, f: &mut SfFrame) {
        f.serial = self.serial;
        f.sample_rate = self.engine.sample_rate();
        f.watching = self.watching;
        f.lane = self.lane;
        if self.watching {
            self.engine.rings(&mut f.pre, &mut f.post);
        }
    }

    /* Every parameter reaches the engine from the shell on the audio
     * thread, so there is no view to rebuild. */
    fn restore(&mut self, _: &SfFrame) {}
}

/* ------------------------------------------------------- the reader -- */

/// The curve, and what it was worked out from.
#[derive(Clone, Copy)]
struct Curve {
    from: (Settings, Globals, f64),
    db: [f32; SF_POINTS],
    phase: [f32; SF_POINTS],
    clamped: bool,
    handle: Option<(f64, f64)>,
    edges: Option<(f64, f64)>,
}

/// The editor thread's half: the analyzer, and the last curve.
struct Reader {
    hz: [f64; SF_POINTS],
    analyzer: Analyzer<SF_POINTS>,
    curve: Option<Box<Curve>>,
}

impl Reader {
    fn new() -> Reader {
        Reader { hz: frequencies(), analyzer: Analyzer::new(frequencies()), curve: None }
    }

    /// The curve of `s` under `g` at `fs`, worked out again only when one of
    /// them moved.
    fn curve(&mut self, s: Settings, g: Globals, fs: f64) -> &Curve {
        let from = (s, g, fs);
        if self.curve.as_ref().is_some_and(|c| c.from != from) {
            self.curve = None;
        }
        let hz = &self.hz;
        self.curve.get_or_insert_with(|| {
            let (c, clamped) = design(&s, None, g, fs);
            let (mut db, mut phase) = ([0.0f32; SF_POINTS], [0.0f32; SF_POINTS]);
            for ((d, ph), hz) in db.iter_mut().zip(phase.iter_mut()).zip(hz.iter()) {
                let h = c.response(angle(*hz, fs));
                *d = decibels(h);
                *ph = h.arg().to_degrees() as f32;
            }
            Box::new(Curve { from, db, phase, clamped, handle: handle(&s, g, &c, fs), edges: edges(&s) })
        })
    }
}

/* -------------------------------------------------------- the handle -- */

/// The handle a plugin instance holds.
pub struct SfShell {
    shape: Shape,
    bridge: Bridge<SfCore>,
    reader: Mutex<Reader>,
}

/// A new engine for product `shape` (SF_SHAPE_*) at `sample_rate` (44.1 kHz
/// if that is not positive), every parameter at its default; null for a shape
/// that is no product. Construction only: it allocates.
#[no_mangle]
pub extern "C" fn sf_shell_create(shape: u32, sample_rate: f64) -> *mut SfShell {
    let Some(core) = shape_of(shape).and_then(|s| SfCore::new(s, sample_rate)) else {
        return core::ptr::null_mut();
    };
    let shape = core.engine.shape();
    let rate = core.engine.sample_rate();
    let bridge = capi::create(core, rate, QUEUE);
    Box::into_raw(Box::new(SfShell { shape, bridge, reader: Mutex::new(Reader::new()) }))
}

/// # Safety
/// `shell` is null or from `sf_shell_create`, and no thread uses it after.
#[no_mangle]
pub unsafe extern "C" fn sf_shell_destroy(shell: *mut SfShell) {
    drop_box(shell);
}

/// The host's rate, applied at the top of the next block. Not the audio
/// thread. A rate that is not positive is ignored.
///
/// # Safety
/// `shell` is null or live.
#[no_mangle]
pub unsafe extern "C" fn sf_shell_post_sample_rate(shell: *const SfShell, sample_rate: f64) {
    if let Some(shell) = shell.as_ref() {
        capi::post_sample_rate(&shell.bridge, sample_rate, Command::SampleRate);
    }
}

/// The filter empties, at the top of the next block. Not the audio thread.
///
/// # Safety
/// `shell` is null or live.
#[no_mangle]
pub unsafe extern "C" fn sf_shell_post_reset(shell: *const SfShell) {
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
pub unsafe extern "C" fn sf_shell_post_watching(shell: *const SfShell, watching: bool) {
    if let Some(shell) = shell.as_ref() {
        shell.bridge.post(Command::Watching(watching));
    }
}

/// The audio thread, at the top of a block: the engine, for this block only.
///
/// # Safety
/// `shell` is null or live; the audio thread only, one block at a time; the
/// pointer is not used after `sf_shell_end`.
#[no_mangle]
pub unsafe extern "C" fn sf_shell_begin(shell: *const SfShell) -> *mut SfCore {
    capi::begin(shell.as_ref().map(|s| &s.bridge))
}

/// The audio thread, at the end of a block of `frames`: publishes when a
/// frame is due.
///
/// # Safety
/// As `sf_shell_begin`, after it.
#[no_mangle]
pub unsafe extern "C" fn sf_shell_end(shell: *const SfShell, frames: u32) {
    if let Some(shell) = shell.as_ref() {
        shell.bridge.end(frames);
    }
}

/// THE PICTURE for `values` -- the host's, `count` of them in the product's
/// order (sf_param_count) -- into `out`. Not the audio thread: the curve and
/// the analyzer are worked out here. False, and nothing written, without a
/// shell, an `out` or a whole table.
///
/// # Safety
/// `shell` is null or live; `values` is null or holds `count` doubles;
/// `out` is null or writable.
#[no_mangle]
pub unsafe extern "C" fn sf_shell_read(shell: *const SfShell, values: *const f64, count: usize, out: *mut SfPicture) -> bool {
    let (Some(shell), Some(out)) = (shell.as_ref(), out.as_mut()) else {
        return false;
    };
    if values.is_null() || count != params::layout(shell.shape).len() {
        return false;
    }
    let values = values_of(shell.shape, std::slice::from_raw_parts(values, count));
    let target = target_of(shell.shape, &values);
    let Ok(mut reader) = shell.reader.lock() else { return false };
    let reader = &mut *reader;
    /* The analyzer follows the filter's lane: a new one is asked of the
     * engine, the bands start again from the floor, and only frames of that
     * lane are read into them. */
    let lane = lane_of(target.channel);
    if reader.analyzer.ask(lane) {
        shell.bridge.post(Command::Lane(lane));
    }
    let (serial, fs, watching, fresh) = shell.bridge.read(|r| {
        let f = r.frame;
        let fresh = reader.analyzer.offer(f.serial, f.watching, f.lane, &f.pre, &f.post);
        (f.serial, f.sample_rate, f.watching, fresh)
    });
    let fs = if fs > 0.0 { fs } else { sf_core::DEFAULT_SAMPLE_RATE };
    if fresh {
        reader.analyzer.analyse(serial, fs);
    }
    out.serial = serial;
    out.analyzer = watching;
    out.sample_rate = fs;
    for (h, hz) in out.hz.iter_mut().zip(reader.hz.iter()) {
        *h = *hz as f32;
    }
    let g = Globals { scale: 1.0, adaptive: if values[Field::AdaptiveQ.slot()] >= 0.5 { 1.0 } else { 0.0 } };
    let output = values[Field::Output.slot()] as f32;
    let c = reader.curve(target.settings, g, fs);
    out.curve_db = c.db;
    out.phase_deg = c.phase;
    for (t, d) in out.total_db.iter_mut().zip(c.db.iter()) {
        *t = d + output;
    }
    out.clamped = c.clamped;
    (out.handle_hz, out.handle_db) = c.handle.unwrap_or((f64::NAN, f64::NAN));
    out.edge_hz = c.edges.map_or([f64::NAN; 2], |(lo, hi)| [lo, hi]);
    out.pre_db = *reader.analyzer.pre();
    out.post_db = *reader.analyzer.post();
    true
}

/// An empty picture, for a shell to hold before its first read. Any thread.
///
/// # Safety
/// `out` is null or writable.
#[no_mangle]
pub unsafe extern "C" fn sf_picture_init(out: *mut SfPicture) {
    if let Some(out) = out.as_mut() {
        *out = SfPicture::empty();
    }
}

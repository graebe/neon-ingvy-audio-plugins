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
                 fd_shell_read                the picture, worked out here;
                                              a new lane is posted too
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

THE TOTAL is exact in every lane. The chain is linear, so at each frequency it
is a 2×2 transfer on the (left, right) pair: a Stereo node is H on both
halves, a Left or Right node H on its half, a Mid or Side node H on the mid
or the side, each in slot order after the isolator, then Output. The total
the editor asks for is what that lane's own input becomes at its output, the
other half silent:

```text
  LEFT, RIGHT   a source on that side alone        T_LL, T_RR
  MID           a centred source (L = R)           (T_LL + T_LR + T_RL + T_RR) / 2
  SIDE          the side alone (L = -R)            (T_LL - T_LR - T_RL + T_RR) / 2
  STEREO        a stereo node's view: the mid's
```

So a Mid filter shows in the Left lane as (1 + H)/2, as a source panned hard
left hears it, and a chain that mixes L/R with M/S nodes is drawn in the order
it plays (they do not commute).

THE ANALYZER: the input's and the output's spectrum in the lane drawn -- the
left, the right, the mid or the side; a stereo view's is the mid, the mono
sum -- as ni_analyzer::Analyzer works it out: dB per band of the picture's
frequencies, rising at once and falling over 300 ms. A read in another lane
than the last tells the engine (`Command::Lane`), whose rings start again
from empty, and the bands start again from the floor.
*/

use crate::{FdCore, FD_NODES};
use fd_core::design::{channel_of, design, edges, globals_of, handle, is_heard, settings_of, Channel, Globals, Settings, Shape};
use fd_core::{isolator, Lane, Param, PARAM_COUNT, RING};
use ni_analyzer::{Analyzer, FLOOR_DB};
use ni_filter::Complex64;
use ni_filter_node::picture::{angle, decibels};
use ni_dsp::ffi::drop_box;
use shell_core::{capi, Bridge, Model};
use std::sync::Mutex;

/// How many frequencies the picture has.
pub const FD_POINTS: usize = 512;
/// The picture's lowest and highest frequency, in Hz.
pub const FD_LOWEST_HZ: f64 = 10.0;
pub const FD_HIGHEST_HZ: f64 = 22_000.0;

/// The lanes a total can be drawn for (the module's notes say what each is).
pub const FD_LANE_LEFT: u32 = 0;
pub const FD_LANE_RIGHT: u32 = 1;
pub const FD_LANE_MID: u32 = 2;
pub const FD_LANE_SIDE: u32 = 3;
/// A Stereo node's view: the mid, what a centred source hears.
pub const FD_LANE_STEREO: u32 = 4;

/// The signal a lane's analyzer shows.
fn analyzed(lane: u32) -> Lane {
    match lane {
        FD_LANE_LEFT => Lane::Left,
        FD_LANE_RIGHT => Lane::Right,
        FD_LANE_SIDE => Lane::Side,
        _ => Lane::Mid,
    }
}

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

impl Model for FdCore {
    type Command = Command;
    type Frame = FdFrame;

    fn new_frame(&self) -> FdFrame {
        FdFrame {
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

    fn publish(&self, f: &mut FdFrame) {
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
    fn restore(&mut self, _: &FdFrame) {}
}

/* ------------------------------------------------------- the reader -- */

/// A node's curve, and what it was worked out from.
#[derive(Clone, Copy)]
struct Curve {
    from: (Settings, Globals, f64),
    /// The node's transfer at each of the picture's frequencies, and its dB.
    h: [Complex64; FD_POINTS],
    db: [f32; FD_POINTS],
    clamped: bool,
    handle: Option<(f64, f64)>,
    edges: Option<(f64, f64)>,
}

/// The editor thread's half: the analyzer, and every node's last curve.
struct Reader {
    hz: [f64; FD_POINTS],
    analyzer: Analyzer<FD_POINTS>,
    /// FD_NODES of them, on the heap: each holds a complex curve.
    curves: Vec<Option<Curve>>,
}

impl Reader {
    fn new() -> Reader {
        Reader { hz: frequencies(), analyzer: Analyzer::new(frequencies()), curves: vec![None; FD_NODES] }
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
            let mut h = [Complex64::new(1.0, 0.0); FD_POINTS];
            let mut db = [0.0f32; FD_POINTS];
            let mut clamped = false;
            let mut at = None;
            if s.shape != Shape::Empty {
                let (c, cl) = design(&s, None, g, fs);
                clamped = cl;
                for ((x, d), hz) in h.iter_mut().zip(db.iter_mut()).zip(hz.iter()) {
                    *x = c.response(angle(*hz, fs));
                    *d = decibels(*x);
                }
                at = handle(&s, g, &c, fs);
            }
            Curve { from, h, db, clamped, handle: at, edges: edges(&s) }
        })
    }
}

/// A 2×2 transfer on the (left, right) pair.
type Pair = [[Complex64; 2]; 2];

/// What a node of `channel` with the transfer `h` does to the pair: `h` on
/// the half it filters and 1 on the rest. A Mid node is L' = H·M + S,
/// R' = H·M - S; a Side node L' = M + H·S, R' = M - H·S; with M = (L + R)/2
/// and S = (L - R)/2.
fn on_pair(channel: Channel, h: Complex64) -> Pair {
    let (one, zero) = (Complex64::new(1.0, 0.0), Complex64::new(0.0, 0.0));
    let (a, b) = ((h + one) * 0.5, (h - one) * 0.5);
    match channel {
        Channel::Stereo => [[h, zero], [zero, h]],
        Channel::Left => [[h, zero], [zero, one]],
        Channel::Right => [[one, zero], [zero, h]],
        Channel::Mid => [[a, b], [b, a]],
        Channel::Side => [[a, -b], [-b, a]],
    }
}

/// `x` after `t`: the product x·t.
fn then(x: &Pair, t: &Pair) -> Pair {
    core::array::from_fn(|i| core::array::from_fn(|j| x[i][0] * t[0][j] + x[i][1] * t[1][j]))
}

/// What `lane`'s own input becomes through `t`, the other half silent.
fn through(t: &Pair, lane: u32) -> Complex64 {
    match lane {
        FD_LANE_LEFT => t[0][0],
        FD_LANE_RIGHT => t[1][1],
        FD_LANE_SIDE => (t[0][0] - t[0][1] - t[1][0] + t[1][1]) * 0.5,
        _ => (t[0][0] + t[0][1] + t[1][0] + t[1][1]) * 0.5,
    }
}

/// The picture for `values` (the host's, in table order) at the frame's
/// rate, the total in `lane`.
fn draw(reader: &mut Reader, values: &[f64], lane: u32, fs: f64, out: &mut FdPicture) {
    for (h, hz) in out.hz.iter_mut().zip(reader.hz.iter()) {
        *h = *hz as f32;
    }
    out.sample_rate = fs;
    let mut heard = [None; FD_NODES];
    for (slot, heard) in heard.iter_mut().enumerate() {
        let c = reader.curve(values, slot, fs);
        out.node_db[slot] = c.db;
        out.node_clamped[slot] = c.clamped;
        (out.handle_hz[slot], out.handle_db[slot]) = c.handle.unwrap_or((f64::NAN, f64::NAN));
        out.edge_hz[slot] = c.edges.map_or([f64::NAN; 2], |(lo, hi)| [lo, hi]);
        *heard = is_heard(values, slot as u8).then(|| channel_of(values, slot as u8));
    }
    /* The isolator: each band's path, at its level, summed as the signal
     * sums them -- complex, so the crossovers' phase is in the total. Then
     * the nodes in slot order, then Output. */
    let (paths, levels) = isolator::picture(values, fs);
    let iso_on = values[Param::IsoOn.index()] >= 0.5;
    let output = values[Param::Output.index()] as f32;
    for (i, hz) in reader.hz.iter().enumerate() {
        let w = angle(*hz, fs);
        let mut sum = Complex64::new(0.0, 0.0);
        for b in 0..3 {
            let h = paths[b][0].response(w) * paths[b][1].response(w) * levels[b];
            out.iso_db[b][i] = decibels(h);
            sum += h;
        }
        out.iso_total_db[i] = decibels(sum);
        let mut t = on_pair(Channel::Stereo, if iso_on { sum } else { Complex64::new(1.0, 0.0) });
        for (slot, channel) in heard.iter().enumerate() {
            if let (Some(channel), Some(c)) = (channel, &reader.curves[slot]) {
                t = then(&on_pair(*channel, c.h[i]), &t);
            }
        }
        out.total_db[i] = output + decibels(through(&t, lane));
    }
    out.pre_db = *reader.analyzer.pre();
    out.post_db = *reader.analyzer.post();
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
    /* The analyzer follows the lane drawn: a new one is asked of the engine,
     * the bands start again from the floor, and only frames of that lane are
     * read into them. */
    if reader.analyzer.ask(analyzed(lane)) {
        shell.bridge.post(Command::Lane(analyzed(lane)));
    }
    let (serial, fs, watching, fresh) = shell.bridge.read(|r| {
        let f = r.frame;
        let fresh = reader.analyzer.offer(f.serial, f.watching, f.lane, &f.pre, &f.post);
        (f.serial, f.sample_rate, f.watching, fresh)
    });
    let fs = if fs > 0.0 { fs } else { fd_core::DEFAULT_SAMPLE_RATE };
    if fresh {
        reader.analyzer.analyse(serial, fs);
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

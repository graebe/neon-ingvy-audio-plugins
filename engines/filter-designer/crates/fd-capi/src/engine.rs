// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
`fd_core.h`: the parameter table and one engine instance. lib.rs says what
the crate may do and which thread calls what.
*/

use std::os::raw::{c_char, c_int};

use fd_core::{Engine, Param, NODES, NODE_BASE, NODE_FIELDS, PARAM_COUNT};
use ni_dsp::ffi::cstr;
pub use ni_params::{NiParamInfo, NiParamKind};

/// How many nodes there are.
pub const FD_NODES: usize = 16;
/// The first node parameter's index, and how many each node has: node `n`'s
/// field `f` is `FD_NODE_BASE + FD_NODE_FIELDS · n + f`.
pub const FD_NODE_BASE: usize = 14;
pub const FD_NODE_FIELDS: usize = 10;
/// How many parameters there are.
pub const FD_PARAM_COUNT: usize = 174;
const _: () = assert!(FD_NODES == NODES && FD_NODE_BASE == NODE_BASE && FD_NODE_FIELDS == NODE_FIELDS);
const _: () = assert!(FD_PARAM_COUNT == PARAM_COUNT);

/* ------------------------------------------------------------ the instance */

/// One engine.
pub struct FdCore {
    pub(crate) engine: Engine,
    /// Samples processed, wrapping: what says a picture is new.
    pub(crate) serial: u32,
    /// Whether a window shows the analyzer (shell.rs).
    #[cfg_attr(not(feature = "shell"), allow(dead_code, reason = "the shell's"))]
    pub(crate) watching: bool,
}

impl FdCore {
    pub fn new(sample_rate: f64) -> FdCore {
        FdCore { engine: Engine::new(sample_rate), serial: 0, watching: false }
    }

    pub fn engine(&self) -> &Engine {
        &self.engine
    }

    pub fn engine_mut(&mut self) -> &mut Engine {
        &mut self.engine
    }

    /// One block in place.
    pub fn process(&mut self, left: &mut [f32], right: Option<&mut [f32]>) {
        self.engine.process_f32(left, right);
        self.serial = self.serial.wrapping_add(left.len() as u32);
    }
}

/// A new engine at `sample_rate` (44.1 kHz if that is not positive), every
/// parameter at its default. Allocates: not the audio thread.
#[no_mangle]
pub extern "C" fn fd_core_new(sample_rate: f64) -> *mut FdCore {
    Box::into_raw(Box::new(FdCore::new(sample_rate)))
}

/// # Safety
/// `core` is null or from `fd_core_new`, and not used after.
#[no_mangle]
pub unsafe extern "C" fn fd_core_free(core: *mut FdCore) {
    if !core.is_null() {
        drop(Box::from_raw(core));
    }
}

/// The host's rate. One that is not positive is ignored.
///
/// # Safety
/// `core` is null or live, and this thread's.
#[no_mangle]
pub unsafe extern "C" fn fd_core_set_sample_rate(core: *mut FdCore, sample_rate: f64) {
    if let Some(core) = core.as_mut() {
        if sample_rate > 0.0 {
            core.engine.set_sample_rate(sample_rate);
        }
    }
}

/// Set parameter `index` to `value`, in its own unit (fd_param_info),
/// clamped. NaN and an index out of range change nothing. Cheap when nothing
/// changes, so a shell may pass every parameter every block.
///
/// # Safety
/// `core` is null or live, and this thread's.
#[no_mangle]
pub unsafe extern "C" fn fd_core_set_num(core: *mut FdCore, index: i32, value: f64) {
    if let (Some(core), Some(p)) = (core.as_mut(), Param::from_i32(index)) {
        core.engine.set_num(p, value);
    }
}

/// Parameter `index`'s value as set; NaN for an index out of range or no
/// engine.
///
/// # Safety
/// `core` is null or live.
#[no_mangle]
pub unsafe extern "C" fn fd_core_num(core: *const FdCore, index: i32) -> f64 {
    match (core.as_ref(), Param::from_i32(index)) {
        (Some(core), Some(p)) => core.engine.num(p),
        _ => f64::NAN,
    }
}

/// One block of `frames` host floats, in place: `left`, and `right` for a
/// stereo track or null for a mono one.
///
/// # Safety
/// `core` is null or live, and this thread's; `left` is null or holds
/// `frames` floats, and so does `right` unless it is null; the two do not
/// overlap.
#[no_mangle]
pub unsafe extern "C" fn fd_core_process_f32(core: *mut FdCore, left: *mut f32, right: *mut f32, frames: u32) {
    let Some(core) = core.as_mut() else { return };
    if left.is_null() || frames == 0 {
        return;
    }
    let l = std::slice::from_raw_parts_mut(left, frames as usize);
    let r = (!right.is_null()).then(|| std::slice::from_raw_parts_mut(right, frames as usize));
    core.process(l, r);
}

/// One block of the Move's interleaved 16-bit stereo, `frames` frames, in
/// place.
///
/// # Safety
/// `core` is null or live, and this thread's; `buf` is null or holds
/// `2 * frames` values.
#[no_mangle]
pub unsafe extern "C" fn fd_core_process_i16(core: *mut FdCore, buf: *mut i16, frames: u32) {
    let Some(core) = core.as_mut() else { return };
    if buf.is_null() {
        return;
    }
    let b = std::slice::from_raw_parts_mut(buf, 2 * frames as usize);
    core.engine.process_i16(b, frames as usize);
}

/// One MIDI message of `len` bytes. CC 120 and CC 123, on any channel, are
/// the panic and reset the engine; nothing else does anything.
///
/// # Safety
/// `core` is null or live, and this thread's; `bytes` is null or holds `len`
/// bytes.
#[no_mangle]
pub unsafe extern "C" fn fd_core_on_midi(core: *mut FdCore, bytes: *const u8, len: usize) {
    let Some(core) = core.as_mut() else { return };
    if bytes.is_null() || len == 0 {
        return;
    }
    core.engine.on_midi(std::slice::from_raw_parts(bytes, len));
}

/// The panic: every filter empties, every fade lands; the settings stay.
///
/// # Safety
/// `core` is null or live, and this thread's.
#[no_mangle]
pub unsafe extern "C" fn fd_core_reset(core: *mut FdCore) {
    if let Some(core) = core.as_mut() {
        core.engine.reset();
    }
}

/// Whether every filter's state is empty: what a test of the panic asks.
///
/// # Safety
/// `core` is null or live.
#[no_mangle]
pub unsafe extern "C" fn fd_core_is_silent(core: *const FdCore) -> bool {
    core.as_ref().is_some_and(|c| c.engine.is_silent())
}

/* ------------------------------------------------------ the parameter table */

/* What a parameter is, in C, is ni-params' `NiParamInfo`, shared by every
 * product: build.rs names its source beside this file's. */

fn kind(index: i32) -> Option<ni_params::Kind> {
    Param::from_i32(index).map(|p| p.info().kind)
}

/// How many parameters there are.
#[no_mangle]
pub extern "C" fn fd_param_count() -> i32 {
    PARAM_COUNT as i32
}

/// Describe parameter `index`: its kind, range, default, resolution, and
/// whether a host may automate it. Any thread. False for an index out of
/// range.
///
/// # Safety
/// `out` is null or writable.
#[no_mangle]
pub unsafe extern "C" fn fd_param_info(index: i32, out: *mut NiParamInfo) -> bool {
    ni_params::c_info(kind(index), out)
}

/// Parameter `index`'s stable key ("n03_freq"), as a saved state and the
/// Move name it, into `out`. Returns its length, or -1 when the index is out
/// of range or `cap` too small (nothing written). Any thread.
///
/// # Safety
/// `out` is null or has room for `cap` bytes.
#[no_mangle]
pub unsafe extern "C" fn fd_param_key(index: i32, out: *mut c_char, cap: c_int) -> c_int {
    ni_params::c_text(Param::from_i32(index).map(|p| p.info().key), out, cap)
}

/// Parameter `index`'s display name ("Filter 3 Freq"), as `fd_param_key`.
///
/// # Safety
/// As `fd_param_key`.
#[no_mangle]
pub unsafe extern "C" fn fd_param_name(index: i32, out: *mut c_char, cap: c_int) -> c_int {
    ni_params::c_text(Param::from_i32(index).map(|p| p.info().name), out, cap)
}

/// Parameter `index`'s unit ("dB", "Hz", "%"; "" for Q and a choice), as
/// `fd_param_key`.
///
/// # Safety
/// As `fd_param_key`.
#[no_mangle]
pub unsafe extern "C" fn fd_param_unit(index: i32, out: *mut c_char, cap: c_int) -> c_int {
    ni_params::c_text(Param::from_i32(index).map(|p| p.info().unit.map_or("", |u| u.label())), out, cap)
}

/// Choice parameter `index`'s label number `option` ("Notch", "Elliptic",
/// "24 dB"), as `fd_param_key`; -1 for a number or an option out of range.
///
/// # Safety
/// As `fd_param_key`.
#[no_mangle]
pub unsafe extern "C" fn fd_param_label(index: i32, option: i32, out: *mut c_char, cap: c_int) -> c_int {
    ni_params::c_text(kind(index).and_then(|k| k.label(option)), out, cap)
}

/// `value` of parameter `index` as every shell prints it ("+3.0 dB",
/// "1.20 kHz", "-inf dB", "Notch"), as `fd_param_key`.
///
/// # Safety
/// As `fd_param_key`.
#[no_mangle]
pub unsafe extern "C" fn fd_param_text(index: i32, value: f64, out: *mut c_char, cap: c_int) -> c_int {
    ni_params::c_written(Param::from_i32(index).map(|p| move |w: &mut dyn core::fmt::Write| p.write_value(w, value)), out, cap)
}

/// What a user typed, read as parameter `index`'s value into `out`, clamped
/// to its range ("1.2k", "-6", "notch"). False, and nothing written, for
/// text with no value in it. Any thread.
///
/// # Safety
/// `text` is null or a NUL-terminated string; `out` is null or writable.
#[no_mangle]
pub unsafe extern "C" fn fd_param_parse(index: i32, text: *const c_char, out: *mut f64) -> bool {
    let (Some(p), Some(out)) = (Param::from_i32(index), out.as_mut()) else {
        return false;
    };
    match p.parse_value(cstr(text)) {
        Some(v) => {
            *out = v;
            true
        }
        None => false,
    }
}

/// Parameter `index`'s `value` as a fraction 0..1 of its knob's travel; 0 for
/// an index out of range. Any thread.
#[no_mangle]
pub extern "C" fn fd_param_normalise(index: i32, value: f64) -> f64 {
    kind(index).map_or(0.0, |k| k.normalise(value))
}

/// `value` on parameter `index`'s resolution, inside its range. 0 for an
/// index out of range. Any thread.
#[no_mangle]
pub extern "C" fn fd_param_snap(index: i32, value: f64) -> f64 {
    kind(index).map_or(0.0, |k| k.snap(value))
}

/// The value at fraction `normalised` of parameter `index`'s travel: the
/// inverse of `fd_param_normalise`. 0 for an index out of range.
#[no_mangle]
pub extern "C" fn fd_param_denormalise(index: i32, normalised: f64) -> f64 {
    kind(index).map_or(0.0, |k| k.denormalise(normalised))
}

/* ------------------------------------------------- what a node's type uses */

/// What a node of type `type_` (0 Empty .. 9 Tilt) at order index `order`
/// (0..7) uses, as bits: 1 Family, 2 Order, 4 Gain, 8 Q -- what an editor
/// draws enabled. Ripple and Stopband follow the family
/// (fd_family_uses_ripple, _stopband). Any thread; pure.
#[no_mangle]
pub extern "C" fn fd_type_uses(type_: i32, order: i32) -> u32 {
    use fd_core::design::Shape;
    let shape = Shape::from_value(f64::from(type_));
    let order = (order.max(0) as usize + 1).clamp(1, shape.max_order().max(1));
    (shape.uses_family() as u32) | ((shape.max_order() > 0) as u32) << 1 | (shape.uses_gain() as u32) << 2 | (shape.uses_q(order) as u32) << 3
}

/// The Order index (0..7) a node of type `type_` in family `family` (0..5)
/// at Order index `order` plays: at most the type's highest, and a
/// Linkwitz-Riley high-pass or low-pass at the even order at or above it.
/// What the editor snaps a choice of Order or Family to. Any thread; pure.
#[no_mangle]
pub extern "C" fn fd_order_played(type_: i32, family: i32, order: i32) -> i32 {
    use fd_core::design::{order_played, Shape};
    let family = ni_filter::Family::from_index(family.max(0) as usize);
    order_played(Shape::from_value(f64::from(type_)), family, order.max(0) as usize) as i32
}

/// Whether family `family` (0..5) takes Ripple.
#[no_mangle]
pub extern "C" fn fd_family_uses_ripple(family: i32) -> bool {
    ni_filter::Family::from_index(family.max(0) as usize).uses_ripple()
}

/// Whether family `family` (0..5) takes Stopband.
#[no_mangle]
pub extern "C" fn fd_family_uses_stopband(family: i32) -> bool {
    ni_filter::Family::from_index(family.max(0) as usize).uses_stopband()
}

/// THE TAIL for `values` -- the host's, `count` of them in table order
/// (FD_PARAM_COUNT) -- at `sample_rate`: the seconds the output goes on
/// once the input stops, until 60 dB down, at most 30. 0 without a whole
/// table or a rate. Any thread but the audio one; pure.
///
/// # Safety
/// `values` is null or holds `count` doubles.
#[no_mangle]
pub unsafe extern "C" fn fd_tail_seconds(values: *const f64, count: usize, sample_rate: f64) -> f64 {
    if values.is_null() || count != PARAM_COUNT {
        return 0.0;
    }
    fd_core::design::tail_seconds(std::slice::from_raw_parts(values, count), sample_rate)
}

/// The Q whose -3 dB edge sits at `edge_hz` for a centre at `freq` (a Bell, a
/// band-pass, a notch): what dragging an edge sets. Any thread; pure.
#[no_mangle]
pub extern "C" fn fd_q_for_edge(freq: f64, edge_hz: f64) -> f64 {
    if freq > 0.0 && edge_hz > 0.0 {
        fd_core::design::q_for_edge(freq, edge_hz)
    } else {
        core::f64::consts::FRAC_1_SQRT_2
    }
}

/// The type (0 Empty .. 9 Tilt) a filter added at `hz` is. Any thread; pure.
#[no_mangle]
pub extern "C" fn fd_type_for_new(hz: f64) -> i32 {
    fd_core::design::shape_for_new(hz) as i32
}

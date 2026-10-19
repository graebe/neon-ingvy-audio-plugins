// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
`sf_core.h`: each product's parameter table and one engine instance. lib.rs
says what the crate may do and which thread calls what.
*/

use std::os::raw::{c_char, c_int};

use ni_dsp::ffi::{byte_slice, cstr, drop_box, f32_block, i16_block, usable_rate};
use ni_filter::Family;
pub use ni_params::{NiParamInfo, NiParamKind};
use sf_core::params::{self, Field};
use sf_core::{Engine, Lane, Shape, FIELDS};

/* --------------------------------------------------------- the products */

/* A product is its filter's shape, numbered as NI Filter Designer's Type
 * parameter numbers them (0 is Empty, which is none). */

/// NI Bell.
pub const SF_SHAPE_BELL: u32 = 1;
/// NI Low Shelf.
pub const SF_SHAPE_LOW_SHELF: u32 = 2;
/// NI High Shelf.
pub const SF_SHAPE_HIGH_SHELF: u32 = 3;
/// NI High Pass.
pub const SF_SHAPE_HIGH_PASS: u32 = 4;
/// NI Low Pass.
pub const SF_SHAPE_LOW_PASS: u32 = 5;
/// NI Band Pass.
pub const SF_SHAPE_BAND_PASS: u32 = 6;
/// NI Notch.
pub const SF_SHAPE_NOTCH: u32 = 7;
/// NI All Pass.
pub const SF_SHAPE_ALL_PASS: u32 = 8;
/// NI Tilt.
pub const SF_SHAPE_TILT: u32 = 9;

/// The product numbered `shape` (SF_SHAPE_*), if there is one.
pub fn shape_of(shape: u32) -> Option<Shape> {
    match Shape::ALL.get(shape as usize) {
        Some(Shape::Empty) | None => None,
        Some(&s) => Some(s),
    }
}

/* ----------------------------------------------------------- the fields */

/* Every field a single filter can have; sf_field_index says where a product
 * declares one, if it does. */

pub const SF_FIELD_FREQ: u32 = 0;
pub const SF_FIELD_GAIN: u32 = 1;
pub const SF_FIELD_Q: u32 = 2;
pub const SF_FIELD_ADAPTIVE_Q: u32 = 3;
pub const SF_FIELD_ORDER: u32 = 4;
pub const SF_FIELD_FAMILY: u32 = 5;
pub const SF_FIELD_RIPPLE: u32 = 6;
pub const SF_FIELD_STOPBAND: u32 = 7;
pub const SF_FIELD_MATCHED: u32 = 8;
pub const SF_FIELD_CHANNEL: u32 = 9;
pub const SF_FIELD_OUTPUT: u32 = 10;
pub const SF_FIELD_ANALYZER: u32 = 11;
/// How many fields there are.
pub const SF_FIELDS: usize = 12;
const _: () = assert!(SF_FIELDS == FIELDS);
const _: () = assert!(Field::Analyzer.slot() == SF_FIELD_ANALYZER as usize && Field::Matched.slot() == SF_FIELD_MATCHED as usize);

/// The most parameters any product has: what a shell sizes for.
pub const SF_PARAM_MAX: usize = 10;

fn field_of(field: u32) -> Option<Field> {
    Field::ALL.get(field as usize).copied()
}

/// Host values, in `shape`'s order (`count` of them), as an engine keeps
/// values: by field, a field the product lacks at its default.
pub fn values_of(shape: Shape, host: &[f64]) -> [f64; FIELDS] {
    let mut v = params::defaults(shape);
    for (&f, &x) in params::layout(shape).iter().zip(host) {
        v[f.slot()] = params::info(shape, f).kind.clamp(x);
    }
    v
}

/* ------------------------------------------------------------ the instance */

/// One engine.
pub struct SfCore {
    pub(crate) engine: Engine,
    /// Samples processed, wrapping: what says a picture is new.
    pub(crate) serial: u32,
    /// Whether a window shows the analyzer (shell.rs).
    #[cfg_attr(not(feature = "shell"), allow(dead_code, reason = "the shell's"))]
    pub(crate) watching: bool,
    /// The lane the analyzer keeps (shell.rs).
    #[cfg_attr(not(feature = "shell"), allow(dead_code, reason = "the shell's"))]
    pub(crate) lane: Lane,
}

impl SfCore {
    /// A new engine for `shape`; `None` for Empty.
    pub fn new(shape: Shape, sample_rate: f64) -> Option<SfCore> {
        Engine::new(shape, sample_rate).map(|engine| SfCore { engine, serial: 0, watching: false, lane: Lane::Mid })
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

    /// One block heard and left alone (Engine::listen_f32).
    pub fn listen(&mut self, left: &[f32], right: Option<&[f32]>) {
        self.engine.listen_f32(left, right);
        self.serial = self.serial.wrapping_add(left.len() as u32);
    }

    fn field(&self, index: i32) -> Option<Field> {
        usize::try_from(index).ok().and_then(|i| params::field_at(self.engine.shape(), i))
    }
}

/// A new engine for product `shape` (SF_SHAPE_*) at `sample_rate` (44.1 kHz
/// if that is not positive), every parameter at its default; null for a
/// shape that is no product. Allocates: not the audio thread.
#[no_mangle]
pub extern "C" fn sf_core_new(shape: u32, sample_rate: f64) -> *mut SfCore {
    match shape_of(shape).and_then(|s| SfCore::new(s, sample_rate)) {
        Some(core) => Box::into_raw(Box::new(core)),
        None => core::ptr::null_mut(),
    }
}

/// # Safety
/// `core` is null or from `sf_core_new`, and not used after.
#[no_mangle]
pub unsafe extern "C" fn sf_core_free(core: *mut SfCore) {
    drop_box(core);
}

/// The host's rate. One that is not positive is ignored.
///
/// # Safety
/// `core` is null or live, and this thread's.
#[no_mangle]
pub unsafe extern "C" fn sf_core_set_sample_rate(core: *mut SfCore, sample_rate: f64) {
    if let (Some(core), Some(rate)) = (core.as_mut(), usable_rate(sample_rate)) {
        core.engine.set_sample_rate(rate);
    }
}

/// Set parameter `index` of the engine's product to `value`, in its own unit
/// (sf_param_info), clamped. NaN and an index out of range change nothing.
/// Cheap when nothing changes.
///
/// # Safety
/// `core` is null or live, and this thread's.
#[no_mangle]
pub unsafe extern "C" fn sf_core_set_num(core: *mut SfCore, index: i32, value: f64) {
    if let Some(core) = core.as_mut() {
        if let Some(f) = core.field(index) {
            core.engine.set(f, value);
        }
    }
}

/// Parameter `index`'s value as set; NaN for an index out of range or no
/// engine.
///
/// # Safety
/// `core` is null or live.
#[no_mangle]
pub unsafe extern "C" fn sf_core_num(core: *const SfCore, index: i32) -> f64 {
    match core.as_ref() {
        Some(core) => core.field(index).map_or(f64::NAN, |f| core.engine.get(f)),
        None => f64::NAN,
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
pub unsafe extern "C" fn sf_core_process_f32(core: *mut SfCore, left: *mut f32, right: *mut f32, frames: u32) {
    if let (Some(core), Some((l, r))) = (core.as_mut(), f32_block(left, right, frames)) {
        core.process(l, r);
    }
}

/// One block of `frames` host floats heard and left alone: nothing is
/// filtered, the analyzer keeps it as its input and its output. What a
/// bypassed shell runs once its engine idles; `sf_core_reset` before the next
/// `sf_core_process_f32`.
///
/// # Safety
/// As `sf_core_process_f32`.
#[no_mangle]
pub unsafe extern "C" fn sf_core_listen_f32(core: *mut SfCore, left: *mut f32, right: *mut f32, frames: u32) {
    if let (Some(core), Some((l, r))) = (core.as_mut(), f32_block(left, right, frames)) {
        core.listen(l, r.as_deref());
    }
}

/// One block of the Move's interleaved 16-bit stereo, `frames` frames, in
/// place.
///
/// # Safety
/// `core` is null or live, and this thread's; `buf` is null or holds
/// `2 * frames` values.
#[no_mangle]
pub unsafe extern "C" fn sf_core_process_i16(core: *mut SfCore, buf: *mut i16, frames: u32) {
    if let (Some(core), Some(b)) = (core.as_mut(), i16_block(buf, frames)) {
        core.engine.process_i16(b, frames as usize);
    }
}

/// One MIDI message of `len` bytes. CC 120 and CC 123, on any channel, are
/// the panic and reset the engine; nothing else does anything.
///
/// # Safety
/// `core` is null or live, and this thread's; `bytes` is null or holds `len`
/// bytes.
#[no_mangle]
pub unsafe extern "C" fn sf_core_on_midi(core: *mut SfCore, bytes: *const u8, len: usize) {
    if let (Some(core), Some(msg)) = (core.as_mut(), byte_slice(bytes, len)) {
        core.engine.on_midi(msg);
    }
}

/// The panic: the filter empties, a fade lands; the settings stay.
///
/// # Safety
/// `core` is null or live, and this thread's.
#[no_mangle]
pub unsafe extern "C" fn sf_core_reset(core: *mut SfCore) {
    if let Some(core) = core.as_mut() {
        core.engine.reset();
    }
}

/// Whether the filter's state is empty: what a test of the panic asks.
///
/// # Safety
/// `core` is null or live.
#[no_mangle]
pub unsafe extern "C" fn sf_core_is_silent(core: *const SfCore) -> bool {
    core.as_ref().is_some_and(|c| c.engine.is_silent())
}

/* ------------------------------------------------------ the parameter table */

/* Each product's table: the same entry points as every product's, with the
 * product first. What a parameter is, in C, is ni-params' `NiParamInfo`. */

fn param(shape: u32, index: i32) -> Option<(Shape, Field)> {
    let s = shape_of(shape)?;
    let f = params::field_at(s, usize::try_from(index).ok()?)?;
    Some((s, f))
}

fn info(shape: u32, index: i32) -> Option<params::Info> {
    param(shape, index).map(|(s, f)| params::info(s, f))
}

fn kind(shape: u32, index: i32) -> Option<ni_params::Kind> {
    info(shape, index).map(|i| i.kind)
}

/// How many parameters product `shape` has; 0 for none.
#[no_mangle]
pub extern "C" fn sf_param_count(shape: u32) -> i32 {
    shape_of(shape).map_or(0, |s| params::layout(s).len() as i32)
}

/// Where product `shape` declares field `field` (SF_FIELD_*): its parameter
/// index, or -1 when the product has no such parameter.
#[no_mangle]
pub extern "C" fn sf_field_index(shape: u32, field: u32) -> i32 {
    match (shape_of(shape), field_of(field)) {
        (Some(s), Some(f)) => params::index_of(s, f).map_or(-1, |i| i as i32),
        _ => -1,
    }
}

/// Describe parameter `index` of product `shape`: its kind, range, default,
/// resolution, and whether a host may automate it. Any thread. False for an
/// index out of range.
///
/// # Safety
/// `out` is null or writable.
#[no_mangle]
pub unsafe extern "C" fn sf_param_info(shape: u32, index: i32, out: *mut NiParamInfo) -> bool {
    ni_params::c_info(kind(shape, index), out)
}

/// Parameter `index`'s stable key ("freq"), as a saved state and the Move
/// name it, into `out`. Returns its length, or -1 when the index is out of
/// range or `cap` too small (nothing written). Any thread.
///
/// # Safety
/// `out` is null or has room for `cap` bytes.
#[no_mangle]
pub unsafe extern "C" fn sf_param_key(shape: u32, index: i32, out: *mut c_char, cap: c_int) -> c_int {
    ni_params::c_text(info(shape, index).map(|i| i.key), out, cap)
}

/// Parameter `index`'s display name ("Freq", "Slope"), as `sf_param_key`.
///
/// # Safety
/// As `sf_param_key`.
#[no_mangle]
pub unsafe extern "C" fn sf_param_name(shape: u32, index: i32, out: *mut c_char, cap: c_int) -> c_int {
    ni_params::c_text(info(shape, index).map(|i| i.name), out, cap)
}

/// Parameter `index`'s unit ("dB", "Hz"; "" for Q and a choice), as
/// `sf_param_key`.
///
/// # Safety
/// As `sf_param_key`.
#[no_mangle]
pub unsafe extern "C" fn sf_param_unit(shape: u32, index: i32, out: *mut c_char, cap: c_int) -> c_int {
    ni_params::c_text(info(shape, index).map(|i| i.unit.map_or("", |u| u.label())), out, cap)
}

/// Choice parameter `index`'s label number `option` ("Elliptic",
/// "24 dB/oct"), as `sf_param_key`; -1 for a number or an option out of
/// range.
///
/// # Safety
/// As `sf_param_key`.
#[no_mangle]
pub unsafe extern "C" fn sf_param_label(shape: u32, index: i32, option: i32, out: *mut c_char, cap: c_int) -> c_int {
    ni_params::c_text(kind(shape, index).and_then(|k| k.label(option)), out, cap)
}

/// `value` of parameter `index` as every shell prints it ("+3.0 dB",
/// "1.20 kHz", "Elliptic"), as `sf_param_key`.
///
/// # Safety
/// As `sf_param_key`.
#[no_mangle]
pub unsafe extern "C" fn sf_param_text(shape: u32, index: i32, value: f64, out: *mut c_char, cap: c_int) -> c_int {
    ni_params::c_written(param(shape, index).map(|(s, f)| move |w: &mut dyn core::fmt::Write| params::write_value(s, f, w, value)), out, cap)
}

/// What a user typed, read as parameter `index`'s value into `out`, clamped
/// to its range ("1.2k", "-6", "elliptic"). False, and nothing written, for
/// text with no value in it. Any thread.
///
/// # Safety
/// `text` is null or a NUL-terminated string; `out` is null or writable.
#[no_mangle]
pub unsafe extern "C" fn sf_param_parse(shape: u32, index: i32, text: *const c_char, out: *mut f64) -> bool {
    let (Some((s, f)), Some(out)) = (param(shape, index), out.as_mut()) else {
        return false;
    };
    match params::parse_value(s, f, cstr(text)) {
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
pub extern "C" fn sf_param_normalise(shape: u32, index: i32, value: f64) -> f64 {
    kind(shape, index).map_or(0.0, |k| k.normalise(value))
}

/// `value` on parameter `index`'s resolution, inside its range. 0 for an
/// index out of range. Any thread.
#[no_mangle]
pub extern "C" fn sf_param_snap(shape: u32, index: i32, value: f64) -> f64 {
    kind(shape, index).map_or(0.0, |k| k.snap(value))
}

/// The value at fraction `normalised` of parameter `index`'s travel: the
/// inverse of `sf_param_normalise`. 0 for an index out of range.
#[no_mangle]
pub extern "C" fn sf_param_denormalise(shape: u32, index: i32, normalised: f64) -> f64 {
    kind(shape, index).map_or(0.0, |k| k.denormalise(normalised))
}

/* ---------------------------------------------------- what the editor asks */

/// Q does something (at this order).
pub const SF_USES_Q: u32 = 1;
/// The family takes a passband ripple.
pub const SF_USES_RIPPLE: u32 = 2;
/// The family takes a stopband depth.
pub const SF_USES_STOPBAND: u32 = 4;

/// What product `shape` at order index `order` (0..7) in family index
/// `family` uses right now, as SF_USES_* bits: what an editor draws enabled
/// of the parameters the product has. Any thread; pure.
#[no_mangle]
pub extern "C" fn sf_uses(shape: u32, order: i32, family: i32) -> u32 {
    let Some(s) = shape_of(shape) else { return 0 };
    let f = Family::from_index(family.max(0) as usize);
    let mut bits = 0;
    if s.uses_q(order.max(0) as usize + 1) {
        bits |= SF_USES_Q;
    }
    if s.uses_family() && f.uses_ripple() {
        bits |= SF_USES_RIPPLE;
    }
    if s.uses_family() && f.uses_stopband() {
        bits |= SF_USES_STOPBAND;
    }
    bits
}

/// The order index product `shape` in family index `family` plays at order
/// index `order`: what an editor snaps a slope to (a Linkwitz-Riley cut is
/// even). Any thread; pure.
#[no_mangle]
pub extern "C" fn sf_order_played(shape: u32, family: i32, order: i32) -> i32 {
    let Some(s) = shape_of(shape) else { return 0 };
    ni_filter_node::design::order_played(s, Family::from_index(family.max(0) as usize), order.max(0) as usize) as i32
}

/// The Q whose -3 dB edge sits at `edge_hz` for a centre at `freq`: what a
/// dragged edge sets. Any thread; pure.
#[no_mangle]
pub extern "C" fn sf_q_for_edge(freq: f64, edge_hz: f64) -> f64 {
    ni_filter_node::design::q_for_edge(freq, edge_hz)
}

/// THE TAIL of product `shape` at `values` (the host's, `count` of them in
/// its order) at `sample_rate`: seconds until it is 60 dB down, at most 30.
/// 0 without a whole table. Any thread, not the audio thread: it designs.
///
/// # Safety
/// `values` is null or holds `count` doubles.
#[no_mangle]
pub unsafe extern "C" fn sf_tail_seconds(shape: u32, values: *const f64, count: usize, sample_rate: f64) -> f64 {
    let Some(s) = shape_of(shape) else { return 0.0 };
    if values.is_null() || count != params::layout(s).len() {
        return 0.0;
    }
    sf_core::tail_seconds(s, &values_of(s, std::slice::from_raw_parts(values, count)), sample_rate)
}

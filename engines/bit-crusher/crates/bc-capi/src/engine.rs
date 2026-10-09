// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
`bc_core.h`: the parameter table, the steps as a curve, and one engine
instance. lib.rs says what the crate may do and which thread calls what.
*/

use std::os::raw::{c_char, c_int};

use bc_core::{transfer, Engine, Kind, Param, Travel, PARAM_COUNT};
use ni_dsp::ffi::{copy_cstr, cstr};

/// How many samples an instance keeps for the scope.
pub const BC_SCOPE_RING: usize = 2048;

/* ------------------------------------------------------------ the instance */

/// One engine, and the last samples it processed.
pub struct BcCore {
    pub(crate) engine: Engine,
    dry: Box<[f32; BC_SCOPE_RING]>,
    wet: Box<[f32; BC_SCOPE_RING]>,
    /// Where the next sample goes.
    pos: usize,
    /// Samples processed, wrapping: what says a scope is new.
    pub(crate) serial: u32,
}

impl BcCore {
    pub fn new(sample_rate: f64) -> BcCore {
        BcCore {
            engine: Engine::new(sample_rate),
            dry: Box::new([0.0; BC_SCOPE_RING]),
            wet: Box::new([0.0; BC_SCOPE_RING]),
            pos: 0,
            serial: 0,
        }
    }

    pub fn engine(&self) -> &Engine {
        &self.engine
    }

    pub fn engine_mut(&mut self) -> &mut Engine {
        &mut self.engine
    }

    /// One block in place, the left channel's dry and wet kept for the scope.
    pub fn process(&mut self, left: &mut [f32], right: Option<&mut [f32]>) {
        /* Only the last ring's worth of a long block can be shown. */
        let keep = left.len().min(BC_SCOPE_RING);
        let from = left.len() - keep;
        let start = (self.pos + from) % BC_SCOPE_RING;
        for (i, x) in left[from..].iter().enumerate() {
            self.dry[(start + i) % BC_SCOPE_RING] = *x;
        }
        self.engine.process_f32(left, right);
        for (i, y) in left[from..].iter().enumerate() {
            self.wet[(start + i) % BC_SCOPE_RING] = *y;
        }
        self.pos = (self.pos + left.len()) % BC_SCOPE_RING;
        self.serial = self.serial.wrapping_add(left.len() as u32);
    }

    /// The last `n` samples (at most the ring), oldest first, as (dry, wet),
    /// by age: `at(0)` is the oldest of them.
    pub fn recent(&self, n: usize) -> impl Iterator<Item = (f32, f32)> + '_ {
        let n = n.min(BC_SCOPE_RING);
        let first = (self.pos + BC_SCOPE_RING - n) % BC_SCOPE_RING;
        (0..n).map(move |i| {
            let k = (first + i) % BC_SCOPE_RING;
            (self.dry[k], self.wet[k])
        })
    }
}

/// A new engine at `sample_rate` (44.1 kHz if that is not positive), every
/// parameter at its default. Allocates: not the audio thread.
#[no_mangle]
pub extern "C" fn bc_core_new(sample_rate: f64) -> *mut BcCore {
    Box::into_raw(Box::new(BcCore::new(sample_rate)))
}

/// # Safety
/// `core` is null or from `bc_core_new`, and not used after.
#[no_mangle]
pub unsafe extern "C" fn bc_core_free(core: *mut BcCore) {
    if !core.is_null() {
        drop(Box::from_raw(core));
    }
}

/// The host's rate. One that is not positive is ignored.
///
/// # Safety
/// `core` is null or live, and this thread's.
#[no_mangle]
pub unsafe extern "C" fn bc_core_set_sample_rate(core: *mut BcCore, sample_rate: f64) {
    if let Some(core) = core.as_mut() {
        if sample_rate > 0.0 {
            core.engine.set_sample_rate(sample_rate);
        }
    }
}

/// Set parameter `index` to `value`, in its own unit (bc_param_info),
/// clamped. NaN and an index out of range change nothing. Cheap when nothing
/// changes, so a shell may pass every parameter every block.
///
/// # Safety
/// `core` is null or live, and this thread's.
#[no_mangle]
pub unsafe extern "C" fn bc_core_set_num(core: *mut BcCore, index: i32, value: f64) {
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
pub unsafe extern "C" fn bc_core_num(core: *const BcCore, index: i32) -> f64 {
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
pub unsafe extern "C" fn bc_core_process_f32(core: *mut BcCore, left: *mut f32, right: *mut f32, frames: u32) {
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
pub unsafe extern "C" fn bc_core_process_i16(core: *mut BcCore, buf: *mut i16, frames: u32) {
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
pub unsafe extern "C" fn bc_core_on_midi(core: *mut BcCore, bytes: *const u8, len: usize) {
    let Some(core) = core.as_mut() else { return };
    if bytes.is_null() || len == 0 {
        return;
    }
    core.engine.on_midi(std::slice::from_raw_parts(bytes, len));
}

/// The panic: everything held lets go and every filter empties; the
/// settings stay.
///
/// # Safety
/// `core` is null or live, and this thread's.
#[no_mangle]
pub unsafe extern "C" fn bc_core_reset(core: *mut BcCore) {
    if let Some(core) = core.as_mut() {
        core.engine.reset();
    }
}

/* ------------------------------------------------------ the parameter table */

/// What a parameter is.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum BcParamKind {
    /// A number from `min` to `max` in its unit.
    Float = 0,
    /// Off (0) or on (1).
    Toggle = 1,
}

/// What a shell declares one parameter with. Values are in the parameter's
/// own unit (bc_param_unit); a toggle's range is 0..1.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct BcParamInfo {
    pub kind: BcParamKind,
    pub min: f64,
    pub max: f64,
    pub default_value: f64,
    /// True when the knob travels in equal ratios (bc_param_normalise).
    pub log_travel: bool,
    /// True when the value runs both ways about zero.
    pub bipolar: bool,
}

/// How many parameters there are.
#[no_mangle]
pub extern "C" fn bc_param_count() -> i32 {
    PARAM_COUNT as i32
}

/// Describe parameter `index`. Any thread. False for an index out of range.
///
/// # Safety
/// `out` is null or writable.
#[no_mangle]
pub unsafe extern "C" fn bc_param_info(index: i32, out: *mut BcParamInfo) -> bool {
    let (Some(p), Some(out)) = (Param::from_i32(index), out.as_mut()) else {
        return false;
    };
    *out = match p.info().kind {
        Kind::Float { min, max, default, travel, bipolar, .. } => BcParamInfo {
            kind: BcParamKind::Float,
            min,
            max,
            default_value: default,
            log_travel: travel == Travel::Log,
            bipolar,
        },
        Kind::Toggle { default } => BcParamInfo {
            kind: BcParamKind::Toggle,
            min: 0.0,
            max: 1.0,
            default_value: default as i32 as f64,
            log_travel: false,
            bipolar: false,
        },
    };
    true
}

/// Parameter `index`'s stable key, as a saved state and the Move name it,
/// into `out`. Returns its length, or -1 when the index is out of range or
/// `cap` too small (nothing written). Any thread.
///
/// # Safety
/// `out` is null or has room for `cap` bytes.
#[no_mangle]
pub unsafe extern "C" fn bc_param_key(index: i32, out: *mut c_char, cap: c_int) -> c_int {
    match Param::from_i32(index) {
        Some(p) => copy_cstr(p.info().key, out, cap),
        None => -1,
    }
}

/// Parameter `index`'s display name, as `bc_param_key`.
///
/// # Safety
/// As `bc_param_key`.
#[no_mangle]
pub unsafe extern "C" fn bc_param_name(index: i32, out: *mut c_char, cap: c_int) -> c_int {
    match Param::from_i32(index) {
        Some(p) => copy_cstr(p.info().name, out, cap),
        None => -1,
    }
}

/// Parameter `index`'s unit ("dB", "bits", "%", "Hz", "oct"; "" for a
/// toggle), as `bc_param_key`.
///
/// # Safety
/// As `bc_param_key`.
#[no_mangle]
pub unsafe extern "C" fn bc_param_unit(index: i32, out: *mut c_char, cap: c_int) -> c_int {
    match Param::from_i32(index) {
        Some(p) => match p.info().kind {
            Kind::Float { unit, .. } => copy_cstr(unit.label(), out, cap),
            Kind::Toggle { .. } => copy_cstr("", out, cap),
        },
        None => -1,
    }
}

/// `value` of parameter `index` as every shell prints it ("+3.0 dB",
/// "12.0 kHz", "On"), as `bc_param_key`.
///
/// # Safety
/// As `bc_param_key`.
#[no_mangle]
pub unsafe extern "C" fn bc_param_text(index: i32, value: f64, out: *mut c_char, cap: c_int) -> c_int {
    let Some(p) = Param::from_i32(index) else { return -1 };
    let mut text = TextBuf::default();
    if p.write_value(&mut text, value).is_err() {
        return -1;
    }
    copy_cstr(text.as_str(), out, cap)
}

/// What a user typed, read as parameter `index`'s value into `out`, clamped
/// to its range. False, and nothing written, for text with no value in it.
/// Any thread.
///
/// # Safety
/// `text` is null or a NUL-terminated string; `out` is null or writable.
#[no_mangle]
pub unsafe extern "C" fn bc_param_parse(index: i32, text: *const c_char, out: *mut f64) -> bool {
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
pub extern "C" fn bc_param_normalise(index: i32, value: f64) -> f64 {
    Param::from_i32(index).map_or(0.0, |p| p.normalise(value))
}

/// The value at fraction `normalised` of parameter `index`'s travel: the
/// inverse of `bc_param_normalise`. 0 for an index out of range.
#[no_mangle]
pub extern "C" fn bc_param_denormalise(index: i32, normalised: f64) -> f64 {
    Param::from_i32(index).map_or(0.0, |p| p.denormalise(normalised))
}

/// A short text on the stack, for a value before it is copied out.
#[derive(Default)]
struct TextBuf {
    buf: [u8; 32],
    len: usize,
}

impl TextBuf {
    fn as_str(&self) -> &str {
        core::str::from_utf8(&self.buf[..self.len]).unwrap_or("")
    }
}

impl core::fmt::Write for TextBuf {
    fn write_str(&mut self, s: &str) -> core::fmt::Result {
        let end = self.len + s.len();
        let dst = self.buf.get_mut(self.len..end).ok_or(core::fmt::Error)?;
        dst.copy_from_slice(s.as_bytes());
        self.len = end;
        Ok(())
    }
}

/* ------------------------------------------------------- the steps, drawn */

/// THE STEPS AS A CURVE, for an editor's plot: `count` outputs for inputs
/// evenly spaced from -1 to 1 (both included), through Drive (dB), the grid
/// of `bits`, Shape (%) and mid-rise or not -- the engine's own staircase,
/// without the noise and the time. Any thread; pure.
///
/// # Safety
/// `out` is null or has room for `count` floats.
#[no_mangle]
pub unsafe extern "C" fn bc_transfer_render(
    drive_db: f64,
    bits: f64,
    shape_pct: f64,
    mid_rise: bool,
    out: *mut f32,
    count: u32,
) {
    if out.is_null() || count == 0 {
        return;
    }
    let out = std::slice::from_raw_parts_mut(out, count as usize);
    let last = (count.max(2) - 1) as f64;
    for (i, y) in out.iter_mut().enumerate() {
        let x = if count == 1 { 0.0 } else { -1.0 + 2.0 * i as f64 / last };
        *y = transfer(drive_db, bits, shape_pct, mid_rise, x) as f32;
    }
}

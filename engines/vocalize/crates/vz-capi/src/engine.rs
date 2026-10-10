// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
`vz_core.h`: the parameter table, the vowel's formants, each mode's latency,
and one engine instance. lib.rs says what the crate may do and which thread
calls what.
*/

use std::os::raw::{c_char, c_int};

use ni_dsp::ffi::{copy_cstr, cstr};
use vz_core::model::{Formants, FORMANTS};
use vz_core::{latency, Engine, Kind, Mode, Param, PARAM_COUNT};

/// How many formants `vz_formants` writes.
pub const VZ_FORMANTS: usize = FORMANTS;

/* ------------------------------------------------------------ the instance */

/// One engine.
pub struct VzCore {
    pub(crate) engine: Engine,
    /// Samples processed, wrapping: what says a picture is new.
    pub(crate) serial: u32,
}

impl VzCore {
    pub fn new(sample_rate: f64) -> VzCore {
        VzCore { engine: Engine::new(sample_rate), serial: 0 }
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
pub extern "C" fn vz_core_new(sample_rate: f64) -> *mut VzCore {
    Box::into_raw(Box::new(VzCore::new(sample_rate)))
}

/// # Safety
/// `core` is null or from `vz_core_new`, and not used after.
#[no_mangle]
pub unsafe extern "C" fn vz_core_free(core: *mut VzCore) {
    if !core.is_null() {
        drop(Box::from_raw(core));
    }
}

/// The host's rate. One that is not positive is ignored.
///
/// # Safety
/// `core` is null or live, and this thread's.
#[no_mangle]
pub unsafe extern "C" fn vz_core_set_sample_rate(core: *mut VzCore, sample_rate: f64) {
    if let Some(core) = core.as_mut() {
        if sample_rate > 0.0 {
            core.engine.set_sample_rate(sample_rate);
        }
    }
}

/// Set parameter `index` to `value`, in its own unit (vz_param_info),
/// clamped. NaN and an index out of range change nothing. Cheap when nothing
/// changes, so a shell may pass every parameter every block.
///
/// # Safety
/// `core` is null or live, and this thread's.
#[no_mangle]
pub unsafe extern "C" fn vz_core_set_num(core: *mut VzCore, index: i32, value: f64) {
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
pub unsafe extern "C" fn vz_core_num(core: *const VzCore, index: i32) -> f64 {
    match (core.as_ref(), Param::from_i32(index)) {
        (Some(core), Some(p)) => core.engine.num(p),
        _ => f64::NAN,
    }
}

/// The latency of the engine running now, in samples: Mode's, once a switch
/// has landed. 0 for no engine.
///
/// # Safety
/// `core` is null or live.
#[no_mangle]
pub unsafe extern "C" fn vz_core_latency(core: *const VzCore) -> u32 {
    core.as_ref().map_or(0, |c| c.engine.latency() as u32)
}

/// One block of `frames` host floats, in place: `left`, and `right` for a
/// stereo track or null for a mono one.
///
/// # Safety
/// `core` is null or live, and this thread's; `left` is null or holds
/// `frames` floats, and so does `right` unless it is null; the two do not
/// overlap.
#[no_mangle]
pub unsafe extern "C" fn vz_core_process_f32(core: *mut VzCore, left: *mut f32, right: *mut f32, frames: u32) {
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
pub unsafe extern "C" fn vz_core_process_i16(core: *mut VzCore, buf: *mut i16, frames: u32) {
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
pub unsafe extern "C" fn vz_core_on_midi(core: *mut VzCore, bytes: *const u8, len: usize) {
    let Some(core) = core.as_mut() else { return };
    if bytes.is_null() || len == 0 {
        return;
    }
    core.engine.on_midi(std::slice::from_raw_parts(bytes, len));
}

/// The panic: everything held lets go; the settings stay.
///
/// # Safety
/// `core` is null or live, and this thread's.
#[no_mangle]
pub unsafe extern "C" fn vz_core_reset(core: *mut VzCore) {
    if let Some(core) = core.as_mut() {
        core.engine.reset();
    }
}

/* ------------------------------------------------------ the parameter table */

/// What a parameter is.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum VzParamKind {
    /// A number from `min` to `max` in its unit.
    Float = 0,
    /// One of `max + 1` labels (vz_param_label), by index from 0; never
    /// automated.
    Choice = 1,
}

/// What a shell declares one parameter with. Values are in the parameter's
/// own unit (vz_param_unit); a choice's range is its indices.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct VzParamInfo {
    pub kind: VzParamKind,
    pub min: f64,
    pub max: f64,
    pub default_value: f64,
    /// True when the value runs both ways about zero.
    pub bipolar: bool,
    /// The finest step a host should keep (vz_param_snap); 1 for a choice.
    pub resolution: f64,
}

/// How many parameters there are.
#[no_mangle]
pub extern "C" fn vz_param_count() -> i32 {
    PARAM_COUNT as i32
}

/// Describe parameter `index`. Any thread. False for an index out of range.
///
/// # Safety
/// `out` is null or writable.
#[no_mangle]
pub unsafe extern "C" fn vz_param_info(index: i32, out: *mut VzParamInfo) -> bool {
    let (Some(p), Some(out)) = (Param::from_i32(index), out.as_mut()) else {
        return false;
    };
    *out = match p.info().kind {
        Kind::Float { min, max, default, bipolar, unit } => VzParamInfo {
            kind: VzParamKind::Float,
            min,
            max,
            default_value: default,
            bipolar,
            resolution: unit.resolution(),
        },
        Kind::Choice { labels, default } => VzParamInfo {
            kind: VzParamKind::Choice,
            min: 0.0,
            max: (labels.len() - 1) as f64,
            default_value: default as f64,
            bipolar: false,
            resolution: 1.0,
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
pub unsafe extern "C" fn vz_param_key(index: i32, out: *mut c_char, cap: c_int) -> c_int {
    match Param::from_i32(index) {
        Some(p) => copy_cstr(p.info().key, out, cap),
        None => -1,
    }
}

/// Parameter `index`'s display name, as `vz_param_key`.
///
/// # Safety
/// As `vz_param_key`.
#[no_mangle]
pub unsafe extern "C" fn vz_param_name(index: i32, out: *mut c_char, cap: c_int) -> c_int {
    match Param::from_i32(index) {
        Some(p) => copy_cstr(p.info().name, out, cap),
        None => -1,
    }
}

/// Parameter `index`'s unit ("%", "dB"; "" for the vowel and a choice), as
/// `vz_param_key`.
///
/// # Safety
/// As `vz_param_key`.
#[no_mangle]
pub unsafe extern "C" fn vz_param_unit(index: i32, out: *mut c_char, cap: c_int) -> c_int {
    match Param::from_i32(index) {
        Some(p) => match p.info().kind {
            Kind::Float { unit, .. } => copy_cstr(unit.label(), out, cap),
            Kind::Choice { .. } => copy_cstr("", out, cap),
        },
        None => -1,
    }
}

/// Choice parameter `index`'s label number `option` ("Time", "Spectral"),
/// as `vz_param_key`; -1 for a float or an option out of range.
///
/// # Safety
/// As `vz_param_key`.
#[no_mangle]
pub unsafe extern "C" fn vz_param_label(index: i32, option: i32, out: *mut c_char, cap: c_int) -> c_int {
    let label = Param::from_i32(index).and_then(|p| match p.info().kind {
        Kind::Choice { labels, .. } => usize::try_from(option).ok().and_then(|o| labels.get(o)).copied(),
        Kind::Float { .. } => None,
    });
    match label {
        Some(l) => copy_cstr(l, out, cap),
        None => -1,
    }
}

/// `value` of parameter `index` as every shell prints it ("80 %", "-6.0 dB",
/// "E>I 30 %", "Spectral"), as `vz_param_key`.
///
/// # Safety
/// As `vz_param_key`.
#[no_mangle]
pub unsafe extern "C" fn vz_param_text(index: i32, value: f64, out: *mut c_char, cap: c_int) -> c_int {
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
pub unsafe extern "C" fn vz_param_parse(index: i32, text: *const c_char, out: *mut f64) -> bool {
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
pub extern "C" fn vz_param_normalise(index: i32, value: f64) -> f64 {
    Param::from_i32(index).map_or(0.0, |p| p.normalise(value))
}

/// `value` on parameter `index`'s resolution, inside its range: what a host
/// that keeps values as floats should keep, so a value on it comes back
/// exactly. 0 for an index out of range. Any thread.
#[no_mangle]
pub extern "C" fn vz_param_snap(index: i32, value: f64) -> f64 {
    Param::from_i32(index).map_or(0.0, |p| p.snap(value))
}

/// The value at fraction `normalised` of parameter `index`'s travel: the
/// inverse of `vz_param_normalise`. 0 for an index out of range.
#[no_mangle]
pub extern "C" fn vz_param_denormalise(index: i32, normalised: f64) -> f64 {
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

/* ------------------------------------------------- the vowel, the latency */

/// The vowel's formant frequencies in Hz, F1 first, at `vowel` (0..4, A..U),
/// `size` (-100..100 %) and `resonance` (0..100 %) -- the engine's own model,
/// for an editor's vowel chart. Writes `VZ_FORMANTS` values. Any thread;
/// pure.
///
/// # Safety
/// `out` is null or has room for `VZ_FORMANTS` doubles.
#[no_mangle]
pub unsafe extern "C" fn vz_formants(vowel: f64, size: f64, resonance: f64, out: *mut f64) {
    if out.is_null() {
        return;
    }
    let f = Formants::at(vowel, size, resonance);
    std::slice::from_raw_parts_mut(out, FORMANTS).copy_from_slice(&f.freq);
}

/// The latency, in samples, of Mode value `mode` (0 Time, 1 Spectral) at
/// `sample_rate`: what a host is told when Mode changes. Any thread; pure.
#[no_mangle]
pub extern "C" fn vz_latency(mode: f64, sample_rate: f64) -> u32 {
    latency(Mode::from_value(mode), sample_rate) as u32
}

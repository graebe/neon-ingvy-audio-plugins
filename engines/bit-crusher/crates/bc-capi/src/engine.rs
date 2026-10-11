// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
`bc_core.h`: the parameter table, the steps as a curve, and one engine
instance. lib.rs says what the crate may do and which thread calls what.
*/

use std::os::raw::{c_char, c_int};

use bc_core::{transfer, transfer_span, Engine, Kind, Param, Travel, LATENCY, PARAM_COUNT, REFRESH_PERIOD_MS};
use ni_dsp::ffi::{copy_cstr, cstr};

/// How many samples an instance keeps for the scope.
pub const BC_SCOPE_RING: usize = 2048;

/* ------------------------------------------------------------ the instance */

/// One engine, and the last samples it processed.
pub struct BcCore {
    pub(crate) engine: Engine,
    dry: Box<[f32; BC_SCOPE_RING]>,
    wet: Box<[f32; BC_SCOPE_RING]>,
    /// What each of those samples did (bc_core::Probe): the level the
    /// quantiser was given, the level it became, and whether the hold took
    /// it -- beside the dry sample they belong to.
    amp_in: Box<[f32; BC_SCOPE_RING]>,
    amp_out: Box<[f32; BC_SCOPE_RING]>,
    took: Box<[bool; BC_SCOPE_RING]>,
    /// The band-limited hold's take (Anti-Alias): where in the sample it
    /// fell, 0..1 past it, or negative for none. Written about HALF_WIDTH
    /// samples after the sample it belongs to, which the hold reads ahead.
    exact_at: Box<[f32; BC_SCOPE_RING]>,
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
            amp_in: Box::new([0.0; BC_SCOPE_RING]),
            amp_out: Box::new([0.0; BC_SCOPE_RING]),
            took: Box::new([false; BC_SCOPE_RING]),
            exact_at: Box::new([-1.0; BC_SCOPE_RING]),
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

    /// One block in place, the left channel's dry and wet kept for the scope
    /// -- side by side as they belong together: the wet sample that came out
    /// LATENCY samples after the dry one it was made from -- and what each
    /// did in the quantiser and the hold, beside its dry sample. Five stores
    /// a sample, window open or not: not worth a switch.
    pub fn process(&mut self, left: &mut [f32], right: Option<&mut [f32]>) {
        /* Only the last ring's worth of a long block can be shown. */
        let keep = left.len().min(BC_SCOPE_RING);
        let from = left.len() - keep;
        let start = (self.pos + from) % BC_SCOPE_RING;
        for (i, x) in left[from..].iter().enumerate() {
            self.dry[(start + i) % BC_SCOPE_RING] = *x;
        }
        let (amp_in, amp_out, took, exact_at) = (&mut self.amp_in, &mut self.amp_out, &mut self.took, &mut self.exact_at);
        self.engine.process_f32_probed(left, right, |i, p| {
            if i >= from {
                let k = start + i - from;
                amp_in[k % BC_SCOPE_RING] = p.input as f32;
                amp_out[k % BC_SCOPE_RING] = p.level as f32;
                took[k % BC_SCOPE_RING] = p.took;
                exact_at[k % BC_SCOPE_RING] = -1.0;
                /* The band-limited hold's take, at the sample it fell after. */
                if let Some(at) = p.exact_take {
                    let t = (k + 2 * BC_SCOPE_RING) as f64 + at;
                    let j = t.floor();
                    exact_at[j as usize % BC_SCOPE_RING] = (t - j) as f32;
                }
            }
        });
        let late = start + BC_SCOPE_RING - LATENCY;
        for (i, y) in left[from..].iter().enumerate() {
            self.wet[(late + i) % BC_SCOPE_RING] = *y;
        }
        self.pos = (self.pos + left.len()) % BC_SCOPE_RING;
        self.serial = self.serial.wrapping_add(left.len() as u32);
    }

    /// The last `n` pairs whose wet half is out (at most the ring less the
    /// latency), oldest first, as (dry, wet).
    pub fn recent(&self, n: usize) -> impl Iterator<Item = (f32, f32)> + '_ {
        let n = n.min(BC_SCOPE_RING - LATENCY);
        let first = (self.pos + 2 * BC_SCOPE_RING - LATENCY - n) % BC_SCOPE_RING;
        (0..n).map(move |i| {
            let k = (first + i) % BC_SCOPE_RING;
            (self.dry[k], self.wet[k])
        })
    }

    /// What the same samples as `recent(n)` did, in the same order: (the
    /// quantiser's input, its level, whether the plain hold took it, and
    /// where in it the band-limited hold took one, 0..1, or negative).
    pub fn recent_probes(&self, n: usize) -> impl Iterator<Item = (f32, f32, bool, f32)> + '_ {
        let n = n.min(BC_SCOPE_RING - LATENCY);
        let first = (self.pos + 2 * BC_SCOPE_RING - LATENCY - n) % BC_SCOPE_RING;
        (0..n).map(move |i| {
            let k = (first + i) % BC_SCOPE_RING;
            (self.amp_in[k], self.amp_out[k], self.took[k], self.exact_at[k])
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

/// How late the output is, in samples: constant, whatever the settings and
/// the sample rate. What a shell reports to its host.
#[no_mangle]
pub extern "C" fn bc_latency() -> u32 {
    LATENCY as u32
}

/// Whether the crusher is heard. False is the host's bypass: the output fades
/// to the input, delayed by the same latency, and stays there; true fades the
/// crusher back in, starting from silence. Cheap when nothing changes, so a
/// shell may call it every block.
///
/// # Safety
/// `core` is null or live, and this thread's.
#[no_mangle]
pub unsafe extern "C" fn bc_core_set_active(core: *mut BcCore, active: bool) {
    if let Some(core) = core.as_mut() {
        core.engine.set_active(active);
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
    /// One of `choice_count` words (bc_param_choice), by index 0..count-1.
    Choice = 2,
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
    /// The finest step a host should keep (bc_param_snap); 0 for a toggle
    /// or a choice.
    pub resolution: f64,
    /// A choice's number of words; 0 otherwise.
    pub choice_count: i32,
    /// Whether a host should offer it for automation: false for a setting
    /// of the window (Refresh), which is saved but not automated.
    pub automatable: bool,
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
    let automatable = p.info().automatable;
    *out = match p.info().kind {
        Kind::Float { min, max, default, travel, bipolar, unit } => BcParamInfo {
            kind: BcParamKind::Float,
            min,
            max,
            default_value: default,
            log_travel: travel == Travel::Log,
            bipolar,
            resolution: unit.resolution(),
            choice_count: 0,
            automatable,
        },
        Kind::Toggle { default } => BcParamInfo {
            kind: BcParamKind::Toggle,
            min: 0.0,
            max: 1.0,
            default_value: default as i32 as f64,
            log_travel: false,
            bipolar: false,
            resolution: 0.0,
            choice_count: 0,
            automatable,
        },
        Kind::Choice { choices, default } => BcParamInfo {
            kind: BcParamKind::Choice,
            min: 0.0,
            max: (choices.len() - 1) as f64,
            default_value: f64::from(default),
            log_travel: false,
            bipolar: false,
            resolution: 0.0,
            choice_count: choices.len() as i32,
            automatable,
        },
    };
    true
}

/// Choice `choice` of parameter `index` ("1/s"), as `bc_param_key`: -1 for
/// a parameter that is no choice, or a choice it does not have.
///
/// # Safety
/// As `bc_param_key`.
#[no_mangle]
pub unsafe extern "C" fn bc_param_choice(index: i32, choice: i32, out: *mut c_char, cap: c_int) -> c_int {
    let Some(Kind::Choice { choices, .. }) = Param::from_i32(index).map(|p| p.info().kind) else { return -1 };
    match usize::try_from(choice).ok().and_then(|c| choices.get(c)) {
        Some(word) => copy_cstr(word, out, cap),
        None => -1,
    }
}

/// THE FILTER'S RESPONSE, for an editor's plot: `count` gains in dB at
/// frequencies evenly spaced in log from `lo_hz` to `hi_hz` (both included),
/// of the low-pass at `cutoff_hz` with `peak_db` and slope `slope` (0, 1, 2:
/// 12, 24, 48 dB per octave) at `sample_rate` -- the engine's own sections,
/// never a second model of them. Any thread; pure.
///
/// # Safety
/// `out` is null or has room for `count` floats.
#[no_mangle]
pub unsafe extern "C" fn bc_filter_response(
    cutoff_hz: f64,
    peak_db: f64,
    slope: i32,
    sample_rate: f64,
    lo_hz: f64,
    hi_hz: f64,
    out: *mut f32,
    count: u32,
) {
    if out.is_null() || count == 0 || !(sample_rate > 0.0) || !(lo_hz > 0.0) || !(hi_hz > lo_hz) {
        return;
    }
    let out = std::slice::from_raw_parts_mut(out, count as usize);
    let c = bc_core::filters::ToneCoefficients::new(cutoff_hz, peak_db, sample_rate);
    let slope = slope.clamp(0, 2) as usize;
    let last = (count.max(2) - 1) as f64;
    for (i, y) in out.iter_mut().enumerate() {
        let hz = lo_hz * (hi_hz / lo_hz).powf(i as f64 / last);
        *y = c.response_db(slope, hz.min(0.499 * sample_rate), sample_rate) as f32;
    }
}

/// How many levels the staircase has at `bits`, mid-tread or mid-rise: 2^b + 1
/// and 2^b at whole Bits (bc_core::level_count). Any thread; pure.
#[no_mangle]
pub extern "C" fn bc_level_count(bits: f64, mid_rise: bool) -> u32 {
    bc_core::level_count(bits, mid_rise)
}

/// How long the editor's pictures hold one look at Refresh choice `choice`,
/// in milliseconds: 0 for Live (every frame the editor draws); Refresh's
/// default for a choice out of range. Any thread.
#[no_mangle]
pub extern "C" fn bc_refresh_period_ms(choice: i32) -> f64 {
    let default = Param::Refresh.default_value() as usize;
    REFRESH_PERIOD_MS[usize::try_from(choice).ok().filter(|&c| c < REFRESH_PERIOD_MS.len()).unwrap_or(default)]
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
/// toggle or a choice), as `bc_param_key`.
///
/// # Safety
/// As `bc_param_key`.
#[no_mangle]
pub unsafe extern "C" fn bc_param_unit(index: i32, out: *mut c_char, cap: c_int) -> c_int {
    match Param::from_i32(index) {
        Some(p) => match p.info().kind {
            Kind::Float { unit, .. } => copy_cstr(unit.label(), out, cap),
            Kind::Toggle { .. } | Kind::Choice { .. } => copy_cstr("", out, cap),
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

/// `value` on parameter `index`'s resolution, inside its range: what a host
/// that keeps values as floats should keep, so a value on it comes back
/// exactly. 0 for an index out of range. Any thread.
#[no_mangle]
pub extern "C" fn bc_param_snap(index: i32, value: f64) -> f64 {
    Param::from_i32(index).map_or(0.0, |p| p.snap(value))
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

/// THE INPUT RANGE AN EDITOR'S PLOT OF THE STEPS SHOULD SHOW: the half-width
/// of -span..span, a power of two from 1 down, holding at most 16 steps of
/// the staircase these settings make (bc_core::transfer_span). Without it, 8
/// bits is 256 steps across a well narrower than that in pixels: a diagonal.
/// Any thread; pure.
#[no_mangle]
pub extern "C" fn bc_transfer_span(drive_db: f64, bits: f64, shape_pct: f64, mid_rise: bool) -> f64 {
    transfer_span(drive_db, bits, shape_pct, mid_rise)
}

/// THE STEPS AS A CURVE, for an editor's plot: `count` outputs for inputs
/// evenly spaced from -span to span (both included), through Drive (dB), the
/// grid of `bits`, Shape (%) and mid-rise or not -- the engine's own
/// staircase, without the noise and the time. `span` is bc_transfer_span's,
/// or 1 for the whole range; one that is not a positive number is 1. Any
/// thread; pure.
///
/// # Safety
/// `out` is null or has room for `count` floats.
#[no_mangle]
pub unsafe extern "C" fn bc_transfer_render(
    drive_db: f64,
    bits: f64,
    shape_pct: f64,
    mid_rise: bool,
    span: f64,
    out: *mut f32,
    count: u32,
) {
    if out.is_null() || count == 0 {
        return;
    }
    let span = if span > 0.0 && span.is_finite() { span.min(1.0) } else { 1.0 };
    let out = std::slice::from_raw_parts_mut(out, count as usize);
    let last = (count.max(2) - 1) as f64;
    for (i, y) in out.iter_mut().enumerate() {
        let x = if count == 1 { 0.0 } else { span * (-1.0 + 2.0 * i as f64 / last) };
        *y = transfer(drive_db, bits, shape_pct, mid_rise, x) as f32;
    }
}

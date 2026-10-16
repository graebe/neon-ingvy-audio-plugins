// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
`vz_core.h`: the parameter table, the vowel's formants, each mode's latency,
and one engine instance. lib.rs says what the crate may do and which thread
calls what.
*/

use std::os::raw::{c_char, c_int};
use std::sync::atomic::{AtomicBool, Ordering};

use ni_dsp::ffi::{byte_slice, cstr, drop_box, f32_block, i16_block, usable_rate};
pub use ni_params::{NiParamInfo, NiParamKind};
use vz_core::model::{Formants, VowelTable, FORMANTS};
use vz_core::{latency, Engine, Mode, Param, PARAM_COUNT};

/// How many formants `vz_formants` writes. A literal, so the header can
/// state it; held to the model's count below.
pub const VZ_FORMANTS: usize = 5;
const _: () = assert!(VZ_FORMANTS == FORMANTS);

/* ------------------------------------------------------------ the instance */

/// One engine.
pub struct VzCore {
    pub(crate) engine: Engine,
    /// Samples processed, wrapping: what says a picture is new.
    pub(crate) serial: u32,
    /// Whether a window shows the picture: the envelopes are worked out only
    /// then (shell.rs).
    pub(crate) watching: bool,
}

impl VzCore {
    pub fn new(sample_rate: f64) -> VzCore {
        VzCore { engine: Engine::new(sample_rate), serial: 0, watching: false }
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
    drop_box(core);
}

/// The host's rate. One that is not positive is ignored.
///
/// # Safety
/// `core` is null or live, and this thread's.
#[no_mangle]
pub unsafe extern "C" fn vz_core_set_sample_rate(core: *mut VzCore, sample_rate: f64) {
    if let (Some(core), Some(rate)) = (core.as_mut(), usable_rate(sample_rate)) {
        core.engine.set_sample_rate(rate);
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
    if let (Some(core), Some((l, r))) = (core.as_mut(), f32_block(left, right, frames)) {
        core.process(l, r);
    }
}

/// One block of the Move's interleaved 16-bit stereo, `frames` frames, in
/// place.
///
/// # Safety
/// `core` is null or live, and this thread's; `buf` is null or holds
/// `2 * frames` values.
#[no_mangle]
pub unsafe extern "C" fn vz_core_process_i16(core: *mut VzCore, buf: *mut i16, frames: u32) {
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
pub unsafe extern "C" fn vz_core_on_midi(core: *mut VzCore, bytes: *const u8, len: usize) {
    if let (Some(core), Some(msg)) = (core.as_mut(), byte_slice(bytes, len)) {
        core.engine.on_midi(msg);
    }
}

/// WHILE BYPASSED, one block of `frames` host floats heard and not changed:
/// the engine keeps its history and its glides, so `vz_core_resume` can start
/// it from the sound it missed rather than from what it held before.
///
/// # Safety
/// As `vz_core_process_f32`; nothing is written.
#[no_mangle]
pub unsafe extern "C" fn vz_core_listen_f32(core: *mut VzCore, left: *const f32, right: *const f32, frames: u32) {
    let Some(core) = core.as_mut() else { return };
    if left.is_null() || frames == 0 {
        return;
    }
    let l = std::slice::from_raw_parts(left, frames as usize);
    let r = (!right.is_null()).then(|| std::slice::from_raw_parts(right, frames as usize));
    core.engine.listen_f32(l, r);
}

/// AFTER A BYPASS: the engine starts again from the history it listened to,
/// primed, and fades in over 10 ms.
///
/// # Safety
/// `core` is null or live, and this thread's.
#[no_mangle]
pub unsafe extern "C" fn vz_core_resume(core: *mut VzCore) {
    if let Some(core) = core.as_mut() {
        core.engine.resume();
    }
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

/* What a parameter is, in C, is ni-params' `NiParamInfo`, shared by every
 * product: build.rs names its source beside this file's. Every entry point
 * below is one line around a ni-params helper. */

fn kind(index: i32) -> Option<ni_params::Kind> {
    Param::from_i32(index).map(|p| p.info().kind)
}

/// How many parameters there are.
#[no_mangle]
pub extern "C" fn vz_param_count() -> i32 {
    PARAM_COUNT as i32
}

/// Describe parameter `index`: its kind, range, default, resolution, and
/// whether a host may automate it. Any thread. False for an index out of
/// range.
///
/// # Safety
/// `out` is null or writable.
#[no_mangle]
pub unsafe extern "C" fn vz_param_info(index: i32, out: *mut NiParamInfo) -> bool {
    ni_params::c_info(kind(index), out)
}

/// Parameter `index`'s stable key, as a saved state and the Move name it,
/// into `out`. Returns its length, or -1 when the index is out of range or
/// `cap` too small (nothing written). Any thread.
///
/// # Safety
/// `out` is null or has room for `cap` bytes.
#[no_mangle]
pub unsafe extern "C" fn vz_param_key(index: i32, out: *mut c_char, cap: c_int) -> c_int {
    ni_params::c_text(Param::from_i32(index).map(|p| p.info().key), out, cap)
}

/// Parameter `index`'s display name, as `vz_param_key`.
///
/// # Safety
/// As `vz_param_key`.
#[no_mangle]
pub unsafe extern "C" fn vz_param_name(index: i32, out: *mut c_char, cap: c_int) -> c_int {
    ni_params::c_text(Param::from_i32(index).map(|p| p.info().name), out, cap)
}

/// Parameter `index`'s unit ("%", "dB"; "" for the vowel and a choice), as
/// `vz_param_key`.
///
/// # Safety
/// As `vz_param_key`.
#[no_mangle]
pub unsafe extern "C" fn vz_param_unit(index: i32, out: *mut c_char, cap: c_int) -> c_int {
    ni_params::c_text(Param::from_i32(index).map(|p| p.info().unit.map_or("", |u| u.label())), out, cap)
}

/// Choice parameter `index`'s label number `option` ("Time", "Spectral",
/// "Off", "On"), as `vz_param_key`; -1 for a number or an option out of range.
///
/// # Safety
/// As `vz_param_key`.
#[no_mangle]
pub unsafe extern "C" fn vz_param_label(index: i32, option: i32, out: *mut c_char, cap: c_int) -> c_int {
    ni_params::c_text(kind(index).and_then(|k| k.label(option)), out, cap)
}

/// `value` of parameter `index` as every shell prints it ("80 %", "-6.0 dB",
/// "E>I 30 %", "Spectral"), as `vz_param_key`.
///
/// # Safety
/// As `vz_param_key`.
#[no_mangle]
pub unsafe extern "C" fn vz_param_text(index: i32, value: f64, out: *mut c_char, cap: c_int) -> c_int {
    ni_params::c_written(Param::from_i32(index).map(|p| move |w: &mut dyn core::fmt::Write| p.write_value(w, value)), out, cap)
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
    kind(index).map_or(0.0, |k| k.normalise(value))
}

/// `value` on parameter `index`'s resolution, inside its range: what a host
/// that keeps values as floats should keep, so a value on it comes back
/// exactly. 0 for an index out of range. Any thread.
#[no_mangle]
pub extern "C" fn vz_param_snap(index: i32, value: f64) -> f64 {
    kind(index).map_or(0.0, |k| k.snap(value))
}

/// The value at fraction `normalised` of parameter `index`'s travel: the
/// inverse of `vz_param_normalise`. 0 for an index out of range.
#[no_mangle]
pub extern "C" fn vz_param_denormalise(index: i32, normalised: f64) -> f64 {
    kind(index).map_or(0.0, |k| k.denormalise(normalised))
}

/* ------------------------------------------------- the vowel, the latency */

/// The formant frequencies in Hz, F1 first, at `vowel` (0..4, A..U), `size`
/// (-100..100 %) and `resonance` (0..100 %), of the voice the engine sings
/// with at Voice `amount` (0..100 %): `voice` (null for the textbook's)
/// morphed from the textbook's -- the engine's own model, for an editor's
/// vowel chart. Writes `VZ_FORMANTS` values. Any thread; pure.
///
/// # Safety
/// `voice` is null or readable; `out` is null or has room for `VZ_FORMANTS`
/// doubles.
#[no_mangle]
pub unsafe extern "C" fn vz_formants(
    voice: *const VzVoice,
    amount: f64,
    vowel: f64,
    size: f64,
    resonance: f64,
    out: *mut f64,
) {
    if out.is_null() {
        return;
    }
    let captured = voice.as_ref().and_then(VzVoice::table).unwrap_or(VowelTable::TEXTBOOK);
    let table = VowelTable::TEXTBOOK.blend(&captured, amount / 100.0);
    let f = Formants::of(&table, vowel, size, resonance);
    std::slice::from_raw_parts_mut(out, FORMANTS).copy_from_slice(&f.freq);
}

/// The latency, in samples, of Mode value `mode` (0 Time, 1 Spectral) at
/// `sample_rate`: what a host is told when Mode changes. Any thread; pure.
#[no_mangle]
pub extern "C" fn vz_latency(mode: f64, sample_rate: f64) -> u32 {
    latency(Mode::from_value(mode), sample_rate) as u32
}

/* ------------------------------------------------------------ the voice */

/// A VOICE: five vowels' five formants and their bandwidths, in Hz, vowel
/// by vowel (A E I O U), F1 first -- `freq[5 * vowel + formant]` -- and what
/// a capture found. The textbook's (vz_voice_textbook) or a singer's
/// (vz_capture_run); a shell keeps one in its saved state.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct VzVoice {
    pub freq: [f64; 25],
    pub bandwidth: [f64; 25],
    /// Which vowels a capture measured; the rest are the textbook's, scaled.
    pub found: [bool; 5],
    /// How many frames each was measured from.
    pub frames: [u32; 5],
    /// The singer's size against the textbook voice (1 for the textbook).
    pub scale: f64,
    /// How far each vowel's frames agreed, 0 (unmeasured) to 1.
    pub confidence: [f64; 5],
    /// Whether the vowels were told apart by the order they were sung in.
    pub by_order: bool,
}

impl VzVoice {
    fn of(table: &VowelTable) -> VzVoice {
        VzVoice {
            freq: core::array::from_fn(|i| table.freq[i / 5][i % 5]),
            bandwidth: core::array::from_fn(|i| table.bandwidth[i / 5][i % 5]),
            found: [false; 5],
            frames: [0; 5],
            scale: 1.0,
            confidence: [0.0; 5],
            by_order: false,
        }
    }

    /// The table, made safe to sing from; `None` for one that is not.
    pub fn table(&self) -> Option<VowelTable> {
        VowelTable {
            freq: core::array::from_fn(|v| core::array::from_fn(|k| self.freq[5 * v + k])),
            bandwidth: core::array::from_fn(|v| core::array::from_fn(|k| self.bandwidth[5 * v + k])),
        }
        .sanitised()
    }
}

/// The textbook voice, into `out`. Any thread.
///
/// # Safety
/// `out` is null or writable.
#[no_mangle]
pub unsafe extern "C" fn vz_voice_textbook(out: *mut VzVoice) {
    if let Some(out) = out.as_mut() {
        *out = VzVoice::of(&VowelTable::TEXTBOOK);
    }
}

/// Make `voice` safe to sing from, in place: what a shell does with a voice
/// read from a saved set. False, and nothing changed, for one with a number
/// in it that is not a number, which is then no voice at all. Any thread.
///
/// # Safety
/// `voice` is null or writable.
#[no_mangle]
pub unsafe extern "C" fn vz_voice_check(voice: *mut VzVoice) -> bool {
    let Some(voice) = voice.as_mut() else { return false };
    match voice.table() {
        Some(t) => {
            let safe = VzVoice::of(&t);
            voice.freq = safe.freq;
            voice.bandwidth = safe.bandwidth;
            voice.scale = if voice.scale.is_finite() && voice.scale > 0.0 { voice.scale } else { 1.0 };
            for c in voice.confidence.iter_mut() {
                *c = if c.is_finite() { c.clamp(0.0, 1.0) } else { 0.0 };
            }
            true
        }
        None => false,
    }
}

/// How a capture ended.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum VzCaptured {
    /// A voice: written to `out`.
    Voice = 0,
    /// Called off (vz_capture_cancel) before it finished.
    Cancelled = 1,
    /// Nothing in the recording was sung: silence, noise, too short, a rate
    /// below 8 kHz -- or a build without capture (the Move's).
    Unsung = 2,
    /// Something was sung, but none of it near enough any vowel.
    NoVowels = 3,
}

/// One capture that can be called off from another thread: made by the
/// thread that starts it, run by the thread that does the work, cancelled by
/// whoever must stop waiting.
pub struct VzCapture {
    cancel: AtomicBool,
}

/// A capture, ready to run. Allocates.
#[no_mangle]
pub extern "C" fn vz_capture_new() -> *mut VzCapture {
    Box::into_raw(Box::new(VzCapture { cancel: AtomicBool::new(false) }))
}

/// # Safety
/// `capture` is null or from `vz_capture_new`, no longer running, and not
/// used after.
#[no_mangle]
pub unsafe extern "C" fn vz_capture_free(capture: *mut VzCapture) {
    if !capture.is_null() {
        drop(Box::from_raw(capture));
    }
}

/// Call `capture` off: a run under way ends within a frame of the
/// analysis, as `Cancelled`. Any thread.
///
/// # Safety
/// `capture` is null or live.
#[no_mangle]
pub unsafe extern "C" fn vz_capture_cancel(capture: *const VzCapture) {
    if let Some(c) = capture.as_ref() {
        c.cancel.store(true, Ordering::Relaxed);
    }
}

/// RUN `capture` over `frames` mono samples at `sample_rate`: a singer's
/// vowels, measured (vz-core's voice.rs), into `out` when it is a voice --
/// nothing is written otherwise. Slow and allocating: not the audio thread,
/// nor one that must stay responsive.
///
/// # Safety
/// `capture` is null or live; `samples` is null or holds `frames` floats;
/// `out` is null or writable.
#[no_mangle]
pub unsafe extern "C" fn vz_capture_run(
    capture: *const VzCapture,
    samples: *const f32,
    frames: usize,
    sample_rate: f64,
    out: *mut VzVoice,
) -> VzCaptured {
    let (Some(capture), Some(out)) = (capture.as_ref(), out.as_mut()) else {
        return VzCaptured::Unsung;
    };
    if samples.is_null() {
        return VzCaptured::Unsung;
    }
    #[cfg(feature = "capture")]
    {
        use vz_core::voice::{capture_cancellable, Failure};
        let x = std::slice::from_raw_parts(samples, frames);
        match capture_cancellable(x, sample_rate, &capture.cancel) {
            Ok(c) => {
                let mut v = VzVoice::of(&c.table);
                v.found = c.found;
                v.frames = c.frames;
                v.scale = c.scale;
                v.confidence = c.confidence;
                v.by_order = c.by_order;
                *out = v;
                VzCaptured::Voice
            }
            Err(Failure::Cancelled) => VzCaptured::Cancelled,
            Err(Failure::Unsung) => VzCaptured::Unsung,
            Err(Failure::NoVowels) => VzCaptured::NoVowels,
        }
    }
    #[cfg(not(feature = "capture"))]
    {
        let _ = (capture, frames, sample_rate, out);
        VzCaptured::Unsung
    }
}

/// Sing with `voice` from now on (null: the textbook's); the vowels glide to
/// it. False, and nothing changed, for a voice that is not one
/// (vz_voice_check).
///
/// # Safety
/// `core` is null or live, and this thread's; `voice` is null or readable.
#[no_mangle]
pub unsafe extern "C" fn vz_core_set_voice(core: *mut VzCore, voice: *const VzVoice) -> bool {
    let Some(core) = core.as_mut() else { return false };
    let table = match voice.as_ref() {
        Some(v) => v.table(),
        None => Some(VowelTable::TEXTBOOK),
    };
    table.is_some_and(|t| core.engine.set_voice(&t))
}

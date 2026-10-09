// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
NI Side-Chain's `sc_core_*` C ABI over the Rust ducker engine, and the source
of sc_core.h, which build.rs generates from this file (cbindgen's
configuration is cbindgen/sc_core.toml). What is declared here is what C sees:
every `pub const` a `#define`, [`ScParam`] the `sc_param_t` enum, each
`#[no_mangle]` function a prototype, with these doc comments above them.

WHAT THIS FILE IS ALLOWED TO DO: null-check, bound-check, and hand the call
to `sc-core`. Nothing here decides anything about the DSP. If a function
below grows a branch that is not about the C boundary, it is in the wrong file.

THE HEADER IS GENERATED FROM IT, so the two cannot disagree: a signature here
is the prototype there. The engine's C tests still `#include` the header and
link the real library, which is what says the boundary behaves.

THREADING. Every function here may be called from the audio callback and none
of them allocate, lock or unwind -- the workspace sets `panic = "abort"`
because unwinding out of `extern "C"` into a C++ host is undefined behaviour.
The exceptions are create/destroy, which allocate and must not be called from
the callback.

WHAT THE ENGINE DOES NOT KNOW: buses, plugin formats, editors, or which host it
is in. It is handed audio, optionally a key signal, optionally a transport, and
MIDI with sample offsets.

THE TRANSPORT (`sc_transport_t`, ni-dsp's [`CTransport`]). `beats` IS QUARTER
NOTES SINCE THE START OF THE TIMELINE, and a NEGATIVE value means "no
transport" -- which is not the same as beat zero. A stopped host must never
report a stale position: pass running = 0 and beats = -1. Passing NULL for the
whole struct means the host told us nothing at all; the engine then keeps the
last tempo it saw, which is what lets a MIDI- or sidechain-triggered duck work
in a session with the timeline parked.
*/

use ni_dsp::ffi::{cstr as s, CTransport};
use sc_core::params::{Param, PARAM_COUNT};
use sc_core::rates::{RATES, RATE_DEFAULT};
use sc_core::{Instance, MAX_BLOCK};
use std::os::raw::{c_char, c_int};

/// The longest block the key buffer holds. A host handing over more than this
/// must be chunked by the shell -- which already chunks for its own dry-copy
/// buffers. Past this point a block gets a key signal for its first
/// SC_MAX_BLOCK frames and silence after, so do not rely on it.
pub const SC_MAX_BLOCK: usize = 8192;
const _: () = assert!(SC_MAX_BLOCK == MAX_BLOCK);

/// Big enough for every readout `sc_core_get_param` gives, `params` included.
pub const SC_STATE_MAX: usize = 4096;

/// The parameters, in the order the shell declares them, so a HOST INDEX IS
/// AN ENGINE INDEX.
///
/// APPEND ONLY. The numeric form is what a host stores in a session, so
/// inserting a parameter re-points every project saved by the build before it.
/// They are sc-core's [`Param`], asserted equal below.
///
/// Rust never receives this type: [`sc_core_set_num`] and [`sc_core_get_num`]
/// take the plain [`ScParamArg`], since a C caller can pass any int and an int
/// outside the enum in a Rust enum is undefined behaviour. The header names
/// that argument's type `sc_param_t`, as it always did (cbindgen's
/// export.rename).
#[repr(C)]
#[allow(
    non_camel_case_types,
    reason = "the variants are the C names, which C callers spell"
)]
pub enum ScParam {
    /// 0 Cycle, 1 MIDI, 2 Sidechain.
    SC_P_SOURCE = 0,
    /// An index into the rate table; `sc_core_rate_label` names it.
    SC_P_RATE,
    /// 0 ms, 1 % of cycle -- A DISPLAY CHOICE, see `sc_core_set_num`.
    SC_P_TIME_MODE,
    /// -100..100 percent of the cycle; NEGATIVE IS EARLY.
    SC_P_DELAY,
    /// 0..200 percent of the cycle.
    SC_P_ATTACK,
    /// 0..200 percent of the cycle.
    SC_P_HOLD,
    /// 0..200 percent of the cycle.
    SC_P_RELEASE,
    /// 0..1.
    SC_P_DEPTH,
    /// 0 Linear, 1 Exponential, 2 S-Curve.
    SC_P_CURVE,
    /// 0 Omni, 1..16.
    SC_P_CHANNEL,
    /// 0..127.
    SC_P_NOTE,
    /// 0 Trigger, 1 Gate.
    SC_P_MIDI_MODE,
    /// 0..1.
    SC_P_VEL_SENS,
    /// dB, -60..0. -60 means "anything triggers".
    SC_P_THRESHOLD,
    /// ms, 0..200.
    SC_P_LOCKOUT,
    SC_P_COUNT,
}

/// An `sc_param_t` as it arrives: any int at all.
pub type ScParamArg = c_int;

const _: () = {
    use ScParam::*;
    assert!(SC_P_SOURCE as i32 == Param::Source as i32);
    assert!(SC_P_RATE as i32 == Param::Rate as i32);
    assert!(SC_P_TIME_MODE as i32 == Param::TimeMode as i32);
    assert!(SC_P_DELAY as i32 == Param::Delay as i32);
    assert!(SC_P_ATTACK as i32 == Param::Attack as i32);
    assert!(SC_P_HOLD as i32 == Param::Hold as i32);
    assert!(SC_P_RELEASE as i32 == Param::Release as i32);
    assert!(SC_P_DEPTH as i32 == Param::Depth as i32);
    assert!(SC_P_CURVE as i32 == Param::Curve as i32);
    assert!(SC_P_CHANNEL as i32 == Param::Channel as i32);
    assert!(SC_P_NOTE as i32 == Param::Note as i32);
    assert!(SC_P_MIDI_MODE as i32 == Param::MidiMode as i32);
    assert!(SC_P_VEL_SENS as i32 == Param::VelSens as i32);
    assert!(SC_P_THRESHOLD as i32 == Param::Threshold as i32);
    assert!(SC_P_LOCKOUT as i32 == Param::Lockout as i32);
    assert!(SC_P_COUNT as i32 == PARAM_COUNT);
};

/// The opaque handle. A newtype rather than `Instance` directly so the C side
/// cannot be given a layout it might be tempted to rely on.
pub struct ScCore(pub(crate) Instance);

/// The transport as `sc_core.h` declares it, `sc_transport_t`.
pub use ni_dsp::ffi::CTransport as ScTransport;

/* ----------------------------------------------------------- lifecycle */

/// A new engine at `sample_rate`. Allocates: never on the audio callback.
#[no_mangle]
pub extern "C" fn sc_core_create(sample_rate: f64) -> *mut ScCore {
    Box::into_raw(Box::new(ScCore(Instance::new(sample_rate))))
}

/// # Safety
/// `c` is null or from `sc_core_create`, and is not used afterwards.
#[no_mangle]
pub unsafe extern "C" fn sc_core_destroy(c: *mut ScCore) {
    if !c.is_null() {
        drop(Box::from_raw(c));
    }
}

/// # Safety
/// `c` is null or a live engine that no other call is using.
#[no_mangle]
pub unsafe extern "C" fn sc_core_set_sample_rate(c: *mut ScCore, sample_rate: f64) {
    if let Some(c) = c.as_mut() {
        c.0.set_sample_rate(sample_rate);
    }
}

/// # Safety
/// `c` is null or a live engine.
#[no_mangle]
pub unsafe extern "C" fn sc_core_get_sample_rate(c: *const ScCore) -> f64 {
    c.as_ref().map(|c| c.0.sample_rate()).unwrap_or(0.0)
}

/// Open the gate now and forget the trigger: a panic.
///
/// # Safety
/// `c` is null or a live engine that no other call is using.
#[no_mangle]
pub unsafe extern "C" fn sc_core_reset(c: *mut ScCore) {
    if let Some(c) = c.as_mut() {
        c.0.reset();
    }
}

/* -------------------------------------------------------------- inputs */

/// The key signal for the block about to be rendered.
///
/// Called BEFORE `process`, and the engine clears it afterwards -- so a block
/// with no key is silence rather than the previous block held. A shell that
/// forgets to push does not get a stale trigger.
///
/// # Safety
/// `c` is null or a live engine that no other call is using; `l` and `r` are
/// each null or hold `frames` samples.
#[no_mangle]
pub unsafe extern "C" fn sc_core_push_key_f32(
    c: *mut ScCore,
    l: *const f32,
    r: *const f32,
    frames: c_int,
) {
    let Some(c) = c.as_mut() else { return };
    if l.is_null() || r.is_null() || frames <= 0 {
        return;
    }
    let n = (frames as usize).min(MAX_BLOCK);
    c.0.push_key(
        std::slice::from_raw_parts(l, n),
        std::slice::from_raw_parts(r, n),
        n,
    );
}

/// Tell the engine whether an aux bus is actually patched.
///
/// The engine does not infer this, and must not: an unconnected bus and a
/// silent one are the same block of zeroes, and the difference is precisely
/// what the UI has to report. "No key" is a different message from "nothing is
/// playing", and only the shell can tell them apart.
///
/// # Safety
/// `c` is null or a live engine that no other call is using.
#[no_mangle]
pub unsafe extern "C" fn sc_core_set_key_connected(c: *mut ScCore, connected: c_int) {
    if let Some(c) = c.as_mut() {
        c.0.set_key_connected(connected != 0);
    }
}

/// Queue a MIDI message at `at`, a sample offset within the next block.
///
/// `at` IS NOT OPTIONAL TO GET RIGHT. Passing 0 for everything -- applying each
/// note at the top of its block -- costs up to a full buffer of jitter on the one event
/// whose timing is the whole effect -- and it is jitter, not latency, so it
/// cannot be compensated. iPlug2 has it in `IMidiMsg::mOffset`; the Schwung v2
/// `on_midi` has no such field and passes 0, which is also what makes the
/// Live/Move render A/B comparable.
///
/// An offset past the end of the block is CLAMPED into it rather than dropped:
/// the queue is cleared per block, so an event the sample walk never reaches
/// is an event that never happens, and a host reporting an offset against a
/// different buffer size is a real thing.
///
/// CC 120 (All Sound Off) and CC 123 (All Notes Off) open the gate, whatever
/// the note filter says. That is the only channel a host panic can reach a
/// Schwung module through -- the v2 vtable has no reset hook.
///
/// # Safety
/// `c` is null or a live engine that no other call is using; `msg` is null or
/// holds `len` bytes.
#[no_mangle]
pub unsafe extern "C" fn sc_core_on_midi(c: *mut ScCore, msg: *const u8, len: c_int, at: c_int) {
    let Some(c) = c.as_mut() else { return };
    if msg.is_null() || len <= 0 || len > 8 {
        return;
    }
    let bytes = std::slice::from_raw_parts(msg, len as usize);
    c.0.on_midi(bytes, at.max(0) as usize);
}

/* ------------------------------------------------------------- process */

/// One block, in place, and THE DRY COPY IS THE CALLER'S JOB: the plugin
/// keeps one so it can draw the input behind the output.
///
/// The float paths do not clamp, deliberately. A ducker only ever ATTENUATES
/// -- the gain is in 0..1 -- so it cannot push a signal out of range, and a
/// host is entitled to headroom above 1.0 that we must not steal. The i16 path
/// clamps because i16 has no headroom, and asymmetrically because i16 is:
/// -32768 is representable and +32768 is not.
///
/// ONE GAIN LAW, THREE BUFFER FORMATS. Move hands over int16 interleaved;
/// VST3, AU and CLAP hand over float, usually as separate channel pointers.
///
/// # Safety
/// `c` is null or a live engine that no other call is using; `l` and `r` are
/// each null or hold `frames` samples; `t` is null or a valid transport.
#[no_mangle]
pub unsafe extern "C" fn sc_core_process_f32_split(
    c: *mut ScCore,
    l: *mut f32,
    r: *mut f32,
    frames: c_int,
    t: *const CTransport,
) {
    let Some(c) = c.as_mut() else { return };
    if l.is_null() || r.is_null() || frames <= 0 {
        return;
    }
    let n = frames as usize;
    let lb = std::slice::from_raw_parts_mut(l, n);
    let rb = std::slice::from_raw_parts_mut(r, n);
    c.0.process_f32_split(lb, rb, n, CTransport::read(t).as_ref());
}

/// The split path, tapping the applied gain and the display sweep per sample.
///
/// Either out-pointer may be NULL; both NULL is the plain split path. `gain` is
/// the MULTIPLIER APPLIED, 0..1, with Depth already in it -- so a trace drawn
/// from it is what the listener heard, not the envelope behind it. `sweep` is
/// where the sample sits on the editor's axis, which is what lets the shell bin
/// its capture columns without re-implementing the phase logic.
///
/// WHY THE ENGINE HANDS THE GAIN OUT rather than the shell deriving it: the
/// editor draws what the ducker actually DID beside what it was asked to do,
/// and the two differ whenever a trigger interrupts a recovery. The shell has
/// the dry and the wet and could divide one by the other, but that answer is
/// meaningless wherever the input is near silence -- which is exactly where a
/// duck is most visible. And it CANNOT compute the sweep itself without
/// becoming a second copy of the phase logic: the phase-locked loop's
/// per-block correction, the saturation past one cycle, and the difference
/// between the three sources all feed it.
///
/// # Safety
/// As `sc_core_process_f32_split`, and `gain` and `sweep` are each null or
/// writable for `frames` floats.
#[no_mangle]
pub unsafe extern "C" fn sc_core_process_f32_split_tap(
    c: *mut ScCore,
    l: *mut f32,
    r: *mut f32,
    gain: *mut f32,
    sweep: *mut f32,
    frames: c_int,
    t: *const CTransport,
) {
    let Some(c) = c.as_mut() else { return };
    if l.is_null() || r.is_null() || frames <= 0 {
        return;
    }
    let n = frames as usize;
    let lb = std::slice::from_raw_parts_mut(l, n);
    let rb = std::slice::from_raw_parts_mut(r, n);
    let gb = if gain.is_null() {
        None
    } else {
        Some(std::slice::from_raw_parts_mut(gain, n))
    };
    let sb = if sweep.is_null() {
        None
    } else {
        Some(std::slice::from_raw_parts_mut(sweep, n))
    };
    c.0.process_f32_split_tap(
        lb,
        rb,
        gb,
        sb,
        n,
        CTransport::read(t).as_ref(),
    );
}

/// # Safety
/// `c` is null or a live engine that no other call is using; `lr` is null or
/// holds `2 * frames` interleaved samples; `t` is null or a valid transport.
#[no_mangle]
pub unsafe extern "C" fn sc_core_process_f32(
    c: *mut ScCore,
    lr: *mut f32,
    frames: c_int,
    t: *const CTransport,
) {
    let Some(c) = c.as_mut() else { return };
    if lr.is_null() || frames <= 0 {
        return;
    }
    let n = frames as usize;
    let buf = std::slice::from_raw_parts_mut(lr, n * 2);
    c.0.process_f32(buf, n, CTransport::read(t).as_ref());
}

/// # Safety
/// `c` is null or a live engine that no other call is using; `lr` is null or
/// holds `2 * frames` interleaved samples; `t` is null or a valid transport.
#[no_mangle]
pub unsafe extern "C" fn sc_core_process_i16(
    c: *mut ScCore,
    lr: *mut i16,
    frames: c_int,
    t: *const CTransport,
) {
    let Some(c) = c.as_mut() else { return };
    if lr.is_null() || frames <= 0 {
        return;
    }
    let n = frames as usize;
    let buf = std::slice::from_raw_parts_mut(lr, n * 2);
    c.0.process_i16(buf, n, CTransport::read(t).as_ref());
}

/* ---------------------------------------------------------- parameters */

/// The audio thread's door: a parameter by number (`sc_param_t`), for host
/// automation. `sc_core_set_param` is the other.
///
/// DELAY IS TWO MECHANISMS, AND THE SOURCE DECIDES WHICH. On CYCLE it is a
/// phase offset on the trigger rather than a wait -- the cycle is PERIODIC, so
/// firing at 80% of it is the same event as firing 20% before the next beat.
/// That is what makes a NEGATIVE delay possible without anticipating anything:
/// "early" is a position already passed. A positive delay lands on exactly the
/// same samples either way, so it goes through the phase too and there is one
/// mechanism rather than two. On MIDI and SIDECHAIN it is a wait after the
/// trigger, because a note that has not arrived cannot be ducked ahead of. A
/// negative delay is CLAMPED TO ZERO there rather than refused: an automation
/// lane is entitled to sweep through it, and the editor says which sources can
/// use it.
///
/// THE TIME UNIT IS A PERCENTAGE OF THE CYCLE, ALWAYS, AND SC_P_TIME_MODE IS A
/// DISPLAY CHOICE -- stated because a reader coming from a compressor will
/// expect otherwise. The four stage lengths are percentages of the current
/// cycle; ms and % are two readings of one number, and
/// `sc_core_get_param("stage_ms")` gives the millisecond reading. A parameter
/// whose MEANING depended on another would be one whose automation lane
/// changes what it does when something else moves; and the musical default
/// for a ducker is the relative one anyway: a shape proportional to the cycle
/// keeps its proportions when the tempo or the rate changes.
///
/// TWO DOORS, AND WHY BOTH. This one takes a number and is for host
/// automation on the audio thread. `sc_core_set_param` takes strings and is
/// for a Schwung chain_params shell, a typed value, or a saved state -- and it
/// is implemented VIA this one, so every clamp exists exactly once and the
/// string door's only extra job is deciding which number a word means.
///
/// Out-of-range values are CLAMPED, never rejected: a host is allowed to send
/// a normalised value that rounds outside the range, and refusing it would
/// freeze the parameter rather than move it. A NaN is DROPPED, because
/// clamping one propagates it and a NaN gain silences a track permanently. A
/// parameter outside the enum is dropped too.
///
/// # Safety
/// `c` is null or a live engine that no other call is using.
#[no_mangle]
pub unsafe extern "C" fn sc_core_set_num(c: *mut ScCore, param: ScParamArg, value: f64) {
    let Some(c) = c.as_mut() else { return };
    let Some(p) = Param::from_i32(param) else { return };
    c.0.set_num(p, value);
}

/// A parameter's value by number, on `sc_core_set_num`'s units; 0 for one
/// outside the enum.
///
/// # Safety
/// `c` is null or a live engine.
#[no_mangle]
pub unsafe extern "C" fn sc_core_get_num(c: *const ScCore, param: ScParamArg) -> f64 {
    let Some(c) = c.as_ref() else { return 0.0 };
    let Some(p) = Param::from_i32(param) else {
        return 0.0;
    };
    c.0.num(p)
}

/// The string door: a key and its value, as a label ("1/8", "S-Curve",
/// "Omni") or an index. Returns nonzero if the key was recognised, so a shell
/// can chain its own. Implemented via `sc_core_set_num`; the string form
/// exists because atof honours LC_NUMERIC, so in a host running under a
/// comma-decimal locale "0.750" parses as 0, and this does not.
///
/// # Safety
/// `c` is null or a live engine that no other call is using; `key` and `val`
/// are each null or NUL-terminated.
#[no_mangle]
pub unsafe extern "C" fn sc_core_set_param(
    c: *mut ScCore,
    key: *const c_char,
    val: *const c_char,
) -> c_int {
    let Some(c) = c.as_mut() else { return 0 };
    c.0.set_param(s(key), s(val)) as c_int
}

/// `key`'s readout into `buf`. Returns the length WRITTEN, excluding the
/// terminator, or -1 for a key this engine does not own. The buffer is always
/// terminated.
///
/// NOT SNPRINTF'S RETURN. snprintf reports what would have fit; this reports
/// what did, so the result is always <= buf_len - 1 and TRUNCATION IS SILENT
/// -- a short buffer looks exactly like a short value. Pass SC_STATE_MAX,
/// which is sized for the longest readout here, and treat a result of
/// buf_len - 1 as a bug in the caller rather than a value. (`Buf` in
/// ni_dsp::fmt does track the would-have-fit length. It is deliberately not
/// returned, matching tg_core_get_param, so that both engines' ABIs answer
/// this question the same way. Do not "fix" one of them alone.)
///
/// Keys:
///
///   "ui"            ONE READ FOR THE WHOLE ANIMATED PICTURE, pushed per frame:
///                   source:rate:ms_cycle:sweep:advancing:fires:duck:key:connected:stage:phase
///                   ELEVEN fields, positional -- the editor parses it by
///                   position, so this is a contract and not a debug dump.
///   "sweep"         the shared display axis, 0..1 -- see sc_core_sweep01
///   "params"        every automatable value, in sc_param_t order, colon
///                   separated, at full round-trip float precision
///   "stage_ms"      delay:attack:hold:release, in milliseconds
///   "phase"         cycle phase 0..1
///   "ms_per_cycle"  the cycle length in ms
///   "fires"         monotonic trigger count -- watch it CHANGE
///   "duck"          current attenuation 0..1
///   "key_level"     the detector's smoothed level
///   "advancing"     1 while the transport is running
///   "dropped"       MIDI events lost to queue overflow, a diagnostic
///   "rate_label"    "1/8" -- so no shell re-spells a table the engine owns
///   "curve_label"   "S-Curve"
///   "source_label"  "Sidechain"
///   <any param key> "attack", "depth", "trigger_note", ... the numeric value
///
/// WHAT get_param MUST NOT DO: it runs on the audio callback too, so it does
/// not allocate and it does not compute. Everything it reports that costs
/// arithmetic is published once per block by the process call.
///
/// # Safety
/// `c` is null or a live engine; `key` is null or NUL-terminated; `buf` is null
/// or writable for `buf_len` bytes.
#[no_mangle]
pub unsafe extern "C" fn sc_core_get_param(
    c: *const ScCore,
    key: *const c_char,
    buf: *mut c_char,
    buf_len: c_int,
) -> c_int {
    let Some(c) = c.as_ref() else { return -1 };
    if key.is_null() || buf.is_null() || buf_len <= 0 {
        return -1;
    }
    let out = std::slice::from_raw_parts_mut(buf as *mut u8, buf_len as usize);
    c.0.get_param(s(key), out)
}

/* -------------------------------------------------- audio-thread reads */

/// The label of rate `index`, NUL-terminated into `buf`: the engine's own
/// table, so a host's menu cannot disagree with it. Returns the length
/// written, or -1 past the end of the table or for a buffer too small.
///
/// # Safety
/// `buf` is null or writable for `buf_len` bytes.
#[no_mangle]
pub unsafe extern "C" fn sc_core_rate_label(index: c_int, buf: *mut c_char, buf_len: c_int) -> c_int {
    let Some(rate) = usize::try_from(index).ok().and_then(|i| RATES.get(i)) else { return -1 };
    ni_dsp::ffi::copy_cstr(rate.label, buf, buf_len)
}

/// The rate a fresh instance plays, as an index into the table.
#[no_mangle]
pub extern "C" fn sc_core_rate_default() -> c_int {
    RATE_DEFAULT as c_int
}

/// Cycle phase, 0..1. The allocation-free answer, for a caller that wants the
/// playhead without parsing the `ui` readout.
///
/// # Safety
/// `c` is null or a live engine.
#[no_mangle]
pub unsafe extern "C" fn sc_core_phase01(c: *const ScCore) -> f64 {
    c.as_ref().map(|c| c.0.phase01()).unwrap_or(0.0)
}

/// Where the display window has got to, 0..1. THE WINDOW IS ONE CYCLE LONG,
/// and this is the axis both the shape editor and the signal scope are drawn
/// on -- which is what makes them one picture rather than two stacked ones.
///
/// One definition for all three sources: on Cycle it is the transport's phase;
/// on MIDI and Sidechain, which have no transport phase, it is the time since
/// the last trigger over one cycle. It SATURATES at 1.0 rather than wrapping,
/// so a source that has not fired again parks at the right-hand edge instead
/// of drawing a dip that never happened.
///
/// The shell indexes its capture columns by this. A column is therefore
/// written once per cycle -- twice a second at 1/4 and 120 bpm, which reads as
/// a live waveform; once every four seconds at 1/1 and 60 bpm, where the
/// picture really is that old. The alternative is a rolling window that does
/// not line up with the editor, which is a worse picture that merely looks
/// fresher.
///
/// # Safety
/// `c` is null or a live engine.
#[no_mangle]
pub unsafe extern "C" fn sc_core_sweep01(c: *const ScCore) -> f64 {
    c.as_ref().map(|c| c.0.sweep01()).unwrap_or(1.0)
}

/// The attenuation as of the last sample rendered, 0..1. What the meter shows,
/// and what the scope's gain-reduction trace is built from.
///
/// # Safety
/// `c` is null or a live engine.
#[no_mangle]
pub unsafe extern "C" fn sc_core_duck(c: *const ScCore) -> f32 {
    c.as_ref().map(|c| c.0.duck_now()).unwrap_or(0.0)
}

/// Monotonic trigger count. The UI watches it CHANGE, which is how "nothing
/// has fired for 500 ms" is answered without the engine owning a clock.
///
/// # Safety
/// `c` is null or a live engine.
#[no_mangle]
pub unsafe extern "C" fn sc_core_fires(c: *const ScCore) -> u32 {
    c.as_ref().map(|c| c.0.fires()).unwrap_or(0)
}

/* ------------------------------------------------------- the single shot */

/// The duck one trigger makes when nothing interrupts it, from these
/// parameters in the engine's units: what the plugin's shape well draws
/// (sc-core's `single.rs`). The four stages are percentages of the cycle,
/// `depth` 0..1, `curve` sc_param_t's SC_P_CURVE value, and `cycle` non-zero
/// when the source is Cycle, whose Delay wraps.
#[repr(C)]
pub struct ScShape {
    pub curve: c_int,
    pub delay: f64,
    pub attack: f64,
    pub hold: f64,
    pub release: f64,
    pub depth: f64,
    pub cycle: c_int,
}

/// Where its corners sit, as phases 0..100 (wrapped on Cycle): where the duck
/// starts, reaches the bottom, leaves it and is back. `span` is the three
/// stages' sum unwrapped, which says whether the duck can finish inside one
/// cycle; `floor` is the gain at the bottom, 1 - depth.
#[repr(C)]
pub struct ScShapeMarks {
    pub start: f64,
    pub bottom: f64,
    pub hold_end: f64,
    pub end: f64,
    pub span: f64,
    pub floor: f64,
}

fn single(s: &ScShape) -> sc_core::single::Single {
    sc_core::single::Single {
        curve: sc_core::shape::Curve::from_i32(s.curve),
        delay: s.delay,
        attack: s.attack,
        hold: s.hold,
        release: s.release,
        depth: s.depth,
        cycle: s.cycle != 0,
    }
}

/// The shot's gain, Depth applied, at `count` phases evenly spaced from 0 to
/// 100 % of the cycle, both ends included. Needs no engine, allocates nothing;
/// any thread.
///
/// # Safety
/// `s` is null or valid; `gain` is null or holds `count` floats.
#[no_mangle]
pub unsafe extern "C" fn sc_shape_render(s: *const ScShape, gain: *mut f32, count: c_int) {
    let Some(s) = s.as_ref() else { return };
    if gain.is_null() || count <= 0 {
        return;
    }
    single(s).render(std::slice::from_raw_parts_mut(gain, count as usize));
}

/// The shot's corners into `out`. Returns 0, or -1 for a null argument.
///
/// # Safety
/// `s` and `out` are null or valid.
#[no_mangle]
pub unsafe extern "C" fn sc_shape_marks(s: *const ScShape, out: *mut ScShapeMarks) -> c_int {
    let (Some(s), Some(out)) = (s.as_ref(), out.as_mut()) else { return -1 };
    let m = single(s).marks();
    *out = ScShapeMarks {
        start: m.start,
        bottom: m.bottom,
        hold_end: m.hold_end,
        end: m.end,
        span: m.span,
        floor: m.floor,
    };
    0
}

/* ------------------------------------------------------------ test hooks */

/// The shape, reachable from C so `tests/shape_table.c` can generate the
/// fixture that the C and the JavaScript copies are each pinned to.
///
/// NOT FOR THE PLUGIN. It exists so the oracle is generated BY THE ENGINE
/// rather than transcribed from it, which is the only version of that test
/// worth having.
///
/// THESE TOOK A DIRECTION AND NO LONGER DO. The asymmetric `Pump` curve is
/// gone, and every remaining curve reads the same in both directions -- so the
/// argument would be one every caller passes and no curve reads.
#[no_mangle]
pub extern "C" fn sc_test_shape(curve: c_int, t: f64) -> f64 {
    sc_core::shape::shape(sc_core::shape::Curve::from_i32(curve), t)
}

/// `sc_test_shape`'s inverse: the progress at which `curve` reaches `w`.
#[no_mangle]
pub extern "C" fn sc_test_shape_inv(curve: c_int, w: f64) -> f64 {
    sc_core::shape::shape_inv(sc_core::shape::Curve::from_i32(curve), w)
}

impl ScCore {
    /// Reach the engine without going back out through C.
    ///
    /// `sc-move` needs this: it links this crate for the `sc_core_*`
    /// symbols the tests use, and drives the same instance through the Schwung
    /// vtable. Round-tripping through the C ABI to do it would mean formatting
    /// numbers into strings and parsing them back.
    pub fn inner(&mut self) -> &mut Instance {
        &mut self.0
    }
}

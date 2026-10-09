// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The `tg_core_*` C ABI -- the engine, with no host in it -- and the source of
trance_gate_core.h, which build.rs generates from this file (cbindgen's
configuration is cbindgen/trance_gate_core.toml). What is declared here is
what C sees: every `pub const` a `#define`, [`TgParam`] the `tg_param_t` enum,
each `#[no_mangle]` function a prototype, with these doc comments above them.

WHY THIS EXISTS. The Trance Gate runs in two shells: Schwung's chain on Move,
and a plugin in a DAW. A second implementation of the envelope would drift
from the first within a month, and the bug reads as "it sounds different in
Live" -- which is the hardest kind to chase. So there is one engine and two
shells, and this is the engine.

WHAT MOVED OUT OF THE SHELL. Three things were host-shaped and are parameters
of the core instead:

  SAMPLE RATE was a #define of 44100. A DAW runs at 48k or 96k, where that
  constant makes a 1/16 step 8.8% or 118% too long.

  AUDIO FORMAT was int16 interleaved, which is what the Move mailbox carries.
  Plugins are float. Both paths are here and must agree.

  TRANSPORT was two host callbacks. It is a struct the shell fills
  (`tg_transport_t`, ni-dsp's [`CTransport`]), so the engine cannot reach for
  a global that a plugin has no way to provide.

THE PARAMETER API IS DELIBERATELY STRINGS. It is what Schwung's chain already
speaks, and it is what makes a patch portable: `state` emits a JSON blob that
the other shell parses back byte for byte. The automatable values have a
numeric door beside it as well, [`tg_core_set_num`], for the audio thread.
*/

use crate::{envelope, gate};
use ni_dsp::ffi::{cstr as s, CTransport};
use ni_dsp::sweep::CycleSweep;
use std::ffi::{c_char, c_int};
use tg_core::params::Param;
use tg_core::rates::{RATES, RATE_DEFAULT};
use tg_core::Instance;

/// 128 steps is eight bars at 1/16. The masks are the reason this is a number
/// and not "as many as you like": a pattern is two bitmaps and a depth per
/// step, and all of it has to fit a state blob.
pub const TG_MAX_STEPS: usize = 128;
/// The pattern slots a patch holds.
pub const TG_SLOTS: usize = 8;

/*
 * THE RATE LADDER AND THE STAGE SCALE, for embedders that have to declare host
 * parameters before any audio runs: a host wants the option count and the
 * default at construction time, long before it will accept an answer from
 * get_param. Each is asserted against the engine below, at compile time, so a
 * number here cannot drift from the Rust table; tests/test_core.c walks the
 * ladder from C as well.
 */
/// The number of rates in the ladder (`tg_core_rate_label`).
pub const TG_NUM_RATES: usize = 13;
/// The rate a fresh instance plays: 1/16.
pub const TG_RATE_DEFAULT: usize = 7;
/// A stage runs from 0 to twice the gate's WIDTH, as a percentage of it.
// Cast for C, as a float: cbindgen drops a literal's f32 suffix and would
// write a double. spectro-capi's analyzer.rs says more.
#[allow(clippy::unnecessary_cast, reason = "the cast is the C type: cbindgen writes it as (float), and drops an f32 suffix")]
pub const TG_STAGE_MAX_PCT: f32 = 200.0 as f32;
/// The 32-bit words of one step bitmap ([`TgMask`]).
#[allow(clippy::manual_div_ceil, reason = "cbindgen writes this expression into the header, and C has no div_ceil")]
pub const TG_MASK_WORDS: usize = (TG_MAX_STEPS + 31) / 32;

/// The pattern plot's buffer: size `tg_core_render_gate`'s with this.
pub const TG_GATE_MAX: usize = 4096;
/// The envelope plot's buffer: size `tg_core_render_envelope`'s with this.
pub const TG_ENVELOPE_MAX: usize = 1024;

/// SIZE YOUR get_param BUFFER FROM THIS, DO NOT PICK A NUMBER.
///
/// The longest thing the engine emits is the "state" blob, and it has grown
/// twice. TG_MAX_STEPS took eight slots of fully accented 128-step patterns to
/// ~2.6 KB, where 32-step ones were a few hundred bytes; the fade-in's per-step
/// ARRIVAL ORDER is a second array of the same size, which takes the worst case
/// to ~4.8 KB and is why this number moved from 4096 to 8192.
///
/// A shell that had guessed 2048 would not fail -- get_param TRUNCATES, and a
/// truncated patch is a project that silently reloads with the wrong pattern.
///
/// 8192 IS ALSO SCHWUNG'S AUDIO-FX CAP, so this is the whole budget rather than
/// a number with room above it. The next array added to a pattern does not fit,
/// and the answer then is a denser encoding rather than a bigger buffer.
///
/// REALISTIC patches are far smaller, and deliberately: neither the depths nor
/// the orders are written while they hold their default, so a patch with no
/// accents and no reordering still fits a bus insert's 1024 bytes. That is a
/// property of the emitter, and tests/test_core.c measures all three cases --
/// plain, accented, and accented with a shuffled order -- against the real
/// emitter rather than a conservative bound over it.
pub const TG_STATE_MAX: usize = 8192;

/// The envelope's curve shapes, as `tg_test_shape` takes them: exposed for the
/// tests, since the properties every stage depends on are cheaper to assert
/// here than to infer from rendered audio.
pub const TG_CURVE_LINEAR: c_int = 0;
pub const TG_CURVE_EXP: c_int = 1;
pub const TG_CURVE_SCURVE: c_int = 2;

/* THE NUMBERS ABOVE ARE THE ENGINE'S, checked where they are compiled: C reads
 * them as literals, and a literal is all cbindgen can write. */
const _: () = {
    use ni_dsp::curve::Curve;
    assert!(TG_MAX_STEPS == tg_core::MAX_STEPS);
    assert!(TG_SLOTS == tg_core::SLOTS);
    assert!(TG_NUM_RATES == RATES.len());
    assert!(TG_RATE_DEFAULT == RATE_DEFAULT);
    assert!(TG_STAGE_MAX_PCT == tg_core::STAGE_MAX_PCT);
    assert!(TG_MASK_WORDS == tg_core::mask::MASK_WORDS);
    assert!(TG_CURVE_LINEAR == Curve::Linear as c_int);
    assert!(TG_CURVE_EXP == Curve::Exp as c_int);
    assert!(TG_CURVE_SCURVE == Curve::SCurve as c_int);
};

/// A step bitmap, as C sees one. This was a bare uint32_t while 32 steps was
/// the ceiling, and widening it was the invasive half of going to 128: the
/// compiler cannot find `(p->steps >> i) & 1` for you once the type still has
/// a `>>`. Wrapping it in a struct is deliberate -- it makes every direct
/// shift a compile error, so the audit is done by the build rather than by
/// grep. The header's `tg_mask_get`, `tg_mask_set` and `tg_mask_zero` read
/// and write it (cbindgen/trance_gate_core.toml carries them: C has no Rust
/// to generate them from).
#[repr(C)]
pub struct TgMask {
    pub w: [u32; TG_MASK_WORDS],
}

/// THE AUTOMATABLE PARAMETERS, BY NUMBER.
///
/// tg_core_set_param is the canonical door and takes strings, which is right
/// for a patch, a pattern or a pad edit -- all of them message-thread work. It
/// is wrong for HOST AUTOMATION, which arrives on the audio thread: a float
/// formatted and parsed back costs a locale-dependent conversion in each
/// direction (atof honours LC_NUMERIC, so a comma-decimal host turns "0.750"
/// into 0) and a string compare per key, per value, per block.
///
/// These are the same fifteen values on the same wire conventions -- slot,
/// length and rate are INDICES, legato and time_mode are 0|1, the rest are the
/// units the string keys use -- with the decimal detour removed. The string
/// setter is implemented in terms of this one, so every clamp exists once.
///
/// THE VALUES ARE THE ABI. A host that saved an automation lane saved these
/// numbers, so inserting one in the middle silently rewires a user's project:
/// they are tg_core's [`Param`], asserted equal below.
///
/// Rust never receives this type. A C caller can pass any int where it is
/// declared, and an int outside the enum in a Rust enum is undefined
/// behaviour, so [`tg_core_set_num`] takes the plain [`TgParamArg`] and
/// drops what [`Param::from_i32`] does not know; the header names that
/// argument's type `tg_param_t`, as it always did (cbindgen's export.rename).
#[repr(C)]
#[allow(non_camel_case_types, reason = "the variants are the C names, which C callers spell")]
pub enum TgParam {
    TG_P_SLOT = 0,
    TG_P_LENGTH,
    TG_P_RATE,
    TG_P_LEGATO,
    TG_P_TIME_MODE,
    TG_P_CURVE,
    TG_P_AMOUNT,
    TG_P_HOLD,
    TG_P_ATTACK,
    TG_P_DECAY,
    TG_P_SUSTAIN,
    TG_P_RELEASE,
    /// APPENDED. These values are the ABI, so the fade's three can only go on
    /// the end -- not beside TG_P_AMOUNT, where they belong by meaning.
    TG_P_FADE,
    TG_P_FADE_SOFT,
    TG_P_FADE_DIR,
    TG_P_COUNT,
}

/// A `tg_param_t` as it arrives: any int at all.
pub type TgParamArg = c_int;

const _: () = {
    use TgParam::*;
    assert!(TG_P_SLOT as i32 == Param::Slot as i32);
    assert!(TG_P_LENGTH as i32 == Param::Length as i32);
    assert!(TG_P_RATE as i32 == Param::Rate as i32);
    assert!(TG_P_LEGATO as i32 == Param::Legato as i32);
    assert!(TG_P_TIME_MODE as i32 == Param::TimeMode as i32);
    assert!(TG_P_CURVE as i32 == Param::Curve as i32);
    assert!(TG_P_AMOUNT as i32 == Param::Amount as i32);
    assert!(TG_P_HOLD as i32 == Param::Hold as i32);
    assert!(TG_P_ATTACK as i32 == Param::Attack as i32);
    assert!(TG_P_DECAY as i32 == Param::Decay as i32);
    assert!(TG_P_SUSTAIN as i32 == Param::Sustain as i32);
    assert!(TG_P_RELEASE as i32 == Param::Release as i32);
    assert!(TG_P_FADE as i32 == Param::Fade as i32);
    assert!(TG_P_FADE_SOFT as i32 == Param::FadeSoft as i32);
    assert!(TG_P_FADE_DIR as i32 == Param::FadeDir as i32);
    assert!(TG_P_COUNT as i32 == Param::FadeDir as i32 + 1);
};

/// Opaque to C, exactly as `tg_core_t` was.
pub struct TgCore {
    pub(crate) engine: Instance,
    /// The scope's sweep, which `tg_core_process_f32_split_tap` keeps beside
    /// the engine.
    pub(crate) sweep: CycleSweep,
    /// How many times a shell command has replaced the current slot's sound
    /// wholesale (a paste): the plugin shell's cue that the host's parameters
    /// must follow the engine.
    #[cfg(feature = "shell")]
    pub(crate) recalls: u32,
    /// The host's values as the plugin shell last pushed them: the engine's
    /// own record, published with it (src/shell.rs, `Pushed`).
    #[cfg(feature = "shell")]
    pub(crate) pushed: crate::shell::Pushed,
}

impl TgCore {
    pub fn new(sample_rate: f64) -> Self {
        TgCore {
            engine: Instance::new(sample_rate),
            sweep: CycleSweep::default(),
            #[cfg(feature = "shell")]
            recalls: 0,
            #[cfg(feature = "shell")]
            pushed: crate::shell::Pushed::NONE,
        }
    }
}

/// One cycle of the pattern in ms, for the scope's axis and its free-running
/// sweep. A second while the engine has no step length to measure it by.
pub(crate) fn scope_cycle_ms(inst: &Instance) -> f64 {
    let ms = inst.cycle_ms();
    if ms > 1.0 { ms } else { 1000.0 }
}

/// The transport as `trance_gate_core.h` declares it, `tg_transport_t`.
pub use ni_dsp::ffi::CTransport as TgTransport;

/// A new engine at `sample_rate`. Never on the audio thread: it allocates.
#[no_mangle]
pub extern "C" fn tg_core_create(sample_rate: f64) -> *mut TgCore {
    Box::into_raw(Box::new(TgCore::new(sample_rate)))
}

/// # Safety
/// `c` is null or from `tg_core_create`, and is not used afterwards.
#[no_mangle]
pub unsafe extern "C" fn tg_core_destroy(c: *mut TgCore) {
    if !c.is_null() {
        drop(Box::from_raw(c));
    }
}

/// Safe to call from the audio thread: it only recomputes derived lengths.
///
/// # Safety
/// `c` is null or a live engine that no other call is using.
#[no_mangle]
pub unsafe extern "C" fn tg_core_set_sample_rate(c: *mut TgCore, sample_rate: f64) {
    let Some(c) = c.as_mut() else { return };
    c.engine.set_sample_rate(sample_rate);
}

/// # Safety
/// `c` is null or a live engine.
#[no_mangle]
pub unsafe extern "C" fn tg_core_get_sample_rate(c: *const TgCore) -> f64 {
    c.as_ref().map_or(0.0, |c| c.engine.sample_rate())
}

/// The canonical door: one key and its value, as text -- a patch, a pattern,
/// a pad edit. Message-thread work; the audio thread's door is
/// [`tg_core_set_num`].
///
/// # Safety
/// `c` is null or a live engine that no other call is using; `key` and `val`
/// are each null or NUL-terminated.
#[no_mangle]
pub unsafe extern "C" fn tg_core_set_param(
    c: *mut TgCore,
    key: *const c_char,
    val: *const c_char,
) {
    let Some(c) = c.as_mut() else { return };
    if key.is_null() || val.is_null() {
        return;
    }
    c.engine.set_param(s(key), s(val));
}

/// The fifteen automatable values by number, for host automation arriving on
/// the audio thread (see `tg_param_t`). Audio-thread safe: a match, a clamp
/// and a store -- no allocation, no formatting, no locale.
///
/// A parameter outside the enum is DROPPED, not clamped onto a neighbour: one
/// silently moving a different control is worse than one doing nothing.
///
/// # Safety
/// `c` is null or a live engine that no other call is using.
#[no_mangle]
pub unsafe extern "C" fn tg_core_set_num(c: *mut TgCore, param: TgParamArg, value: f64) {
    let Some(c) = c.as_mut() else { return };
    let Some(p) = Param::from_i32(param) else { return };
    c.engine.set_num(p, value);
}

/// `key`'s readout into `buf`, NUL-terminated. Returns the length written, or
/// -1 for a key this engine does not serve -- which is how a shell knows to
/// answer its own (chain_params, ui_hierarchy). Size `buf` with TG_STATE_MAX.
///
/// # Safety
/// `c` is null or a live engine; `key` is null or NUL-terminated; `buf` is null
/// or writable for `buf_len` bytes.
#[no_mangle]
pub unsafe extern "C" fn tg_core_get_param(
    c: *mut TgCore,
    key: *const c_char,
    buf: *mut c_char,
    buf_len: c_int,
) -> c_int {
    let Some(c) = c.as_ref() else { return -1 };
    if key.is_null() || buf.is_null() || buf_len <= 0 {
        return -1;
    }
    let out = std::slice::from_raw_parts_mut(buf as *mut u8, buf_len as usize);
    c.engine.get_param(s(key), out)
}

/// The host's time signature. Changes no sample -- the `params` readout's
/// Length detents are counted in it. A meter no host could mean, or 0/0 for
/// "the host did not say", is 4/4. Audio thread, allocation-free.
///
/// # Safety
/// `c` is null or a live engine that no other call is using.
#[no_mangle]
pub unsafe extern "C" fn tg_core_set_meter(c: *mut TgCore, num: c_int, den: c_int) {
    if let Some(c) = c.as_mut() {
        c.engine.set_meter(num, den);
    }
}

/// Ignored: the engine has never used MIDI; the Move shell claims CCs for the
/// arrows and handles them in its UI layer. Kept because it is part of the
/// published surface.
///
/// # Safety
/// Sound for any arguments: it reads none of them.
#[no_mangle]
pub unsafe extern "C" fn tg_core_on_midi(c: *mut TgCore, msg: *const u8, len: c_int) {
    let _ = (c, msg, len);
}

/// One block of interleaved stereo, in place. The int16 path is Move's; the
/// float path is every plugin format's. They run the same maths and must
/// agree.
///
/// # Safety
/// `c` is null or a live engine that no other call is using; `lr` is null or
/// holds `2 * frames` interleaved samples; `t` is null or a valid transport.
#[no_mangle]
pub unsafe extern "C" fn tg_core_process_i16(
    c: *mut TgCore,
    lr: *mut i16,
    frames: c_int,
    t: *const CTransport,
) {
    let Some(c) = c.as_mut() else { return };
    if lr.is_null() || frames <= 0 {
        return;
    }
    let buf = std::slice::from_raw_parts_mut(lr, frames as usize * 2);
    c.engine.process_i16(buf, frames as usize, CTransport::read(t).as_ref());
}

/// `tg_core_process_i16`, in float.
///
/// # Safety
/// `c` is null or a live engine that no other call is using; `lr` is null or
/// holds `2 * frames` interleaved samples; `t` is null or a valid transport.
#[no_mangle]
pub unsafe extern "C" fn tg_core_process_f32(
    c: *mut TgCore,
    lr: *mut f32,
    frames: c_int,
    t: *const CTransport,
) {
    let Some(c) = c.as_mut() else { return };
    if lr.is_null() || frames <= 0 {
        return;
    }
    let buf = std::slice::from_raw_parts_mut(lr, frames as usize * 2);
    c.engine.process_f32(buf, frames as usize, CTransport::read(t).as_ref());
}

/// Non-interleaved, which is what VST3 and AU actually hand you.
///
/// # Safety
/// `c` is null or a live engine that no other call is using; `l` and `r` are
/// each null or hold `frames` samples; `t` is null or a valid transport.
#[no_mangle]
pub unsafe extern "C" fn tg_core_process_f32_split(
    c: *mut TgCore,
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
    c.engine.process_f32_split(lb, rb, n, CTransport::read(t).as_ref());
}

/// `tg_core_process_f32_split`, and where in the pattern's cycle each sample
/// fell, 0..1, into `sweep` -- the scope's x-axis. A stopped or seeking
/// transport free-runs the sweep at one cycle per cycle length (see
/// `ni_dsp::sweep`). `sweep` may be null.
///
/// # Safety
/// As `tg_core_process_f32_split`, and `sweep` is null or writable for `frames`
/// floats.
#[no_mangle]
pub unsafe extern "C" fn tg_core_process_f32_split_tap(
    c: *mut TgCore,
    l: *mut f32,
    r: *mut f32,
    sweep: *mut f32,
    frames: c_int,
    t: *const CTransport,
) {
    let Some(c) = c.as_mut() else { return };
    if l.is_null() || r.is_null() || frames <= 0 {
        return;
    }
    let n = frames as usize;
    let transport = CTransport::read(t);
    let ph0 = c.engine.phase01();
    c.engine.process_f32_split(
        std::slice::from_raw_parts_mut(l, n),
        std::slice::from_raw_parts_mut(r, n),
        n,
        transport.as_ref(),
    );
    if sweep.is_null() {
        return;
    }
    let ph1 = c.engine.phase01();
    let advancing = transport.is_some_and(|t| t.running);
    let cycle_samples = scope_cycle_ms(&c.engine) * c.engine.sample_rate() / 1000.0;
    c.sweep.fill(ph0, ph1, advancing, cycle_samples, std::slice::from_raw_parts_mut(sweep, n));
}

/// The playhead's position in the pattern, 0..1. Cheap and allocation-free --
/// for a caller on the audio thread, where the equivalent get_param key's
/// formatting does not belong.
///
/// # Safety
/// `c` is null or a live engine.
#[no_mangle]
pub unsafe extern "C" fn tg_core_phase01(c: *const TgCore) -> f64 {
    c.as_ref().map_or(0.0, |c| c.engine.phase01())
}

/// The label of rate `index`, NUL-terminated into `buf`: the engine's own
/// table, so a host's menu cannot disagree with it. Returns the length
/// written, or -1 past the end of the table or for a buffer too small.
///
/// # Safety
/// `buf` is null or writable for `buf_len` bytes.
#[no_mangle]
pub unsafe extern "C" fn tg_core_rate_label(index: c_int, buf: *mut c_char, buf_len: c_int) -> c_int {
    let Some(rate) = usize::try_from(index).ok().and_then(|i| RATES.get(i)) else { return -1 };
    ni_dsp::ffi::copy_cstr(rate.label, buf, buf_len)
}

/// The pattern plot's curve for the patch in `state`: `"<length>:<per_step>:"`
/// then one raw byte of gain per sample of one cycle (see gate.rs). BINARY and
/// not NUL-terminated -- a gain of 0 is a 0 byte. Returns the number of bytes
/// written, or -1 for nothing to draw or a buffer too small -- size it with
/// TG_GATE_MAX. Allocates: never on the audio thread.
///
/// # Safety
/// `state` is null or NUL-terminated; `buf` is null or writable for `buf_len`
/// bytes.
#[no_mangle]
pub unsafe extern "C" fn tg_core_render_gate(state: *const c_char, buf: *mut c_char, buf_len: c_int) -> c_int {
    copy_out(gate::render(s(state)), buf, buf_len)
}

/// The envelope plot's two curves for the patch in `state` (see envelope.rs):
/// `"<steps>:<per_step>:"`, then the gated curve and the envelope as dialled,
/// one raw byte of gain per sample each. BINARY, not a C string. Returns the
/// number of bytes written, or -1 for nothing to draw or a buffer too small --
/// size it with TG_ENVELOPE_MAX. Allocates: never on the audio thread.
///
/// # Safety
/// `state` is null or NUL-terminated; `buf` is null or writable for `buf_len`
/// bytes.
#[no_mangle]
pub unsafe extern "C" fn tg_core_render_envelope(state: *const c_char, buf: *mut c_char, buf_len: c_int) -> c_int {
    copy_out(envelope::render(s(state)), buf, buf_len)
}

/// A rendered plot into the caller's buffer, as both renders answer it: the
/// number of bytes written, or -1 for nothing to draw or a buffer too small.
/// Binary, so no NUL is added -- a gain of 0 is a 0 byte.
///
/// # Safety
/// `buf` is null or writable for `buf_len` bytes.
unsafe fn copy_out(bytes: Option<Vec<u8>>, buf: *mut c_char, buf_len: c_int) -> c_int {
    let Some(bytes) = bytes else { return -1 };
    if buf.is_null() || buf_len < 0 || bytes.len() > buf_len as usize {
        return -1;
    }
    std::ptr::copy_nonoverlapping(bytes.as_ptr(), buf.cast::<u8>(), bytes.len());
    bytes.len() as c_int
}

/// The rate a fresh instance plays, as an index into the table.
#[no_mangle]
pub extern "C" fn tg_core_rate_default() -> c_int {
    RATE_DEFAULT as c_int
}

/*
 * THE SHAPES, VISIBLE TO THE TESTS.
 *
 * The properties the whole envelope rests on -- endpoints, monotonicity, an
 * exact inverse -- are worth asserting directly rather than inferred from
 * rendered audio, where a broken shape would show up as "the gate sounds
 * odd".
 */
/// The warp curve `curve` (TG_CURVE_*) applies to a stage's 0..1 progress
/// `t`.
#[no_mangle]
pub extern "C" fn tg_test_shape(curve: c_int, t: f64) -> f64 {
    tg_core::envelope::shape(tg_core::envelope::Curve::from_i32(curve), t)
}

/// `tg_test_shape`'s inverse: the progress at which `curve` reaches `w`.
#[no_mangle]
pub extern "C" fn tg_test_shape_inv(curve: c_int, w: f64) -> f64 {
    tg_core::envelope::shape_inv(tg_core::envelope::Curve::from_i32(curve), w)
}

/// For the Move shell, which needs the same `Instance` without going back out
/// through C.
impl TgCore {
    pub fn inner(&mut self) -> &mut Instance {
        &mut self.engine
    }
}

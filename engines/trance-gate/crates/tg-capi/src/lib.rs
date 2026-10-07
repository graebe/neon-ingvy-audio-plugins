// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The `tg_core_*` C ABI.

Byte-for-byte the surface the C engine exported, so every existing caller --
the Schwung shell, the plugin, and the engine's C tests (tests/test_core.c,
tests/test_gate.c) -- links this instead without changing a line. That is the
whole point: the tests are not rewritten for the port, they are relinked, and
they are what says the port is correct.

# Safety

Every function here is `unsafe` in the C sense and safe in practice under the
contract the C had: a `tg_core_t*` is a pointer returned by
[`tg_core_create`] and not yet destroyed, and buffers are valid for the frame
counts given. Null is checked because the C checked it, and callers rely on
that; anything else is the caller's bargain, as it was before.
*/

/*
 * THE GROUND'S C ABI RIDES IN THIS ARCHIVE, and this line is what puts it there.
 *
 * `ground-capi` is an rlib holding the gnd_* entry points the editor's animated
 * background needs. It is not a static library of its own on purpose: two Rust
 * staticlibs in one binary duplicate the Rust runtime and fail to link, so this
 * repository keeps one archive per plugin (spectro-capi's Cargo.toml states the
 * rule). Naming the crate here is what makes rustc link it in, so the symbols
 * are exported from this archive rather than dropped as unreachable.
 *
 * It and the shell are the `shell` feature, the plugin's half of this crate:
 * the Move module has no editor and one thread, and builds without either
 * (see Cargo.toml).
 */
#[cfg(feature = "shell")]
use ground_capi as _;

/* The plugin shell's door to the engine; see shell.rs. */
#[cfg(feature = "shell")]
mod shell;
#[cfg(feature = "shell")]
pub use shell::TgShell;
/* The pattern plot's curve, rendered through a scratch engine; see gate.rs. */
mod gate;
mod envelope;

#[cfg(test)]
mod tests;

use ni_dsp::ffi::{cstr as s, CTransport};
use ni_dsp::sweep::CycleSweep;
use std::ffi::{c_char, c_int};
use tg_core::params::Param;
use tg_core::rates::{RATES, RATE_DEFAULT};
use tg_core::Instance;

/// Opaque to C, exactly as `tg_core_t` was.
pub struct TgCore {
    engine: Instance,
    /// The scope's sweep, which `tg_core_process_f32_split_tap` keeps beside
    /// the engine.
    sweep: CycleSweep,
    /// How many times a shell command has replaced the current slot's sound
    /// wholesale (a paste): the plugin shell's cue that the host's parameters
    /// must follow the engine.
    #[cfg(feature = "shell")]
    recalls: u32,
}

impl TgCore {
    pub fn new(sample_rate: f64) -> Self {
        TgCore {
            engine: Instance::new(sample_rate),
            sweep: CycleSweep::default(),
            #[cfg(feature = "shell")]
            recalls: 0,
        }
    }
}

/// One cycle of the pattern in ms, for the scope's axis and its free-running
/// sweep. A second while the engine has no step length to measure it by.
pub(crate) fn scope_cycle_ms(inst: &Instance) -> f64 {
    let ms = inst.cycle_ms();
    if ms > 1.0 { ms } else { 1000.0 }
}

/// The transport as `trance_gate_core.h` declares it.
pub use ni_dsp::ffi::CTransport as TgTransport;

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

/// `tg_core_set_num`: the fifteen automatable values by number, for host
/// automation arriving on the audio thread. See [`tg_core::params::Param`] --
/// the discriminants are the ABI, so a host that saved an automation lane
/// saved these integers.
///
/// A parameter outside the enum is DROPPED, not clamped onto a neighbour: one
/// silently moving a different control is worse than one doing nothing.
///
/// # Safety
/// `c` is null or a live engine that no other call is using.
#[no_mangle]
pub unsafe extern "C" fn tg_core_set_num(c: *mut TgCore, param: c_int, value: f64) {
    let Some(c) = c.as_mut() else { return };
    let Some(p) = Param::from_i32(param) else { return };
    c.engine.set_num(p, value);
}

/// Returns the length written, or -1 for a key this engine does not serve --
/// which is how a shell knows to answer its own.
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

/// # Safety
/// Sound for any arguments: it reads none of them.
#[no_mangle]
pub unsafe extern "C" fn tg_core_on_midi(_c: *mut TgCore, _msg: *const u8, _len: c_int) {
    /* The engine has never used MIDI; the Move shell claims CCs for the
     * arrows and handles them in its UI layer. Kept because it is part of the
     * published surface. */
}

/// # Safety
/// `c` is null or a live engine that no other call is using; `lr` is null or
/// holds `2 * frames` interleaved samples; `t` is null or a valid transport.
#[no_mangle]
pub unsafe extern "C" fn tg_core_process_i16(
    c: *mut TgCore,
    lr: *mut i16,
    frames: c_int,
    t: *const TgTransport,
) {
    let Some(c) = c.as_mut() else { return };
    if lr.is_null() || frames <= 0 {
        return;
    }
    let buf = std::slice::from_raw_parts_mut(lr, frames as usize * 2);
    c.engine.process_i16(buf, frames as usize, CTransport::read(t).as_ref());
}

/// # Safety
/// `c` is null or a live engine that no other call is using; `lr` is null or
/// holds `2 * frames` interleaved samples; `t` is null or a valid transport.
#[no_mangle]
pub unsafe extern "C" fn tg_core_process_f32(
    c: *mut TgCore,
    lr: *mut f32,
    frames: c_int,
    t: *const TgTransport,
) {
    let Some(c) = c.as_mut() else { return };
    if lr.is_null() || frames <= 0 {
        return;
    }
    let buf = std::slice::from_raw_parts_mut(lr, frames as usize * 2);
    c.engine.process_f32(buf, frames as usize, CTransport::read(t).as_ref());
}

/// # Safety
/// `c` is null or a live engine that no other call is using; `l` and `r` are
/// each null or hold `frames` samples; `t` is null or a valid transport.
#[no_mangle]
pub unsafe extern "C" fn tg_core_process_f32_split(
    c: *mut TgCore,
    l: *mut f32,
    r: *mut f32,
    frames: c_int,
    t: *const TgTransport,
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
    t: *const TgTransport,
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
#[no_mangle]
pub extern "C" fn tg_test_shape(curve: c_int, t: f64) -> f64 {
    tg_core::envelope::shape(tg_core::envelope::Curve::from_i32(curve), t)
}

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

/*!
The `tg_core_*` C ABI.

Fourteen symbols, byte-for-byte the surface the C engine exported, so every
existing caller -- the Schwung shell, the JUCE plugin, and 1,510 lines of C
tests -- links this instead without changing a line. That is the whole point:
the tests are not rewritten for the port, they are relinked, and they are what
says the port is correct.

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
 */
use ground_capi as _;

/* The plugin shell's door to the engine; see shell.rs. */
mod shell;
pub use shell::TgShell;
/* The pattern plot's curve, rendered through a scratch engine; see gate.rs. */
mod gate;

use ni_dsp::ffi::{cstr as s, CTransport};
use ni_dsp::sweep::CycleSweep;
use std::ffi::{c_char, c_int};
use tg_core::params::Param;
use tg_core::rates::{RATES, RATE_DEFAULT};
use tg_core::Instance;

/// Opaque to C, exactly as `tg_core_t` was: the engine, and the scope sweep
/// `tg_core_process_f32_split_tap` keeps beside it.
pub struct TgCore(Instance, CycleSweep);

impl TgCore {
    pub fn new(sample_rate: f64) -> Self {
        TgCore(Instance::new(sample_rate), CycleSweep::default())
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

#[no_mangle]
pub unsafe extern "C" fn tg_core_destroy(c: *mut TgCore) {
    if !c.is_null() {
        drop(Box::from_raw(c));
    }
}

#[no_mangle]
pub unsafe extern "C" fn tg_core_set_sample_rate(c: *mut TgCore, sample_rate: f64) {
    let Some(c) = c.as_mut() else { return };
    c.0.set_sample_rate(sample_rate);
}

#[no_mangle]
pub unsafe extern "C" fn tg_core_get_sample_rate(c: *const TgCore) -> f64 {
    c.as_ref().map_or(0.0, |c| c.0.sample_rate())
}

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
    c.0.set_param(s(key), s(val));
}

/// `tg_core_set_num`: the fifteen automatable values by number, for host
/// automation arriving on the audio thread. See [`tg_core::params::Param`] --
/// the discriminants are the ABI, so a host that saved an automation lane
/// saved these integers.
///
/// A parameter outside the enum is DROPPED, not clamped onto a neighbour: one
/// silently moving a different control is worse than one doing nothing.
#[no_mangle]
pub unsafe extern "C" fn tg_core_set_num(c: *mut TgCore, param: c_int, value: f64) {
    let Some(c) = c.as_mut() else { return };
    let Some(p) = Param::from_i32(param) else { return };
    c.0.set_num(p, value);
}

/// Returns the length written, or -1 for a key this engine does not serve --
/// which is how a shell knows to answer its own.
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
    c.0.get_param(s(key), out)
}

#[no_mangle]
pub unsafe extern "C" fn tg_core_on_midi(_c: *mut TgCore, _msg: *const u8, _len: c_int) {
    /* The engine has never used MIDI; the Move shell claims CCs for the
     * arrows and handles them in its UI layer. Kept because it is part of the
     * published surface. */
}

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
    c.0.process_i16(buf, frames as usize, CTransport::read(t).as_ref());
}

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
    c.0.process_f32(buf, frames as usize, CTransport::read(t).as_ref());
}

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
    c.0.process_f32_split(lb, rb, n, CTransport::read(t).as_ref());
}

/// `tg_core_process_f32_split`, and where in the pattern's cycle each sample
/// fell, 0..1, into `sweep` -- the scope's x-axis. A stopped or seeking
/// transport free-runs the sweep at one cycle per cycle length (see
/// `ni_dsp::sweep`). `sweep` may be null.
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
    let ph0 = c.0.phase01();
    c.0.process_f32_split(
        std::slice::from_raw_parts_mut(l, n),
        std::slice::from_raw_parts_mut(r, n),
        n,
        transport.as_ref(),
    );
    if sweep.is_null() {
        return;
    }
    let ph1 = c.0.phase01();
    let advancing = transport.is_some_and(|t| t.running);
    let cycle_samples = scope_cycle_ms(&c.0) * c.0.sample_rate() / 1000.0;
    c.1.fill(ph0, ph1, advancing, cycle_samples, std::slice::from_raw_parts_mut(sweep, n));
}

#[no_mangle]
pub unsafe extern "C" fn tg_core_phase01(c: *const TgCore) -> f64 {
    c.as_ref().map_or(0.0, |c| c.0.phase01())
}

/// The label of rate `index`, NUL-terminated into `buf`: the engine's own
/// table, so a host's menu cannot disagree with it. Returns the length
/// written, or -1 past the end of the table or for a buffer too small.
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
#[no_mangle]
pub unsafe extern "C" fn tg_core_render_gate(state: *const c_char, buf: *mut c_char, buf_len: c_int) -> c_int {
    let Some(bytes) = gate::render(s(state)) else { return -1 };
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
        &mut self.0
    }
}

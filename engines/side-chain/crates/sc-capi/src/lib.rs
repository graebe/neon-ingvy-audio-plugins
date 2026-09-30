/*!
The `sc_core_*` C ABI.

WHAT THIS CRATE IS ALLOWED TO DO: null-check, bound-check, and hand the call
to `sc-core`. Nothing here decides anything about the DSP. If a function
below grows a branch that is not about the C boundary, it is in the wrong file.

THE HEADER IS THE CONTRACT AND IT IS WRITTEN BY HAND. There is no cbindgen. If
`engines/side-chain/include/sc_core.h` and this file disagree, they disagree
SILENTLY -- a mismatched signature is a link that succeeds and a call that
corrupts the stack. What keeps them honest is that the engine's C tests
`#include` that header and link this library, so a drift is a test failure.

EVERY ENTRY POINT RUNS ON AN AUDIO CALLBACK. No allocation, no locking, no
panicking -- the workspace sets `panic = "abort"` because unwinding out of
`extern "C"` into a C++ host is undefined behaviour.
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
pub use shell::ScShell;

use ni_dsp::ffi::{cstr as s, CTransport};
use sc_core::params::Param;
use sc_core::{Instance, MAX_BLOCK};
use std::os::raw::{c_char, c_int, c_uchar};

/// The opaque handle. A newtype rather than `Instance` directly so the C side
/// cannot be given a layout it might be tempted to rely on.
pub struct ScCore(Instance);

/// The transport as `sc_core.h` declares it.
pub use ni_dsp::ffi::CTransport as ScTransport;

/* ----------------------------------------------------------- lifecycle */

#[no_mangle]
pub extern "C" fn sc_core_create(sample_rate: f64) -> *mut ScCore {
    Box::into_raw(Box::new(ScCore(Instance::new(sample_rate))))
}

#[no_mangle]
pub unsafe extern "C" fn sc_core_destroy(c: *mut ScCore) {
    if !c.is_null() {
        drop(Box::from_raw(c));
    }
}

#[no_mangle]
pub unsafe extern "C" fn sc_core_set_sample_rate(c: *mut ScCore, sample_rate: f64) {
    if let Some(c) = c.as_mut() {
        c.0.set_sample_rate(sample_rate);
    }
}

#[no_mangle]
pub unsafe extern "C" fn sc_core_get_sample_rate(c: *const ScCore) -> f64 {
    c.as_ref().map(|c| c.0.sample_rate()).unwrap_or(0.0)
}

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
/// The engine does not infer this. An unconnected bus and a silent one are the
/// same block of zeroes, and the difference is what the UI has to report --
/// "no key" is a different message from "nothing is playing".
#[no_mangle]
pub unsafe extern "C" fn sc_core_set_key_connected(c: *mut ScCore, connected: c_int) {
    if let Some(c) = c.as_mut() {
        c.0.set_key_connected(connected != 0);
    }
}

/// Queue a MIDI message at `at`, a sample offset within the next block.
///
/// `at` IS NOT OPTIONAL TO GET RIGHT. Passing 0 for everything is what
/// `ducker.c` does and it costs up to a full buffer of jitter on the one event
/// whose timing is the whole effect. iPlug2 has it in `IMidiMsg::mOffset`; the
/// Schwung v2 `on_midi` has no such field and passes 0, which is also what
/// makes the Live/Move render A/B comparable.
#[no_mangle]
pub unsafe extern "C" fn sc_core_on_midi(
    c: *mut ScCore,
    msg: *const c_uchar,
    len: c_int,
    at: c_int,
) {
    let Some(c) = c.as_mut() else { return };
    if msg.is_null() || len <= 0 || len > 8 {
        return;
    }
    let bytes = std::slice::from_raw_parts(msg, len as usize);
    c.0.on_midi(bytes, at.max(0) as usize);
}

/* ------------------------------------------------------------- process */

#[no_mangle]
pub unsafe extern "C" fn sc_core_process_f32_split(
    c: *mut ScCore,
    l: *mut f32,
    r: *mut f32,
    frames: c_int,
    t: *const ScTransport,
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
#[no_mangle]
pub unsafe extern "C" fn sc_core_process_f32_split_tap(
    c: *mut ScCore,
    l: *mut f32,
    r: *mut f32,
    gain: *mut f32,
    sweep: *mut f32,
    frames: c_int,
    t: *const ScTransport,
) {
    let Some(c) = c.as_mut() else { return };
    if l.is_null() || r.is_null() || frames <= 0 {
        return;
    }
    let n = frames as usize;
    let lb = std::slice::from_raw_parts_mut(l, n);
    let rb = std::slice::from_raw_parts_mut(r, n);
    let mut gb = if gain.is_null() {
        None
    } else {
        Some(std::slice::from_raw_parts_mut(gain, n))
    };
    let mut sb = if sweep.is_null() {
        None
    } else {
        Some(std::slice::from_raw_parts_mut(sweep, n))
    };
    c.0.process_f32_split_tap(
        lb,
        rb,
        gb.as_deref_mut(),
        sb.as_deref_mut(),
        n,
        CTransport::read(t).as_ref(),
    );
}

#[no_mangle]
pub unsafe extern "C" fn sc_core_process_f32(
    c: *mut ScCore,
    lr: *mut f32,
    frames: c_int,
    t: *const ScTransport,
) {
    let Some(c) = c.as_mut() else { return };
    if lr.is_null() || frames <= 0 {
        return;
    }
    let n = frames as usize;
    let buf = std::slice::from_raw_parts_mut(lr, n * 2);
    c.0.process_f32(buf, n, CTransport::read(t).as_ref());
}

#[no_mangle]
pub unsafe extern "C" fn sc_core_process_i16(
    c: *mut ScCore,
    lr: *mut i16,
    frames: c_int,
    t: *const ScTransport,
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

#[no_mangle]
pub unsafe extern "C" fn sc_core_set_num(c: *mut ScCore, param: c_int, value: f64) {
    let Some(c) = c.as_mut() else { return };
    let Some(p) = Param::from_i32(param) else { return };
    c.0.set_num(p, value);
}

#[no_mangle]
pub unsafe extern "C" fn sc_core_get_num(c: *const ScCore, param: c_int) -> f64 {
    let Some(c) = c.as_ref() else { return 0.0 };
    let Some(p) = Param::from_i32(param) else {
        return 0.0;
    };
    c.0.num(p)
}

#[no_mangle]
pub unsafe extern "C" fn sc_core_set_param(
    c: *mut ScCore,
    key: *const c_char,
    val: *const c_char,
) -> c_int {
    let Some(c) = c.as_mut() else { return 0 };
    c.0.set_param(s(key), s(val)) as c_int
}

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

/// Cycle phase, 0..1. The allocation-free answer, for a caller that wants the
/// playhead without parsing the `ui` readout.
#[no_mangle]
pub unsafe extern "C" fn sc_core_phase01(c: *const ScCore) -> f64 {
    c.as_ref().map(|c| c.0.phase01()).unwrap_or(0.0)
}

/// Where the display window has got to, 0..1 -- one cycle long, and defined for
/// all three sources. The scope indexes its columns by this so the audio lands
/// on the same axis the editor draws the shape on.
#[no_mangle]
pub unsafe extern "C" fn sc_core_sweep01(c: *const ScCore) -> f64 {
    c.as_ref().map(|c| c.0.sweep01()).unwrap_or(1.0)
}

/// The attenuation as of the last sample rendered, 0..1. What the meter shows,
/// and what the scope's gain-reduction trace is built from.
#[no_mangle]
pub unsafe extern "C" fn sc_core_duck(c: *const ScCore) -> f32 {
    c.as_ref().map(|c| c.0.duck_now()).unwrap_or(0.0)
}

/// Monotonic trigger count. The UI watches it CHANGE, which is how "nothing
/// has fired for 500 ms" is answered without the engine owning a clock.
#[no_mangle]
pub unsafe extern "C" fn sc_core_fires(c: *const ScCore) -> u32 {
    c.as_ref().map(|c| c.0.fires()).unwrap_or(0)
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

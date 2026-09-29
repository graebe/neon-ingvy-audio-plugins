/*
 * spectro-capi -- the C ABI. This is the whole surface the iPlug2 shell sees,
 * and the surface a Schwung module on the Move would see.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * The shape is the Trance Gate engine's: an opaque handle, a couple of
 * functions, no callbacks and no C structs shared across the boundary. A C ABI
 * that passes only scalars and byte buffers is one that cannot get out of step
 * with its header.
 *
 * THE THREAD RULES ARE PART OF THE ABI, not a note in a README:
 *
 *   spectro_new / free / configure   one thread, with no other call in flight
 *   spectro_push_f32                 the audio thread, and only it
 *   spectro_take_columns             the message thread, and only it
 *
 * push and take_columns may run at the same time -- that is what the ring
 * inside is for -- but two threads pushing, or a configure racing either, is
 * undefined. The shell honours this by construction: OnReset configures while
 * the host guarantees audio is stopped, ProcessBlock pushes, OnIdle drains.
 *
 * PANICS ABORT (see the workspace Cargo.toml). Unwinding out of an `extern "C"`
 * function into C++ is undefined behaviour, and a crash the OS reports beats a
 * host whose stack has been quietly corrupted.
 */

use core::ffi::c_int;
use spectro_core::{pick_fft_size, pick_hop, Analyzer, Config};

/// Allocate an analyzer with the default configuration. Returns null only if
/// the allocator does, which on a desktop host means the process is already
/// finished.
#[no_mangle]
pub extern "C" fn spectro_new() -> *mut Analyzer {
    Box::into_raw(Box::new(Analyzer::new(Config::default())))
}

/// Free an analyzer. Null is a no-op, so a shell's destructor needs no branch.
///
/// # Safety
/// `p` must be a pointer from `spectro_new` and must not be used afterwards.
#[no_mangle]
pub unsafe extern "C" fn spectro_free(p: *mut Analyzer) {
    if !p.is_null() {
        drop(Box::from_raw(p));
    }
}

/// Re-configure and clear. Allocates, so this is a prepare-to-play call and
/// never an audio-thread one. Out-of-range arguments are clamped rather than
/// rejected -- there is no error channel here worth the complexity, and a
/// picture drawn at a sane fallback beats one that never appears.
///
/// # Safety
/// `p` must be a live analyzer, and no other call may be in flight.
#[no_mangle]
pub unsafe extern "C" fn spectro_configure(
    p: *mut Analyzer,
    sample_rate: f32,
    fft_size: c_int,
    hop: c_int,
    bands: c_int,
    f_min: f32,
    f_max: f32,
    db_floor: f32,
    db_ceil: f32,
) {
    if p.is_null() {
        return;
    }
    let cfg = Config {
        sample_rate,
        fft_size: fft_size.max(0) as usize,
        hop: hop.max(0) as usize,
        bands: bands.max(0) as usize,
        f_min,
        f_max,
        db_floor,
        db_ceil,
    };
    /* A fresh analyzer rather than a reset: every buffer's size depends on the
     * configuration, so "reconfigure" and "reallocate" are the same act. The
     * old one's ring goes with it, which is correct -- its columns were
     * measured against a different axis. */
    *p = Analyzer::new(cfg);
}

/// Feed `n` mono samples. Audio thread only. Allocates nothing.
///
/// # Safety
/// `mono` must point to `n` readable floats; `p` must be a live analyzer.
#[no_mangle]
pub unsafe extern "C" fn spectro_push_f32(p: *const Analyzer, mono: *const f32, n: c_int) {
    if p.is_null() || mono.is_null() || n <= 0 {
        return;
    }
    (*p).push(core::slice::from_raw_parts(mono, n as usize))
}

/// Drain finished columns into `out`, `spectro_bands()` bytes each, oldest
/// first. Returns the number of columns written. Message thread only.
///
/// # Safety
/// `out` must be writable for `max_cols * spectro_bands(p)` bytes.
#[no_mangle]
pub unsafe extern "C" fn spectro_take_columns(
    p: *const Analyzer,
    out: *mut u8,
    max_cols: c_int,
) -> c_int {
    if p.is_null() || out.is_null() || max_cols <= 0 {
        return 0;
    }
    let bands = (*p).bands();
    let slice = core::slice::from_raw_parts_mut(out, bands * max_cols as usize);
    (*p).take_columns(slice, max_cols as usize) as c_int
}

/// Bytes per column: the band count.
///
/// # Safety
/// `p` must be a live analyzer.
#[no_mangle]
pub unsafe extern "C" fn spectro_bands(p: *const Analyzer) -> c_int {
    if p.is_null() {
        return 0;
    }
    (*p).bands() as c_int
}

/// Band centre frequencies in Hz, ascending, up to `max`. Returns how many were
/// written. THE UI'S AXIS LABELS COME FROM HERE so the log mapping is never
/// written down a second time.
///
/// # Safety
/// `out` must be writable for `max` floats.
#[no_mangle]
pub unsafe extern "C" fn spectro_band_hz(p: *const Analyzer, out: *mut f32, max: c_int) -> c_int {
    if p.is_null() || out.is_null() || max <= 0 {
        return 0;
    }
    (*p).band_hz_into(core::slice::from_raw_parts_mut(out, max as usize)) as c_int
}

/// The window length that resolves 10 Hz at this sample rate, as a power of
/// two: 8192 at 44.1/48 kHz, 16384 at 88.2/96 kHz, capped there.
///
/// THE RULE LIVES IN THE ENGINE, not in the shell that calls it, because it is
/// arithmetic about sound rather than about a host -- an FFT's bins are
/// sample_rate / fft_size apart, and a picture that starts at 10 Hz needs them
/// finer than 10 Hz.
#[no_mangle]
pub extern "C" fn spectro_pick_fft_size(sample_rate: f32) -> c_int {
    pick_fft_size(sample_rate) as c_int
}

/// The hop that scrolls the picture at ~47 columns a second at this rate, so a
/// window holds the same thirteen seconds whatever the session runs at.
#[no_mangle]
pub extern "C" fn spectro_pick_hop(sample_rate: f32, fft_size: c_int) -> c_int {
    pick_hop(sample_rate, fft_size.max(0) as usize) as c_int
}

/// Change the frequency range the picture covers. **Message thread, and safe
/// while audio is running** -- unlike `spectro_configure`, which allocates.
///
/// The analyzer stores the request and the audio thread rebuilds its own band
/// table at its next frame; columns already queued from before the change are
/// dropped rather than handed out under the new scale. A range that cannot be
/// drawn (either bound not finite, f_min below 1 Hz, or less than half an octave
/// between them) is ignored.
///
/// # Safety
/// `p` must be a live analyzer.
#[no_mangle]
pub unsafe extern "C" fn spectro_set_range(p: *const Analyzer, f_min: f32, f_max: f32) {
    if p.is_null() {
        return;
    }
    (*p).set_range(f_min, f_max)
}

/// Columns thrown away because nothing drained the ring. A diagnostic: it
/// should be zero while an editor is open, and a jump in it means OnIdle
/// stopped running rather than that the analysis broke.
///
/// # Safety
/// `p` must be a live analyzer.
#[no_mangle]
pub unsafe extern "C" fn spectro_dropped(p: *const Analyzer) -> c_int {
    if p.is_null() {
        return 0;
    }
    (*p).dropped().min(c_int::MAX as usize) as c_int
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The analyzer's C ABI, `spectro_*`, and the source of spectro_core.h, which
build.rs generates from this file (cbindgen's configuration is
cbindgen/spectro_core.toml): every `pub const` here a `#define`, each
`#[no_mangle]` function a prototype, with these doc comments above them.

A short-time Fourier analyzer that produces SPECTROGRAM COLUMNS: one byte per
log-spaced frequency band, computed by spectro_push_f32 on the audio thread
and drained on the message thread through a lock-free ring. (The Spectrogram
plugin analyses through the receiver, recv.rs and spectro_recv.h, instead, off
the audio thread.)

THE THREAD RULES ARE PART OF THE ABI:

  spectro_new / free / configure   one thread, nothing else in flight
  spectro_push_f32                 the audio thread, and only it
  spectro_take_columns             the message thread, and only it

push and take_columns may overlap -- that is what the ring is for. Two
pushers, or a configure racing either, is undefined.
*/

use core::ffi::{c_int, c_uchar};
use core::ptr::addr_of_mut;
use spectro_core::{pick_fft_size, pick_hop, Analyzer, Config, Consumer, Producer};

/*
 * THE DEFAULTS, for a caller that wants to state them explicitly.
 *
 * THE WINDOW AND THE HOP ARE NOT CONSTANTS IN PRACTICE: both depend on the
 * sample rate, and spectro_pick_fft_size / spectro_pick_hop are what a
 * host-facing caller should use. The two below are what those functions return
 * at 48 kHz, and they are here so a test can state a configuration without a
 * rate. They are `Config::default()`'s, which the tests hold them to.
 */
/// The window at 48 kHz: 8192 is bins 5.9 Hz apart -- which is what
/// SPECTRO_F_MIN of 10 Hz requires, since an axis cannot start below the first
/// bin above DC. It is 171 ms of window, so time resolution is the price.
pub const SPECTRO_FFT_SIZE: c_int = 8192;
/// The hop at 48 kHz.
pub const SPECTRO_HOP: c_int = 1024;
/// Bands per column: bytes per column.
pub const SPECTRO_BANDS: c_int = 256;
/* THE FOUR FLOATS ARE CAST TO THEIR OWN TYPE, for C. The header has always
 * declared them float (10.0f), and cbindgen writes a float literal's digits
 * without its suffix -- a double, which changes the type of every expression
 * a C caller builds from one. A cast it writes as a cast: (float)10.0. */
/// The bottom of the frequency axis, in Hz.
#[allow(clippy::unnecessary_cast, reason = "the cast is the C type: cbindgen writes it as (float), and drops an f32 suffix")]
pub const SPECTRO_F_MIN: f32 = 10.0 as f32;
/// The top of the frequency axis, in Hz.
#[allow(clippy::unnecessary_cast, reason = "the cast is the C type: cbindgen writes it as (float), and drops an f32 suffix")]
pub const SPECTRO_F_MAX: f32 = 20000.0 as f32;
/// The level a column byte of 0 stands for, in dBFS: 16-bit silence.
#[allow(clippy::unnecessary_cast, reason = "the cast is the C type: cbindgen writes it as (float), and drops an f32 suffix")]
pub const SPECTRO_DB_FLOOR: f32 = -96.0 as f32;
/// The level a column byte of 255 stands for, in dBFS.
#[allow(clippy::unnecessary_cast, reason = "the cast is the C type: cbindgen writes it as (float), and drops an f32 suffix")]
pub const SPECTRO_DB_CEIL: f32 = 0.0 as f32;
/// Columns the ring holds before it starts dropping them: ~5 s at the
/// defaults. A drainer running at 60 Hz leaves at most one behind.
pub const SPECTRO_COLUMN_CAPACITY: c_int = 256;
const _: () = assert!(SPECTRO_COLUMN_CAPACITY as usize == spectro_core::COLUMN_CAPACITY);

/*
 * ONE C HANDLE, TWO RUST HALVES.
 *
 * The C side holds one pointer and calls push from the audio thread while the
 * message thread drains. Making a `&mut` (or even a `&`) to the whole handle on
 * either thread while the other is inside it would be aliasing undefined
 * behaviour, however disjoint the work. So every entry point projects from the
 * raw pointer to the ONE field its thread owns -- `tx` for the audio thread,
 * `rx` for the message thread -- and the handle as a whole is only named to
 * create, reconfigure or free it, when nothing else is in flight.
 */
pub struct Spectro {
    tx: Producer,
    rx: Consumer,
}

impl Spectro {
    fn new(cfg: Config) -> Self {
        let (tx, rx) = Analyzer::new(cfg).split();
        Spectro { tx, rx }
    }
}

/// The audio thread's half. Caller: the audio thread, with `p` live.
unsafe fn tx<'a>(p: *mut Spectro) -> &'a mut Producer {
    &mut *addr_of_mut!((*p).tx)
}

/// The message thread's half. Caller: the message thread, with `p` live.
unsafe fn rx<'a>(p: *mut Spectro) -> &'a mut Consumer {
    &mut *addr_of_mut!((*p).rx)
}

/// Allocate an analyzer with the default configuration. Returns null only if
/// the allocator does, which on a desktop host means the process is already
/// finished.
#[no_mangle]
pub extern "C" fn spectro_new() -> *mut Spectro {
    Box::into_raw(Box::new(Spectro::new(Config::default())))
}

/// Free an analyzer. Null is a no-op, so a shell's destructor needs no branch.
///
/// # Safety
/// `p` must be a pointer from `spectro_new` and must not be used afterwards.
#[no_mangle]
pub unsafe extern "C" fn spectro_free(p: *mut Spectro) {
    if !p.is_null() {
        drop(Box::from_raw(p));
    }
}

/// Re-configure and clear. Allocates, so this is a prepare-to-play call and
/// never an audio-thread one. Out-of-range arguments are clamped rather than
/// rejected -- fft_size is rounded down to a power of two, f_max down to
/// Nyquist, and hop into fft_size/32 .. fft_size. There is no error channel
/// here worth the complexity, and a picture drawn at a sane fallback beats one
/// that never appears.
///
/// # Safety
/// `p` must be a live analyzer, and no other call may be in flight.
#[no_mangle]
pub unsafe extern "C" fn spectro_configure(
    p: *mut Spectro,
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
    *p = Spectro::new(cfg);
}

/// Feed `n` mono samples. Audio thread only. Allocates nothing.
///
/// # Safety
/// `mono` must point to `n` readable floats; `p` must be a live analyzer.
#[no_mangle]
pub unsafe extern "C" fn spectro_push_f32(p: *mut Spectro, mono: *const f32, n: c_int) {
    if p.is_null() || mono.is_null() || n <= 0 {
        return;
    }
    tx(p).push(core::slice::from_raw_parts(mono, n as usize))
}

/// Drain finished columns into `out`, `spectro_bands()` bytes each, oldest
/// first, band 0 the lowest frequency. `out` must hold `max_cols *
/// spectro_bands()` bytes. Returns the columns written, which may be 0.
/// Message thread only.
///
/// # Safety
/// `out` must be writable for `max_cols * spectro_bands(p)` bytes.
#[no_mangle]
pub unsafe extern "C" fn spectro_take_columns(
    p: *mut Spectro,
    out: *mut c_uchar,
    max_cols: c_int,
) -> c_int {
    if p.is_null() || out.is_null() || max_cols <= 0 {
        return 0;
    }
    let rx = rx(p);
    let slice = core::slice::from_raw_parts_mut(out, rx.bands() * max_cols as usize);
    rx.take_columns(slice, max_cols as usize) as c_int
}

/// Bytes per column: the band count.
///
/// # Safety
/// `p` must be a live analyzer.
#[no_mangle]
pub unsafe extern "C" fn spectro_bands(p: *const Spectro) -> c_int {
    if p.is_null() {
        return 0;
    }
    rx(p as *mut Spectro).bands() as c_int
}

/// Band centre frequencies in Hz, ascending, up to `max`. Returns how many were
/// written. THE UI'S AXIS LABELS COME FROM HERE so the log mapping is never
/// written down a second time.
///
/// # Safety
/// `out` must be writable for `max` floats.
#[no_mangle]
pub unsafe extern "C" fn spectro_band_hz(p: *const Spectro, out: *mut f32, max: c_int) -> c_int {
    if p.is_null() || out.is_null() || max <= 0 {
        return 0;
    }
    rx(p as *mut Spectro).band_hz_into(core::slice::from_raw_parts_mut(out, max as usize)) as c_int
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
/// window holds the same thirteen seconds whatever the session runs at. Never
/// longer than `fft_size`.
#[no_mangle]
pub extern "C" fn spectro_pick_hop(sample_rate: f32, fft_size: c_int) -> c_int {
    pick_hop(sample_rate, fft_size.max(0) as usize) as c_int
}

/// Change the frequency range the picture covers. **Message thread, and safe
/// while audio is running** -- unlike `spectro_configure`, which allocates.
///
/// The analyzer stores the request and the audio thread rebuilds its own band
/// table at its next frame; columns already queued from before the change are
/// dropped rather than handed out under the new scale -- they are answers to a
/// different question. `spectro_band_hz` reflects the new range IMMEDIATELY,
/// before the audio thread has run: the axis is derived from the request, not
/// read from the table. A range that cannot be
/// drawn (either bound not finite, f_min below 1 Hz, or less than half an octave
/// between them) is ignored.
///
/// # Safety
/// `p` must be a live analyzer.
#[no_mangle]
pub unsafe extern "C" fn spectro_set_range(p: *mut Spectro, f_min: f32, f_max: f32) {
    if p.is_null() {
        return;
    }
    rx(p).set_range(f_min, f_max)
}

/// Columns thrown away because nothing drained the ring. A diagnostic: it
/// should be zero while an editor is open, and a jump in it means OnIdle
/// stopped running rather than that the analysis broke.
///
/// # Safety
/// `p` must be a live analyzer.
#[no_mangle]
pub unsafe extern "C" fn spectro_dropped(p: *const Spectro) -> c_int {
    if p.is_null() {
        return 0;
    }
    rx(p as *mut Spectro).dropped().min(c_int::MAX as usize) as c_int
}

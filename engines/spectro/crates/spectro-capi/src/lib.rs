// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * spectro-capi -- the C ABI. This is the whole surface the iPlug2 shell sees,
 * and the surface a Schwung module on the Move would see.
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
/* shell_handoff_*, for the same reason: see engines/shell/include/shell_handoff.h. */
use shell_capi as _;

use core::ffi::c_int;
use core::ptr::addr_of_mut;
use spectro_core::{pick_fft_size, pick_hop, Analyzer, Config, Consumer, Producer};

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
/// rejected -- there is no error channel here worth the complexity, and a
/// picture drawn at a sane fallback beats one that never appears.
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
/// first. Returns the number of columns written. Message thread only.
///
/// # Safety
/// `out` must be writable for `max_cols * spectro_bands(p)` bytes.
#[no_mangle]
pub unsafe extern "C" fn spectro_take_columns(
    p: *mut Spectro,
    out: *mut u8,
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

/* ---------------------------------------------------------------- receiver --
 *
 * THE LISTEN-IN HALF. Everything above analyses the track the plugin sits on;
 * this lets one plugin also read a Listen-In bus, so two sources can be looked
 * at in one picture and the places they fight can be marked.
 *
 * The thread rules are NOT the same as the analyzer's above, and the difference
 * is the whole design:
 *
 *   srecv_new / free / start / set_sources / set_clash   the main thread
 *   srecv_push_own                                       the audio thread, and only it
 *   srecv_pump / take_columns / clash / slots            the message thread, and only it
 *
 * The message side is SERIALISED rather than trusted: its half of the handle
 * sits behind a lock, so a second thread calling in -- a host restoring state
 * on a thread of its own, say -- waits its turn instead of racing the first.
 * That is what makes srecv_set_sources, which waits for the worker, safe
 * against itself. The audio thread's half takes no lock.
 *
 * The transforms run on the receiver's own worker once `srecv_start` has
 * started it -- never on the audio thread, which cannot drain a bus reader and
 * must not overrun, and no longer on the host's UI thread either. The own
 * channel reaches the worker through a ring, which is what lets every source be
 * fed the same number of frames and therefore be compared cell by cell. Without
 * a worker, `srecv_pump` runs the same pump on the caller's thread. See the
 * header of spectro-recv.
 */
use spectro_recv::{OwnFeed, Receiver, MAX_SOURCES};
use std::sync::{Mutex, MutexGuard, PoisonError};

/*
 * THE SAME SPLIT AS `Spectro`: the audio thread's `feed` and the message
 * side's `rx`, reached only by projecting from the raw pointer, so
 * `srecv_push_own` and a message-side call never alias -- they are different
 * fields and neither names the handle as a whole. The worker, when started,
 * owns none of this handle: it holds its own engine inside `rx` and is joined
 * when `rx` is dropped.
 *
 * `rx` IS BEHIND A LOCK and `feed` is not. The message side is documented as
 * one thread, but a host decides which thread calls a plugin's state methods,
 * and `Receiver` hands the worker one plan at a time on the strength of `&mut`.
 * Two threads each making that `&mut` from a raw pointer is exactly how a
 * caller came to wait forever for a plan the other had taken. The lock is
 * uncontended in correct use, and the audio thread never touches it.
 */
pub struct Srecv {
    feed: OwnFeed,
    rx: Mutex<Receiver>,
}

/// The message side's half, held for as long as the guard lives. Any thread
/// but the audio thread, with `p` live. Not re-entrant: take it once a call.
unsafe fn recv<'a>(p: *const Srecv) -> MutexGuard<'a, Receiver> {
    /* A panic aborts (see the top of this file), so a poisoned lock cannot be
     * observed; the guard is taken either way rather than unwrapped. */
    (*p).rx.lock().unwrap_or_else(PoisonError::into_inner)
}

/// Sources one receiver will draw, the own channel included.
#[no_mangle]
pub extern "C" fn srecv_max_sources() -> c_int {
    MAX_SOURCES as c_int
}

/// Allocate a receiver. Its own channel is configured exactly as
/// `spectro_configure` would, and every bus source it later opens inherits it.
#[no_mangle]
pub extern "C" fn srecv_new(
    sample_rate: f32,
    fft_size: c_int,
    hop: c_int,
    bands: c_int,
    f_min: f32,
    f_max: f32,
    db_floor: f32,
    db_ceil: f32,
) -> *mut Srecv {
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
    let (rx, feed) = Receiver::new(cfg);
    Box::into_raw(Box::new(Srecv { feed, rx: Mutex::new(rx) }))
}

/// Start the receiver's analysis thread. Returns 1 if it is running (also
/// when it already was), 0 if it could not be created -- then `srecv_pump`
/// goes on doing the work on the caller's thread. **Main thread.**
///
/// # Safety
/// `p` must be a live receiver.
#[no_mangle]
pub unsafe extern "C" fn srecv_start(p: *mut Srecv) -> c_int {
    if p.is_null() {
        return 0;
    }
    i32::from(recv(p).start())
}

/// Stops and joins the analysis thread, if one was started, before freeing.
///
/// # Safety
/// `p` must be a pointer from `srecv_new` and must not be used afterwards.
#[no_mangle]
pub unsafe extern "C" fn srecv_free(p: *mut Srecv) {
    if !p.is_null() {
        drop(Box::from_raw(p));
    }
}

/// Channels currently drawable: the own channel is 0, each open bus follows in
/// the order it was asked for.
///
/// # Safety
/// `p` must be a live receiver.
#[no_mangle]
pub unsafe extern "C" fn srecv_channels(p: *const Srecv) -> c_int {
    if p.is_null() {
        return 0;
    }
    recv(p).channels() as c_int
}

/// The bus slot behind a channel, or 0 for the own channel.
///
/// # Safety
/// `p` must be a live receiver.
#[no_mangle]
pub unsafe extern "C" fn srecv_slot_of(p: *const Srecv, ch: c_int) -> c_int {
    if p.is_null() || ch < 0 {
        return 0;
    }
    recv(p).slot_of(ch as usize).unwrap_or(0) as c_int
}

/// Non-zero when a channel's sender runs at another sample rate. Such a source
/// is NOT drawn: a different rate picks a different window and therefore a
/// different group delay, so the two pictures would be quietly offset.
///
/// # Safety
/// `p` must be a live receiver.
#[no_mangle]
pub unsafe extern "C" fn srecv_rate_mismatch(p: *const Srecv, ch: c_int) -> c_int {
    if p.is_null() || ch < 0 {
        return 0;
    }
    i32::from(recv(p).rate_mismatch(ch as usize))
}

/// Choose which buses to listen to: `slots[0..n]`, 1-based, in the order they
/// should appear. **Main thread**, and it allocates.
///
/// # Safety
/// `p` must be a live receiver; `slots` must point to `n` readable u32s.
#[no_mangle]
pub unsafe extern "C" fn srecv_set_sources(p: *mut Srecv, slots: *const u32, n: c_int) {
    if p.is_null() {
        return;
    }
    let wanted: &[u32] = if slots.is_null() || n <= 0 {
        &[]
    } else {
        core::slice::from_raw_parts(slots, n as usize)
    };
    recv(p).set_sources(wanted)
}

/// Feed `n` mono samples from the track this plugin sits on. **Audio thread
/// only.** Copies into a ring and returns; allocates nothing, locks nothing.
///
/// # Safety
/// `mono` must point to `n` readable floats; `p` must be a live receiver.
#[no_mangle]
pub unsafe extern "C" fn srecv_push_own(p: *mut Srecv, mono: *const f32, n: c_int) {
    if p.is_null() || mono.is_null() || n <= 0 {
        return;
    }
    let feed = &mut *addr_of_mut!((*p).feed);
    feed.push(core::slice::from_raw_parts(mono, n as usize))
}

/// Move audio into every analyzer, in step, on the calling thread. Returns the
/// frames each source was given -- always 0 once `srecv_start` has handed the
/// work to the receiver's thread. **Message thread only.**
///
/// # Safety
/// `p` must be a live receiver.
#[no_mangle]
pub unsafe extern "C" fn srecv_pump(p: *mut Srecv) -> c_int {
    if p.is_null() {
        return 0;
    }
    recv(p).pump() as c_int
}

/// Columns every drawn channel has ready, as of the last finished pump.
/// Taking this many from each keeps them the same moments while the analysis
/// thread is adding more.
/// **Message thread only.**
///
/// # Safety
/// `p` must be a live receiver.
#[no_mangle]
pub unsafe extern "C" fn srecv_ready(p: *const Srecv) -> c_int {
    if p.is_null() {
        return 0;
    }
    recv(p).ready().min(c_int::MAX as usize) as c_int
}

/// Drain one channel's finished columns, `spectro_bands()` bytes each, oldest
/// first. **Message thread only.**
///
/// # Safety
/// `out` must be writable for `max_cols * bands` bytes.
#[no_mangle]
pub unsafe extern "C" fn srecv_take_columns(
    p: *mut Srecv,
    ch: c_int,
    out: *mut u8,
    max_cols: c_int,
) -> c_int {
    if p.is_null() || out.is_null() || max_cols <= 0 || ch < 0 {
        return 0;
    }
    let mut r = recv(p);
    let slice = core::slice::from_raw_parts_mut(out, r.bands() * max_cols as usize);
    r.take_columns(ch as usize, slice, max_cols as usize) as c_int
}

/// One editor tick's picture: every channel drained in step, the `view`
/// channels summed in power into `sum_out`, and -- when `cmp_a` and `cmp_b` are
/// both >= 0 -- their clash into `clash_out`. Returns the columns in
/// `sum_out` (0: nothing to send) and stores the clash's in `*clash_cols`.
/// A null `sum_out` drains and drops. **Message thread only.**
///
/// # Safety
/// `view` holds `n_view` ints; `sum_out` and `clash_out` are null or writable
/// for `max_cols * bands` bytes; `clash_cols` is null or writable.
#[no_mangle]
pub unsafe extern "C" fn srecv_frame(
    p: *mut Srecv,
    view: *const c_int,
    n_view: c_int,
    cmp_a: c_int,
    cmp_b: c_int,
    sum_out: *mut u8,
    clash_out: *mut u8,
    max_cols: c_int,
    clash_cols: *mut c_int,
) -> c_int {
    if !clash_cols.is_null() {
        *clash_cols = 0;
    }
    if p.is_null() || max_cols <= 0 {
        return 0;
    }
    let mut r = recv(p);
    let n = r.bands() * max_cols as usize;
    let wanted: &[c_int] = if view.is_null() || n_view <= 0 {
        &[]
    } else {
        core::slice::from_raw_parts(view, n_view as usize)
    };
    /* On the stack: a view names at most a handful of channels. */
    let mut chans = [0usize; 16];
    let mut k = 0;
    for &ch in wanted {
        if ch >= 0 && k < chans.len() {
            chans[k] = ch as usize;
            k += 1;
        }
    }
    let compare = (cmp_a >= 0 && cmp_b >= 0).then_some((cmp_a as usize, cmp_b as usize));
    let sum = (!sum_out.is_null()).then(|| core::slice::from_raw_parts_mut(sum_out, n));
    let clash = (!clash_out.is_null()).then(|| core::slice::from_raw_parts_mut(clash_out, n));
    let (cols, clashed) = r.frame(&chans[..k], compare, sum, clash, max_cols as usize);
    if !clash_cols.is_null() {
        *clash_cols = clashed as c_int;
    }
    cols as c_int
}

/// The range every source is measured over. Allocation-free and safe while
/// audio runs, exactly as `spectro_set_range` is.
///
/// # Safety
/// `p` must be a live receiver.
#[no_mangle]
pub unsafe extern "C" fn srecv_set_range(p: *const Srecv, f_min: f32, f_max: f32) {
    if p.is_null() {
        return;
    }
    recv(p).set_range(f_min, f_max)
}

/// What counts as a clash: a floor in dBFS that BOTH sources must clear, and a
/// balance window in dB past which the louder one is simply winning.
///
/// # Safety
/// `p` must be a live receiver.
#[no_mangle]
pub unsafe extern "C" fn srecv_set_clash(p: *mut Srecv, floor_db: f32, balance_db: f32) {
    if p.is_null() {
        return;
    }
    recv(p).set_clash(floor_db, balance_db)
}

/// Clash strength for `n_cols` columns of `a` against `b`, into `out`.
///
/// Takes the columns the caller ALREADY drained rather than draining again:
/// `srecv_take_columns` is destructive, so a second drain would compare one
/// source's present against another's future.
///
/// # Safety
/// All three must be readable/writable for `n_cols * bands` bytes.
#[no_mangle]
pub unsafe extern "C" fn srecv_clash(
    p: *const Srecv,
    a: *const u8,
    b: *const u8,
    out: *mut u8,
    n_cols: c_int,
) {
    if p.is_null() || a.is_null() || b.is_null() || out.is_null() || n_cols <= 0 {
        return;
    }
    let r = recv(p);
    let n = r.bands() * n_cols as usize;
    r.clash_into(
        core::slice::from_raw_parts(a, n),
        core::slice::from_raw_parts(b, n),
        core::slice::from_raw_parts_mut(out, n),
    )
}

/// Frames a channel lost before the receiver reached them. A diagnostic: a jump
/// means whatever pumps stopped keeping up, not that the analysis broke.
///
/// # Safety
/// `p` must be a live receiver.
#[no_mangle]
pub unsafe extern "C" fn srecv_dropped(p: *const Srecv, ch: c_int) -> c_int {
    if p.is_null() || ch < 0 {
        return 0;
    }
    recv(p).bus_dropped(ch as usize).min(c_int::MAX as u64) as c_int
}

/// The source list: every slot that exists, as
/// `"<slot>:<live>:<rate>:<label>"` per line. Returns the bytes written, or the
/// bytes it WOULD have written when `cap` is too small -- so a caller can size a
/// buffer by asking twice. **Main thread.**
///
/// Probing creates nothing: walking all sixteen slots leaves the machine
/// exactly as it found it.
///
/// # Safety
/// `out` must be writable for `cap` bytes.
#[no_mangle]
pub unsafe extern "C" fn srecv_slots(out: *mut u8, cap: c_int) -> c_int {
    let text = spectro_recv::encode_slots(&Receiver::slots());
    let bytes = text.as_bytes();
    if !out.is_null() && cap > 0 {
        let n = bytes.len().min(cap as usize - 1);
        core::ptr::copy_nonoverlapping(bytes.as_ptr(), out, n);
        *out.add(n) = 0;
    }
    bytes.len() as c_int
}

/// Bytes in one column of any channel: the band count they all share.
///
/// # Safety
/// `p` must be a live receiver.
#[no_mangle]
pub unsafe extern "C" fn srecv_bands(p: *const Srecv) -> c_int {
    if p.is_null() {
        return 0;
    }
    recv(p).bands() as c_int
}

/// The band centre frequencies, ascending, into `out`. Returns how many.
///
/// One axis for every source, because they share a configuration -- which is
/// what lets their columns be compared at all.
///
/// # Safety
/// `out` must be writable for `n` floats.
#[no_mangle]
pub unsafe extern "C" fn srecv_band_hz(p: *const Srecv, out: *mut f32, n: c_int) -> c_int {
    if p.is_null() || out.is_null() || n <= 0 {
        return 0;
    }
    recv(p).band_hz_into(core::slice::from_raw_parts_mut(out, n as usize)) as c_int
}

/// Add several channels' columns into one, in POWER, into `out`.
///
/// `srcs` is `n_src` pointers, each to `n_cols * bands` bytes; `out` likewise.
///
/// IT CANNOT BE DONE IN BYTE SPACE and that is the whole reason this exists: a
/// byte is linear in dB, so adding two bytes adds two decibels, which multiplies
/// two amplitudes. Two sources at -20 dB would come out at -40, quieter than
/// either. This inverts to power, adds, and re-encodes -- +3 dB for two equal
/// uncorrelated sources, which is what a bass and a pad actually measure.
///
/// Like `srecv_clash`, it takes columns the caller ALREADY drained: taking them
/// again would add one source's present to another's future.
///
/// # Safety
/// Every pointer must be readable/writable for `n_cols * bands` bytes.
#[no_mangle]
pub unsafe extern "C" fn srecv_sum(
    p: *const Srecv,
    srcs: *const *const u8,
    n_src: c_int,
    out: *mut u8,
    n_cols: c_int,
) {
    if p.is_null() || out.is_null() || n_cols <= 0 {
        return;
    }
    let r = recv(p);
    let n = r.bands() * n_cols as usize;
    let dst = core::slice::from_raw_parts_mut(out, n);

    if srcs.is_null() || n_src <= 0 {
        dst.fill(0);
        return;
    }
    let ptrs = core::slice::from_raw_parts(srcs, n_src as usize);

    /* Borrowed on the stack: MAX_SOURCES is the cap the receiver enforces, so
     * nothing is allocated to call this. */
    let mut view: [&[u8]; MAX_SOURCES] = [&[]; MAX_SOURCES];
    let mut k = 0;
    for &ptr in ptrs.iter().take(MAX_SOURCES) {
        if ptr.is_null() {
            continue;
        }
        view[k] = core::slice::from_raw_parts(ptr, n);
        k += 1;
    }
    r.sum_into(&view[..k], dst)
}

/// Non-zero when a channel is being zero-filled because its sender has gone
/// quiet -- a muted Listen-In, or one whose host stopped calling it.
///
/// Worth saying out loud rather than drawing: a black stripe reads as "that
/// track is silent" when what happened is that the bus is absent.
///
/// # Safety
/// `p` must be a live receiver.
#[no_mangle]
pub unsafe extern "C" fn srecv_starved(p: *const Srecv, ch: c_int) -> c_int {
    if p.is_null() || ch < 0 {
        return 0;
    }
    i32::from(recv(p).starved(ch as usize))
}

#[cfg(test)]
mod tests;

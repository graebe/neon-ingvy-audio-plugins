// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The listen-in receiver's C ABI, `srecv_*`, and the source of spectro_recv.h,
which build.rs generates from this file (cbindgen's configuration is
cbindgen/spectro_recv.toml).

The analyzer (analyzer.rs, spectro_core.h) analyses ONE source: the track the
plugin sits on. This is the other half -- a receiver that also reads Listen-In
buses, so a bass and a pad can be looked at in one picture, and the places
they are fighting can be marked. It ships in the same static library,
libspectro_capi.a, so linking it costs nothing extra.

THE THREAD RULES ARE PART OF THE ABI, and they are NOT the analyzer's:

  srecv_new / free / start             one thread, nothing else in flight
  srecv_set_sources / set_clash        the main thread -- both allocate
  srecv_slots                          the main thread
  srecv_push_own                       the audio thread, and only it
  srecv_pump                           the message thread, and only it
  srecv_take_columns / clash / frame   the message thread, and only it

THE TRANSFORMS RUN ON THE RECEIVER'S OWN THREAD once srecv_start has started
it: a worker, below the UI's priority, that wakes every few milliseconds,
drains the plugin's own audio and every bus, and queues finished columns for
srecv_take_columns. srecv_free stops and joins it. Without it, srecv_pump does
the same work on the caller's thread.

A bus reader cannot be drained from the audio thread -- opening one allocates
and maps memory, and `abus_reader_read` is "one thread, the same one each
time". So every analyzer is fed in one place and the plugin's OWN audio
reaches it through a ring: the audio callback does nothing but copy its mono
sum into it. Every source is given the same number of frames, so column k of
each is the same moment -- which is what makes a per-cell clash mean anything
rather than being a coincidence.

THE MESSAGE SIDE IS SERIALISED, NOT MERELY TRUSTED. Its half sits behind a
lock, so a second thread calling in while the first is inside waits its turn:
nothing is corrupted and nothing deadlocks. That is a floor, not the design.
srecv_set_sources can hold the lock for a worker tick and a pump, and every
message-thread call behind it waits too -- so a plugin still calls these from
its main thread, and a host thread that wants a change (a state load) records
it for the main thread to apply. `srecv_push_own` allocates nothing, takes no
lock and makes no system call; `srecv_set_sources` does all three, which is
why it is not allowed anywhere near the audio thread.
*/

use core::ffi::{c_int, c_uchar, c_uint};
use core::ptr::addr_of_mut;
use spectro_core::Config;
/*
 * THE LISTEN-IN HALF. The analyzer analyses the track the plugin sits on;
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

/// Channel 0 is always the track the plugin is inserted on.
pub const SRECV_OWN: c_int = 0;
/// Sources one receiver draws at once, the own channel included -- the
/// number `srecv_max_sources` answers, for an array bound.
///
/// Each one past the first is a whole analysis chain -- up to a 16384-point
/// transform about 47 times a second. Four is also about where a picture
/// stops being readable, so the cost and the legibility run out together.
pub const SRECV_MAX_SOURCES: c_int = 4;
const _: () = assert!(SRECV_OWN as usize == spectro_recv::OWN);
const _: () = assert!(SRECV_MAX_SOURCES as usize == MAX_SOURCES);

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
/// should appear. Anything past srecv_max_sources()-1, any duplicate, and any
/// slot outside 1..abus_max_slot() is dropped from the request.
///
/// Slots ALREADY OPEN ARE KEPT rather than reopened: a reopened reader starts
/// at the live edge, which would put a seam in a picture that had no reason
/// for one.
///
/// Once `srecv_start` has started the analysis thread this WAITS for it to
/// adopt the change -- at most a tick and one pump. A thread that is not
/// running is never waited for. Concurrent callers are served one after
/// another, each waiting for its own change: safe, but it blocks every other
/// message-side call meanwhile, which is why it belongs to the main thread.
///
/// **Main thread.** Allocates, takes the message side's lock, and waits.
///
/// # Safety
/// `p` must be a live receiver; `slots` must point to `n` readable u32s.
#[no_mangle]
pub unsafe extern "C" fn srecv_set_sources(p: *mut Srecv, slots: *const c_uint, n: c_int) {
    if p.is_null() {
        return;
    }
    let wanted: &[c_uint] = if slots.is_null() || n <= 0 {
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
/// frames each source was given, commonly 0 -- and always 0 once
/// `srecv_start` has handed the work to the receiver's thread. **Message
/// thread only.**
///
/// A source whose sample rate differs from the receiver's is read and
/// discarded, never analysed; the verdict is taken again whenever its sender
/// restarts. A bus that has delivered nothing for about a second is re-opened
/// if its sender quit and came back under the same slot.
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

/// Columns every drawn channel has ready, as of the last finished pump;
/// take this many from each and column k is the same moment in all of them.
/// Columns become visible a whole pump at a time -- the analysis thread feeds
/// one analyzer after another, and a drain that saw one channel's new column
/// before the next channel had it would pair them one column apart for good.
/// Left out: a channel refused for its sample rate, and a bus that has not
/// drawn its first column yet. **Message thread only.**
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

/// Drain one channel's finished columns -- as of the last finished pump --
/// `spectro_bands()` bytes each, oldest first. `out` must hold `max_cols *
/// bands` bytes; pass `srecv_ready()` as `max_cols` to keep the channels in
/// step. **Message thread only.**
///
/// # Safety
/// `out` must be writable for `max_cols * bands` bytes.
#[no_mangle]
pub unsafe extern "C" fn srecv_take_columns(
    p: *mut Srecv,
    ch: c_int,
    out: *mut c_uchar,
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
/// A channel that drew nothing this tick is left out of the sum rather than
/// adding silence. A null `sum_out` drains and drops -- a closed editor keeps
/// the rings from filling with a picture nobody will see. **Message thread
/// only**; allocates on its first call.
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
    sum_out: *mut c_uchar,
    clash_out: *mut c_uchar,
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
/// balance window in dB past which the louder one is simply winning rather
/// than competing.
///
/// The floor alone is not enough. A product of two spectra is a SUM in dB, so
/// 0 dB against -60 scores what -30 against -30 scores -- and only the second
/// is a clash. `min` answers "both present"; the window answers "and neither
/// wins".
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
    a: *const c_uchar,
    b: *const c_uchar,
    out: *mut c_uchar,
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
pub unsafe extern "C" fn srecv_slots(out: *mut c_uchar, cap: c_int) -> c_int {
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
    srcs: *const *const c_uchar,
    n_src: c_int,
    out: *mut c_uchar,
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
/// A bus that publishes nothing used to hold every OTHER source still,
/// because the pump waited for the slowest. Now the own track sets the pace
/// and a silent bus is filled with silence -- which is true, and keeps column
/// k the same moment for every source. This is how the editor knows to SAY so
/// rather than letting a black stripe read as "that track is silent".
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

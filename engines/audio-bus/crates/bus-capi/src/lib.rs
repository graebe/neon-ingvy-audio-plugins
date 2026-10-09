// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The C ABI: what a sending plugin and a receiving plugin both link -- and the
source of audio_bus.h, which build.rs generates from this file (cbindgen's
configuration is cbindgen/audio_bus.toml), these doc comments included.

Scalars and float buffers cross the boundary. No callbacks, no shared structs,
no ownership passing either way except the opaque handles.

A SHARED-MEMORY AUDIO BUS between plugins in one host. A sender claims one of
sixteen numbered slots and publishes stereo float audio into it; any number
of receivers, in this process or another, open the same slot and read it.

Nobody blocks anybody. The sender never waits. A receiver that falls behind
is TOLD how much it missed rather than handed a buffer spliced together from
two different moments -- which would look exactly like audio and be exactly
wrong. See `dropped` on abus_reader_read.

THE THREAD RULES ARE PART OF THE ABI:

  abus_writer_claim / release            the main thread
  abus_writer_set_label / sample_rate    the main thread
  abus_pusher_release                    the main thread
  abus_pusher_push / push_at             the audio thread, and only it
  abus_reader_open / close / reattach    the main thread
  abus_reader_read / position /          one thread, the same one each time
    timeline_of
  abus_probe / abus_incarnation          the main thread
  abus_reader_incarnation                the main thread, before the reader
                                         is lent to another

push and read may overlap across any number of processes -- that is the whole
point. TWO SENDERS ON ONE SLOT IS THE ONE THING THAT CANNOT HAPPEN, and it is
prevented rather than left undefined: the second abus_writer_claim returns
ABUS_ERR_TAKEN and the caller publishes nothing.

`abus_pusher_push` allocates nothing, takes no lock and makes no system call.
`abus_writer_claim` does all three, which is why it is not allowed anywhere
near the audio thread.
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
/* shell_handoff_*, for the same reason: see shell-capi. */
use shell_capi as _;

use bus_core::{ClaimError, Pusher, Reader, Writer};
use std::ffi::{c_char, c_int};

/*
 * TWO C HANDLES, ONE PER THREAD -- the Rust split, carried across the ABI.
 *
 * A claim is a `Writer` for the main thread (label, rate, release) and a
 * `Pusher` for the audio thread (push), and the C side gets them as two boxes.
 * A plugin keeps the writer and LENDS the pusher to its audio thread through
 * shell_handoff.h, which frees it only once no block is holding it; nothing
 * the main thread does to its own half can race a block, because the halves
 * share nothing but the claim's atomics. The slot stays claimed until BOTH are
 * released -- the claim is dropped with its last half.
 */
/// The main thread's half of a claim.
pub struct AbusWriter(Writer);
/// The audio thread's half of a claim.
pub struct AbusPusher(Pusher);
/// One receiver's place in one slot's stream.
pub struct AbusReader(Reader);

impl AbusReader {
    /// The reader inside, for a Rust crate that links this one (sc-capi's
    /// kick tap) and reads it on its own thread, by the same rules.
    pub fn reader_mut(&mut self) -> &mut Reader {
        &mut self.0
    }
}

/// What `abus_writer_claim` and `abus_reader_open` answer when they worked.
pub const ABUS_OK: c_int = 0;
/// The slot is outside 1..abus_max_slot().
pub const ABUS_ERR_BAD_SLOT: c_int = -1;
/// The segment could not be made or mapped.
pub const ABUS_ERR_UNAVAILABLE: c_int = -2;
/// Another LIVE sender holds this slot.
pub const ABUS_ERR_TAKEN: c_int = -3;

/// Bytes in the buffer `abus_probe` fills, NUL included.
pub const ABUS_LABEL_CAP: c_int = 32;
/// The highest slot, for code that needs it at compile time -- an array
/// bound, or a translation unit that links nothing. `abus_max_slot` answers
/// the same; abus_roundtrip.c checks the two agree.
pub const ABUS_MAX_SLOT: c_int = 16;
const _: () = assert!(ABUS_LABEL_CAP as usize == bus_core::LABEL_BYTES);
const _: () = assert!(ABUS_MAX_SLOT as u32 == bus_core::MAX_SLOT);

fn code(e: ClaimError) -> c_int {
    match e {
        ClaimError::BadSlot => ABUS_ERR_BAD_SLOT,
        ClaimError::Unavailable => ABUS_ERR_UNAVAILABLE,
        ClaimError::Taken => ABUS_ERR_TAKEN,
    }
}

/// The highest valid slot number. Slots are 1-based: slot 0 is not a bus, it
/// is a mistake, and it is reported as one.
#[no_mangle]
pub extern "C" fn abus_max_slot() -> u32 {
    bus_core::MAX_SLOT
}

/// Always 2, always interleaved. A mono source is duplicated by the SENDER, so
/// a receiver never has to ask how many channels arrived.
#[no_mangle]
pub extern "C" fn abus_channels() -> u32 {
    bus_core::CHANNELS
}

/// Claim a slot. On success `*writer` and `*pusher` receive the two halves,
/// each to be released later; on failure both are untouched. The return value
/// is ABUS_OK or an ABUS_ERR_*.
///
/// # Safety
/// `writer` and `pusher` must be valid, writable pointers.
#[no_mangle]
pub unsafe extern "C" fn abus_writer_claim(
    slot: u32,
    sample_rate: u32,
    writer: *mut *mut AbusWriter,
    pusher: *mut *mut AbusPusher,
) -> c_int {
    if writer.is_null() || pusher.is_null() {
        return ABUS_ERR_UNAVAILABLE;
    }
    match Writer::claim(slot, sample_rate) {
        Ok((w, p)) => {
            *writer = Box::into_raw(Box::new(AbusWriter(w)));
            *pusher = Box::into_raw(Box::new(AbusPusher(p)));
            ABUS_OK
        }
        Err(e) => code(e),
    }
}

/// # Safety
/// `w` must come from `abus_writer_claim` and must not be used afterwards.
#[no_mangle]
pub unsafe extern "C" fn abus_writer_release(w: *mut AbusWriter) {
    if !w.is_null() {
        drop(Box::from_raw(w));
    }
}

/// # Safety
/// `p` must come from `abus_writer_claim` and must not be used afterwards.
/// Not on the audio thread: the last half released unmaps the segment.
#[no_mangle]
pub unsafe extern "C" fn abus_pusher_release(p: *mut AbusPusher) {
    if !p.is_null() {
        drop(Box::from_raw(p));
    }
}

/// Publish `frames` of interleaved stereo. THE AUDIO THREAD CALLS THIS; it
/// allocates nothing and makes no system call.
///
/// # Safety
/// `p` is null or from `abus_writer_claim`, used by one thread at a time;
/// `interleaved` must point at `frames * abus_channels()` readable floats.
#[no_mangle]
pub unsafe extern "C" fn abus_pusher_push(p: *mut AbusPusher, interleaved: *const f32, frames: u32) {
    /* A NULL handle is the normal state of a Listen-In whose slot was taken by
     * another instance, and ProcessBlock calls this unconditionally. Silence is
     * the correct response, not a branch at every call site. */
    let Some(p) = p.as_mut() else { return };
    if interleaved.is_null() || frames == 0 {
        return;
    }
    let n = frames as usize * bus_core::CHANNELS as usize;
    p.0.push(core::slice::from_raw_parts(interleaved, n));
}

/// `abus_pusher_push`, for a block whose first frame is the host's timeline
/// sample `timeline`; `has_timeline` 0 while the host gives no position (a
/// stopped transport). A reader on another track lines its own frames up with
/// these by it, whichever track the host ran first. THE AUDIO THREAD.
///
/// # Safety
/// As `abus_pusher_push`.
#[no_mangle]
pub unsafe extern "C" fn abus_pusher_push_at(
    p: *mut AbusPusher,
    interleaved: *const f32,
    frames: u32,
    timeline: i64,
    has_timeline: c_int,
) {
    let Some(p) = p.as_mut() else { return };
    if interleaved.is_null() || frames == 0 {
        return;
    }
    let n = frames as usize * bus_core::CHANNELS as usize;
    p.0.push_at(
        core::slice::from_raw_parts(interleaved, n),
        (has_timeline != 0).then_some(timeline),
    );
}

/// # Safety
/// `w` must come from `abus_writer_claim`.
#[no_mangle]
pub unsafe extern "C" fn abus_writer_set_sample_rate(w: *mut AbusWriter, sample_rate: u32) {
    if let Some(w) = w.as_mut() {
        w.0.set_sample_rate(sample_rate);
    }
}

/// Set the display name. `text` is NUL-terminated UTF-8; anything past 31
/// bytes is dropped, at a character boundary.
///
/// # Safety
/// `text` must be a valid NUL-terminated string or NULL.
#[no_mangle]
pub unsafe extern "C" fn abus_writer_set_label(w: *mut AbusWriter, text: *const c_char) {
    let Some(w) = w.as_mut() else { return };
    w.0.set_label(&cstr(text));
}

/// # Safety
/// `out` must be a valid, writable pointer.
#[no_mangle]
pub unsafe extern "C" fn abus_reader_open(slot: u32, out: *mut *mut AbusReader) -> c_int {
    if out.is_null() {
        return ABUS_ERR_UNAVAILABLE;
    }
    match Reader::open(slot) {
        Some(r) => {
            *out = Box::into_raw(Box::new(AbusReader(r)));
            ABUS_OK
        }
        None => ABUS_ERR_UNAVAILABLE,
    }
}

/// # Safety
/// `r` must come from `abus_reader_open` and must not be used afterwards.
#[no_mangle]
pub unsafe extern "C" fn abus_reader_close(r: *mut AbusReader) {
    if !r.is_null() {
        drop(Box::from_raw(r));
    }
}

/// Read up to `max_frames`. Returns the frames delivered; `dropped` and
/// `resynced` report what was missed, and may be NULL if the caller does not
/// care -- though a caller that never looks at `dropped` is a caller that
/// cannot tell a gap from silence.
///
/// # Safety
/// `out` must point at `max_frames * abus_channels()` writable floats.
#[no_mangle]
pub unsafe extern "C" fn abus_reader_read(
    r: *mut AbusReader,
    out: *mut f32,
    max_frames: u32,
    dropped: *mut u64,
    resynced: *mut i32,
) -> u32 {
    if r.is_null() || out.is_null() || max_frames == 0 {
        return 0;
    }
    let n = max_frames as usize * bus_core::CHANNELS as usize;
    let res = (*r).0.read(core::slice::from_raw_parts_mut(out, n));
    if !dropped.is_null() {
        *dropped = res.dropped;
    }
    if !resynced.is_null() {
        *resynced = i32::from(res.resynced);
    }
    res.frames
}

/// The stream position of the next frame `abus_reader_read` delivers, so a
/// read of `n` frames delivered positions `abus_reader_position(r) - n ..`.
///
/// # Safety
/// `r` must come from `abus_reader_open`, or be NULL (0).
#[no_mangle]
pub unsafe extern "C" fn abus_reader_position(r: *const AbusReader) -> u64 {
    r.as_ref().map_or(0, |r| r.0.position())
}

/// Where stream position `frame` -- one this reader has read -- sat on the
/// sender's timeline. Returns 1 and writes `*timeline` when the sender
/// stamped it; 0 when it did not (a stopped transport) or its stamp is gone.
/// Loads only: the reading thread may ask, the audio thread included.
///
/// # Safety
/// `r` must come from `abus_reader_open`, or be NULL; `timeline` writable.
#[no_mangle]
pub unsafe extern "C" fn abus_reader_timeline_of(r: *const AbusReader, frame: u64, timeline: *mut i64) -> c_int {
    let Some(r) = r.as_ref() else { return 0 };
    match r.0.stamp_at(frame).and_then(|s| s.timeline_of(frame)) {
        Some(t) => {
            if !timeline.is_null() {
                *timeline = t;
            }
            1
        }
        None => 0,
    }
}

/// Move a reader to the segment its slot's name leads to now, if a sender
/// replaced the one it has. Returns 1 if it moved (the next read reports
/// `resynced`), 0 otherwise. Main thread: it makes system calls.
///
/// # Safety
/// `r` must come from `abus_reader_open`, with no read in flight.
#[no_mangle]
pub unsafe extern "C" fn abus_reader_reattach(r: *mut AbusReader) -> c_int {
    if r.is_null() {
        return 0;
    }
    c_int::from((*r).0.reattach())
}

/// Describe a slot without opening it -- what a receiver builds its source list
/// from. Returns 1 if the slot exists, 0 if nobody has ever used it.
///
/// `label` receives a NUL-terminated name of at most `label_cap` bytes.
///
/// # Safety
/// `label` must point at `label_cap` writable bytes, or be NULL.
#[no_mangle]
pub unsafe extern "C" fn abus_probe(
    slot: u32,
    live: *mut i32,
    sample_rate: *mut u32,
    label: *mut c_char,
    label_cap: u32,
) -> c_int {
    let info = match bus_core::probe(slot) {
        Some(i) => i,
        None => {
            if !live.is_null() {
                *live = 0;
            }
            if !sample_rate.is_null() {
                *sample_rate = 0;
            }
            if !label.is_null() && label_cap > 0 {
                *label = 0;
            }
            return 0;
        }
    };
    if !live.is_null() {
        *live = i32::from(info.live);
    }
    if !sample_rate.is_null() {
        *sample_rate = info.sample_rate;
    }
    if !label.is_null() && label_cap > 0 {
        let bytes = info.label.as_bytes();
        let n = core::cmp::min(bytes.len(), label_cap as usize - 1);
        core::ptr::copy_nonoverlapping(bytes.as_ptr(), label.cast::<u8>(), n);
        *label.add(n) = 0;
    }
    1
}

/// Which segment slot `slot`'s name leads to now, or 0 when nobody has ever
/// used it. A reader whose `abus_reader_incarnation` differs maps a segment
/// that was replaced -- its sender quit and a new one created the bus -- and
/// will never hear from it again: open the slot afresh.
#[no_mangle]
pub extern "C" fn abus_incarnation(slot: u32) -> u64 {
    bus_core::probe(slot).map_or(0, |i| i.incarnation)
}

/// Which segment `r` maps (see `abus_incarnation`); 0 for NULL. Ask before
/// lending the reader to the thread that reads it.
///
/// # Safety
/// `r` must come from `abus_reader_open`, or be NULL.
#[no_mangle]
pub unsafe extern "C" fn abus_reader_incarnation(r: *const AbusReader) -> u64 {
    r.as_ref().map_or(0, |r| r.0.incarnation())
}

/// # Safety
/// `p` must be NUL-terminated or NULL.
unsafe fn cstr(p: *const c_char) -> String {
    let p = p.cast::<u8>();
    if p.is_null() {
        return String::new();
    }
    let mut n = 0usize;
    while *p.add(n) != 0 && n < 4096 {
        n += 1;
    }
    String::from_utf8_lossy(core::slice::from_raw_parts(p, n)).into_owned()
}

#[cfg(test)]
mod tests;

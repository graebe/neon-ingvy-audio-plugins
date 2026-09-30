/*
 * The C ABI: what a sending plugin and a receiving plugin both link.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * `engines/audio-bus/include/audio_bus.h` is the contract; this is the half
 * that implements it. The header is written by hand rather than generated, for
 * the reason spectro states: a generator hides a disagreement by overwriting
 * it, and `abus_roundtrip.c` compiles against the header and links this, which
 * is what catches drift.
 *
 * Scalars and float buffers cross the boundary. No callbacks, no shared
 * structs, no ownership passing either way except the two opaque handles.
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

use bus_core::{ClaimError, Pusher, Reader, Writer};

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
pub struct AbusWriter(Writer);
pub struct AbusPusher(Pusher);
pub struct AbusReader(Reader);

/* Mirrors ABUS_OK / ABUS_ERR_* in the header. */
const ABUS_OK: i32 = 0;
const ABUS_ERR_BAD_SLOT: i32 = -1;
const ABUS_ERR_UNAVAILABLE: i32 = -2;
const ABUS_ERR_TAKEN: i32 = -3;

fn code(e: ClaimError) -> i32 {
    match e {
        ClaimError::BadSlot => ABUS_ERR_BAD_SLOT,
        ClaimError::Unavailable => ABUS_ERR_UNAVAILABLE,
        ClaimError::Taken => ABUS_ERR_TAKEN,
    }
}

#[no_mangle]
pub extern "C" fn abus_max_slot() -> u32 {
    bus_core::MAX_SLOT
}

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
) -> i32 {
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
pub unsafe extern "C" fn abus_writer_set_label(w: *mut AbusWriter, text: *const u8) {
    let Some(w) = w.as_mut() else { return };
    w.0.set_label(&cstr(text));
}

/// # Safety
/// `out` must be a valid, writable pointer.
#[no_mangle]
pub unsafe extern "C" fn abus_reader_open(slot: u32, out: *mut *mut AbusReader) -> i32 {
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

/// Move a reader to the segment its slot's name leads to now, if a sender
/// replaced the one it has. Returns 1 if it moved (the next read reports
/// `resynced`), 0 otherwise. Main thread: it makes system calls.
///
/// # Safety
/// `r` must come from `abus_reader_open`, with no read in flight.
#[no_mangle]
pub unsafe extern "C" fn abus_reader_reattach(r: *mut AbusReader) -> i32 {
    if r.is_null() {
        return 0;
    }
    i32::from((*r).0.reattach())
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
    label: *mut u8,
    label_cap: u32,
) -> i32 {
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
        core::ptr::copy_nonoverlapping(bytes.as_ptr(), label, n);
        *label.add(n) = 0;
    }
    1
}

/// # Safety
/// `p` must be NUL-terminated or NULL.
unsafe fn cstr(p: *const u8) -> String {
    if p.is_null() {
        return String::new();
    }
    let mut n = 0usize;
    while *p.add(n) != 0 && n < 4096 {
        n += 1;
    }
    String::from_utf8_lossy(core::slice::from_raw_parts(p, n)).into_owned()
}

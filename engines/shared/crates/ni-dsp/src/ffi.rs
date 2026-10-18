// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The C side of a capi crate's entry points: strings, the transport, and the
blocks and handles every engine's C ABI takes -- one reading of a pointer and
a length, so each product's `extern "C"` function is its own name and a line.
*/

use crate::Transport;
use std::ffi::{c_char, c_int, CStr};

/// A `*const c_char` as a `&str`, or "" -- which is what C's `atof` and
/// `strcmp` effectively did with junk. Invalid UTF-8 is treated as absent
/// rather than panicking: this runs on an audio callback.
///
/// # Safety
/// `p` is null or a NUL-terminated string that outlives `'a`.
pub unsafe fn cstr<'a>(p: *const c_char) -> &'a str {
    if p.is_null() {
        return "";
    }
    CStr::from_ptr(p).to_str().unwrap_or("")
}

/// Copy `s` into a C buffer of `buf_len` bytes, NUL-terminated. Returns the
/// length written, or -1 -- writing nothing -- when it does not fit whole: a
/// truncated label is a different label.
///
/// # Safety
/// `buf` is null or writable for `buf_len` bytes.
pub unsafe fn copy_cstr(s: &str, buf: *mut c_char, buf_len: c_int) -> c_int {
    if buf.is_null() || buf_len <= 0 || s.len() >= buf_len as usize {
        return -1;
    }
    /* A raw copy, not a `&mut [u8]` over the buffer: the caller's bytes may be
     * uninitialised C memory, and a slice of u8 would claim they are not. */
    core::ptr::copy_nonoverlapping(s.as_ptr(), buf as *mut u8, s.len());
    *buf.add(s.len()) = 0;
    s.len() as c_int
}

/// A host's float block, `frames` long: `left`, and `right` unless it is null
/// (a mono track). Nothing when `left` is null or the block is empty.
///
/// # Safety
/// `left` is null or holds `frames` floats, and so does `right` unless it is
/// null; the two do not overlap, and both outlive `'a`.
pub unsafe fn f32_block<'a>(left: *mut f32, right: *mut f32, frames: u32) -> Option<(&'a mut [f32], Option<&'a mut [f32]>)> {
    if left.is_null() || frames == 0 {
        return None;
    }
    let l = std::slice::from_raw_parts_mut(left, frames as usize);
    let r = (!right.is_null()).then(|| std::slice::from_raw_parts_mut(right, frames as usize));
    Some((l, r))
}

/// The Move's interleaved 16-bit stereo, `frames` frames. Nothing when `buf`
/// is null.
///
/// # Safety
/// `buf` is null or holds `2 * frames` values that outlive `'a`.
pub unsafe fn i16_block<'a>(buf: *mut i16, frames: u32) -> Option<&'a mut [i16]> {
    (!buf.is_null()).then(|| std::slice::from_raw_parts_mut(buf, 2 * frames as usize))
}

/// `len` bytes -- a MIDI message. Nothing when `bytes` is null or `len` is 0.
///
/// # Safety
/// `bytes` is null or holds `len` bytes that outlive `'a`.
pub unsafe fn byte_slice<'a>(bytes: *const u8, len: usize) -> Option<&'a [u8]> {
    (!bytes.is_null() && len > 0).then(|| std::slice::from_raw_parts(bytes, len))
}

/// A handle made with `Box::into_raw`, dropped; null is nothing.
///
/// # Safety
/// `p` is null or from `Box::into_raw`, and nothing uses it after.
pub unsafe fn drop_box<T>(p: *mut T) {
    if !p.is_null() {
        drop(Box::from_raw(p));
    }
}

/// A host's sample rate, or nothing when it is not a positive number --
/// NaN included -- which every engine ignores rather than adopts.
pub fn usable_rate(sample_rate: f64) -> Option<f64> {
    (sample_rate > 0.0 && sample_rate.is_finite()).then_some(sample_rate)
}

/// The transport as every engine's C header declares it.
#[repr(C)]
pub struct CTransport {
    pub running: c_int,
    pub beats: f64,
    pub bpm: f32,
}

impl CTransport {
    /// `NULL` means "the host told us nothing", which is NOT a stopped
    /// transport: a stopped transport still reports a tempo.
    ///
    /// # Safety
    /// `t` is null or points at a valid `CTransport`.
    pub unsafe fn read(t: *const CTransport) -> Option<Transport> {
        t.as_ref().map(|t| Transport {
            running: t.running != 0,
            beats: t.beats,
            bpm: t.bpm,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::ffi::CString;

    #[test]
    fn junk_is_empty_and_null_is_nothing() {
        let ok = CString::new("rate").unwrap();
        let bad = [0xFFu8 as c_char, 0];
        unsafe {
            assert_eq!(cstr(ok.as_ptr()), "rate");
            assert_eq!(cstr(bad.as_ptr()), "");
            assert_eq!(cstr(std::ptr::null()), "");
            assert!(CTransport::read(std::ptr::null()).is_none());
            let t = CTransport { running: 2, beats: 1.5, bpm: 90.0 };
            let r = CTransport::read(&t).unwrap();
            assert!(r.running && r.beats == 1.5 && r.bpm == 90.0);
        }
    }

    #[test]
    fn blocks_and_handles_read_as_c_gives_them() {
        let mut l = [1.0f32, 2.0];
        let mut r = [3.0f32, 4.0];
        let mut buf = [0i16; 4];
        let midi = [0xB0u8, 120, 0];
        unsafe {
            let (a, b) = f32_block(l.as_mut_ptr(), r.as_mut_ptr(), 2).unwrap();
            assert_eq!((a.len(), b.map(|b| b.len())), (2, Some(2)));
            assert!(f32_block(l.as_mut_ptr(), std::ptr::null_mut(), 2).unwrap().1.is_none());
            assert!(f32_block(std::ptr::null_mut(), r.as_mut_ptr(), 2).is_none());
            assert!(f32_block(l.as_mut_ptr(), r.as_mut_ptr(), 0).is_none());
            assert_eq!(i16_block(buf.as_mut_ptr(), 2).unwrap().len(), 4);
            assert!(i16_block(std::ptr::null_mut(), 2).is_none());
            assert_eq!(byte_slice(midi.as_ptr(), 3), Some(&midi[..]));
            assert!(byte_slice(midi.as_ptr(), 0).is_none());
            assert!(byte_slice(std::ptr::null(), 3).is_none());
            drop_box(Box::into_raw(Box::new(7u32)));
            drop_box::<u32>(std::ptr::null_mut());
        }
        assert_eq!(usable_rate(48_000.0), Some(48_000.0));
        for bad in [0.0, -1.0, f64::NAN, f64::INFINITY] {
            assert_eq!(usable_rate(bad), None, "{bad}");
        }
    }

    #[test]
    fn a_copy_fits_whole_or_not_at_all() {
        let mut buf = [0x7f as c_char; 5];
        unsafe {
            assert_eq!(copy_cstr("1/16", buf.as_mut_ptr(), 5), 4);
            assert_eq!(cstr(buf.as_ptr()), "1/16");
            assert_eq!(copy_cstr("1/16T", buf.as_mut_ptr(), 5), -1);
            assert_eq!(cstr(buf.as_ptr()), "1/16", "a refused copy writes nothing");
            assert_eq!(copy_cstr("x", std::ptr::null_mut(), 5), -1);
            /* No room, or a length C got wrong, is a refusal too. */
            assert_eq!(copy_cstr("", buf.as_mut_ptr(), 0), -1);
            assert_eq!(copy_cstr("", buf.as_mut_ptr(), -1), -1);
            assert_eq!(cstr(buf.as_ptr()), "1/16");
            /* The empty label needs the terminator's byte and no more. */
            assert_eq!(copy_cstr("", buf.as_mut_ptr(), 1), 0);
            assert_eq!(cstr(buf.as_ptr()), "");
        }
    }
}

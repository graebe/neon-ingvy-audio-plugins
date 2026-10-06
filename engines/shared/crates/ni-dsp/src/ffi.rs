// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The C side of a capi crate's entry points: strings and the transport.
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
    core::ptr::copy_nonoverlapping(s.as_ptr(), buf as *mut u8, s.len());
    *buf.add(s.len()) = 0;
    s.len() as c_int
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
    fn a_copy_fits_whole_or_not_at_all() {
        let mut buf = [0x7f as c_char; 5];
        unsafe {
            assert_eq!(copy_cstr("1/16", buf.as_mut_ptr(), 5), 4);
            assert_eq!(cstr(buf.as_ptr()), "1/16");
            assert_eq!(copy_cstr("1/16T", buf.as_mut_ptr(), 5), -1);
            assert_eq!(cstr(buf.as_ptr()), "1/16", "a refused copy writes nothing");
            assert_eq!(copy_cstr("x", std::ptr::null_mut(), 5), -1);
        }
    }
}

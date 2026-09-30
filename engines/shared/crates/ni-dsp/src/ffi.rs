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
}

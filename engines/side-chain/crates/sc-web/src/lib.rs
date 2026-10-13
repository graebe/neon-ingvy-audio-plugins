// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
NI Side-Chain's C ABI, built for the browser (wasm32-unknown-unknown).

The web editor itself needs no engine: it draws the duck from the module's
`params` readout with the kit's curve. What runs this one is the editor's
development harness and its end-to-end tests, which stand in for the Move with
the real engine behind a fake of Schwung's web manager -- so the values, the
readouts and the playhead the page is tested against are the engine's own,
not a model of them.

As tg-web: every `sc_core_*` symbol re-exported unchanged, plus memory for a
caller that cannot bring its own.

```text
sc_web_alloc(n) -> ptr      n bytes the caller writes into; 0 when n is 0
sc_web_free(ptr, n)         give them back, with the same n
```
*/

/* Forces the sc_core_* exports into this cdylib; see tg-web. */
extern crate sc_capi as _sc_capi;

/// `n` bytes for the caller, uninitialised; null for 0. Freed with
/// [`sc_web_free`] and the same `n`.
#[no_mangle]
pub extern "C" fn sc_web_alloc(n: usize) -> *mut u8 {
    if n == 0 {
        return std::ptr::null_mut();
    }
    let mut v = Vec::<u8>::with_capacity(n);
    let p = v.as_mut_ptr();
    std::mem::forget(v);
    p
}

/// Give back what [`sc_web_alloc`] handed out.
///
/// # Safety
/// `p` is null or came from `sc_web_alloc(n)` with this `n`, and is not used
/// again.
#[no_mangle]
pub unsafe extern "C" fn sc_web_free(p: *mut u8, n: usize) {
    if !p.is_null() && n != 0 {
        drop(Vec::from_raw_parts(p, 0, n));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn memory_round_trips() {
        let p = sc_web_alloc(64);
        assert!(!p.is_null());
        unsafe {
            p.write_bytes(7, 64);
            assert_eq!(*p.add(63), 7);
            sc_web_free(p, 64);
        }
        assert!(sc_web_alloc(0).is_null());
        unsafe { sc_web_free(std::ptr::null_mut(), 0) };
    }
}

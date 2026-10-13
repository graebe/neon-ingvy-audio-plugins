// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The Trance Gate's C ABI, built for the browser (wasm32-unknown-unknown).

The web editor on the Move (Schwung's web manager, modules/trance-gate/web)
draws the pattern and envelope plots from the curves the ENGINE renders -- a
scratch engine running the patch with a DC input, so the picture is the sound
(tg-capi's gate.rs, envelope.rs). The plugin's editor gets them through
`tg_core_render_gate` / `tg_core_render_envelope`; the web editor calls the very
same two, compiled into this module, on the patch the Move publishes as `state`.

What the browser cannot do without help is hand a C function a pointer. So this
crate adds the one thing a C caller would bring itself: memory.

```text
tg_web_alloc(n) -> ptr      n bytes the caller writes into (the patch and its
                            NUL, or the output buffer); 0 when n is 0
tg_web_free(ptr, n)         give them back, with the same n
```

and re-exports every `tg_core_*` symbol unchanged.
*/

/* Forces the tg_core_* exports into this cdylib: nothing here names tg-capi,
 * and without a reference the linker drops its #[no_mangle] symbols. */
extern crate tg_capi as _tg_capi;

/// `n` bytes for the caller, uninitialised; null for 0. Freed with
/// [`tg_web_free`] and the same `n`.
#[no_mangle]
pub extern "C" fn tg_web_alloc(n: usize) -> *mut u8 {
    if n == 0 {
        return std::ptr::null_mut();
    }
    let mut v = Vec::<u8>::with_capacity(n);
    let p = v.as_mut_ptr();
    std::mem::forget(v);
    p
}

/// Give back what [`tg_web_alloc`] handed out.
///
/// # Safety
/// `p` is null or came from `tg_web_alloc(n)` with this `n`, and is not used
/// again.
#[no_mangle]
pub unsafe extern "C" fn tg_web_free(p: *mut u8, n: usize) {
    if !p.is_null() && n != 0 {
        drop(Vec::from_raw_parts(p, 0, n));
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::ffi::{c_char, c_int};

    extern "C" {
        fn tg_core_render_gate(state: *const c_char, buf: *mut c_char, len: c_int) -> c_int;
    }

    /* The browser's sequence, here: alloc the patch and the output, render,
     * free -- and the curve is the engine's. */
    #[test]
    fn a_patch_renders_through_memory_the_module_handed_out() {
        let patch = br#"{"sv":7,"slot":0,"p0":"5555:0:16:"}"#;
        unsafe {
            let n = patch.len() + 1;
            let s = tg_web_alloc(n);
            std::ptr::copy_nonoverlapping(patch.as_ptr(), s, patch.len());
            *s.add(patch.len()) = 0;
            let cap = tg_capi::TG_GATE_MAX;
            let out = tg_web_alloc(cap);
            let got = tg_core_render_gate(s.cast(), out.cast(), cap as c_int);
            assert!(got > 0, "the engine drew nothing");
            let head = std::slice::from_raw_parts(out, 8);
            assert!(head.starts_with(b"16:"), "{:?}", String::from_utf8_lossy(head));
            tg_web_free(s, n);
            tg_web_free(out, cap);
        }
        assert!(tg_web_alloc(0).is_null());
        unsafe { tg_web_free(std::ptr::null_mut(), 0) };
    }
}

//! One C function, called by the smoke plugin's processBlock on every platform.

/// Multiplies `len` samples at `samples` by `gain`, in place. A null pointer
/// is a no-op.
///
/// # Safety
/// `samples` must be null or point to `len` writable `f32`s.
#[no_mangle]
pub unsafe extern "C" fn ni_smoke_apply_gain(samples: *mut f32, len: usize, gain: f32) {
    if samples.is_null() {
        return;
    }
    // SAFETY: the caller guarantees `len` valid, writable samples.
    let block = unsafe { core::slice::from_raw_parts_mut(samples, len) };
    block.iter_mut().for_each(|sample| *sample *= gain);
}

#[cfg(test)]
mod tests {
    #[test]
    fn scales_in_place_and_ignores_null() {
        let mut block = [1.0_f32, -0.5, 0.25];
        unsafe { super::ni_smoke_apply_gain(block.as_mut_ptr(), block.len(), 0.5) };
        assert_eq!(block, [0.5, -0.25, 0.125]);
        unsafe { super::ni_smoke_apply_gain(core::ptr::null_mut(), 3, 0.5) };
    }
}

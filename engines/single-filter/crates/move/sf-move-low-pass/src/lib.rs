// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
NI Low Pass on the Move: the single filters' module (sf-move) for this product,
exported as the one `move_audio_fx_init_v2` its .so carries.
*/

ni_schwung::export_audio_fx!(sf_move::SingleFilter<sf_move::LowPass>);

#[cfg(test)]
mod tests {
    use sf_move::Product;

    /// The table the device gets is this product's.
    #[test]
    fn the_device_gets_this_products_module() {
        /* SAFETY: init returns its module type's table, a constant that lives
         * as long as the process. */
        let api = unsafe { &*super::move_audio_fx_init_v2(std::ptr::null()) };
        let inst = (api.create_instance.unwrap())(std::ptr::null(), std::ptr::null());
        let key = std::ffi::CString::new("chain_params").unwrap();
        let mut buf = [0 as std::ffi::c_char; 4096];
        let n = (api.get_param.unwrap())(inst, key.as_ptr(), buf.as_mut_ptr(), buf.len() as std::ffi::c_int);
        let got: Vec<u8> = buf[..n as usize].iter().map(|c| *c as u8).collect();
        assert_eq!(got, sf_move::LowPass::CHAIN_PARAMS.as_bytes());
        (api.destroy_instance.unwrap())(inst);
    }
}

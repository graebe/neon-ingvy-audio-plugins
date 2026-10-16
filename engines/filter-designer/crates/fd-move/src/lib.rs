// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The Schwung shell for NI Filter Designer: its `chain_params` and a [`Module`]
impl over [`fd_core`]. The vtable, the transport and the entry points are
`ni_schwung`'s.

WHAT THE MODULE IS. The same engine as the plugin, set up as the isolator a
DJ reaches for, framed by a high-pass and a low-pass (params.rs): node 1 a
Butterworth high-pass and node 2 a Butterworth low-pass, both bypassed until
switched on; the isolator on.

THE PANIC. The v2 vtable has no reset hook, so a host panic reaches a module
only as CC 120 (All Sound Off) or CC 123 (All Notes Off) through on_midi. The
engine resets on either: every filter empties, every fade lands, so the next
block starts from silence.
*/

/* Forces the fd_core_* C ABI into this staticlib: without it nothing here
 * references fd-capi and the linker drops its #[no_mangle] symbols. */
extern crate fd_capi as _fd_capi;

use core::fmt::Write;
use fd_core::{Engine, Param};
use ni_dsp::fmt::Buf;
use ni_dsp::Transport;
use ni_schwung::Module;
use std::ffi::c_int;

mod params;
mod setup;

/// The filter designer, as a Schwung module.
pub struct FilterDesigner(Engine);

impl FilterDesigner {
    /// The module's own starting point (setup.rs): the cuts in their
    /// slots, the isolator on -- what `chain_params` declares as those
    /// knobs' defaults.
    fn set_up(e: &mut Engine) {
        for (p, v) in setup::SET_UP {
            e.set_num(p, v);
        }
        e.reset();
    }
}

impl Module for FilterDesigner {
    const CHAIN_PARAMS: &'static str = params::CHAIN_PARAMS;
    fn new(sample_rate: f64) -> Self {
        let mut e = Engine::new(sample_rate);
        FilterDesigner::set_up(&mut e);
        FilterDesigner(e)
    }
    /* No tempo is needed: a filter keeps no time but the samples'. */
    fn process_i16(&mut self, buf: &mut [i16], frames: usize, _t: Option<&Transport>) {
        self.0.process_i16(buf, frames);
    }
    fn set_param(&mut self, key: &str, val: &str) {
        self.0.set_param(key, val);
    }
    /* A frequency knob reads back the option it sent, in its words. */
    fn get_param(&self, key: &str, out: &mut [u8]) -> c_int {
        if let Some(label) = frequency_label(key, self.0.num_by_key(key)) {
            let mut b = Buf::new(out);
            return if b.write_str(label).is_ok() { b.finish() } else { -1 };
        }
        self.0.get_param(key, out)
    }
    fn on_midi(&mut self, msg: &[u8]) {
        self.0.on_midi(msg);
    }
}

/// The declared option a frequency knob's value is, if it is one.
fn frequency_label(key: &str, hz: Option<f64>) -> Option<&'static str> {
    let hz = hz?;
    let p = Param::from_key(key)?;
    let (_, labels) = params::FREQUENCY_OPTIONS.iter().find(|(k, _)| *k == key)?;
    labels.iter().copied().find(|l| p.parse_value(l).is_some_and(|v| (v - hz).abs() < 1e-6))
}

ni_schwung::export_audio_fx!(FilterDesigner);

/*
 * THE MOVE'S PARAMETER PATH, END TO END: the entry points the chain host
 * calls, with the values it actually sends -- a declared default on a
 * Delete-to-default, an option's label on a turn of a frequency knob -- and
 * the panic through on_midi.
 */
#[cfg(test)]
mod tests {
    use super::*;
    use ni_schwung::AudioFxApiV2;
    use serde_json::Value;
    use std::ffi::{c_char, c_void, CString};

    fn api() -> &'static AudioFxApiV2 {
        /* SAFETY: init returns its module type's table, a constant that lives
         * as long as the process. */
        unsafe { &*move_audio_fx_init_v2(std::ptr::null()) }
    }

    struct Instance(*mut c_void);
    impl Instance {
        fn new() -> Self {
            Instance((api().create_instance.unwrap())(std::ptr::null(), std::ptr::null()))
        }
        fn set(&self, k: &str, v: &str) {
            let (k, v) = (CString::new(k).unwrap(), CString::new(v).unwrap());
            (api().set_param.unwrap())(self.0, k.as_ptr(), v.as_ptr());
        }
        fn get(&self, k: &str) -> Option<String> {
            let k = CString::new(k).unwrap();
            let mut buf = [0 as c_char; 4096];
            let n = (api().get_param.unwrap())(self.0, k.as_ptr(), buf.as_mut_ptr(), buf.len() as c_int);
            (n >= 0).then(|| String::from_utf8(buf[..n as usize].iter().map(|c| *c as u8).collect()).unwrap())
        }
        fn process(&self, buf: &mut [i16; 256]) {
            (api().process_block.unwrap())(self.0, buf.as_mut_ptr(), 128);
        }
    }
    impl Drop for Instance {
        fn drop(&mut self) {
            (api().destroy_instance.unwrap())(self.0);
        }
    }

    #[test]
    fn the_device_reads_the_declaration_these_tests_check() {
        let m = Instance::new();
        assert_eq!(m.get("chain_params").as_deref(), Some(params::CHAIN_PARAMS));
        assert_eq!(m.get("no_such_key"), None);
    }

    #[test]
    fn every_declared_default_is_what_the_module_starts_at_and_reads_back() {
        let declared: Value = serde_json::from_str(params::CHAIN_PARAMS).unwrap();
        let fresh = Instance::new();
        let m = Instance::new();
        for e in declared.as_array().unwrap() {
            let key = e["key"].as_str().unwrap();
            let default = match &e["default"] {
                Value::String(s) => s.clone(),
                v => v.to_string(),
            };
            let start = fresh.get(key).unwrap_or_else(|| panic!("{key} not served"));
            m.set(key, &default);
            let got = m.get(key).unwrap();
            if e["wire_format"] == "name" {
                assert_eq!(got, default, "{key} reads back its label");
                assert_eq!(start, default, "{key} starts at its declared default");
            } else {
                assert_eq!(got.parse::<f64>().unwrap(), default.parse::<f64>().unwrap(), "{key}");
                assert_eq!(start.parse::<f64>().unwrap(), default.parse::<f64>().unwrap(), "{key} starts there");
            }
        }
    }

    #[test]
    fn a_frequency_knob_sends_and_reads_its_label() {
        let m = Instance::new();
        m.set("iso_freq_hi", "2.5k");
        assert_eq!(m.get("iso_freq_hi").as_deref(), Some("2.5k"));
        m.set("n01_freq", "150");
        assert_eq!(m.get("n01_freq").as_deref(), Some("150"));
    }

    #[test]
    fn a_host_panic_on_cc_120_or_123_silences_the_next_block() {
        for cc in [120u8, 123] {
            let m = Instance::new();
            m.set("n01_on", "1");
            m.set("n01_q", "8");
            m.set("n02_on", "1");
            m.set("iso_mid", "6");
            let mut loud = [0i16; 256];
            for (i, s) in loud.iter_mut().enumerate() {
                *s = if i % 7 < 3 { 20_000 } else { -20_000 };
            }
            for _ in 0..40 {
                let mut b = loud;
                m.process(&mut b);
            }
            let panic = [0xB0u8, cc, 0];
            move_audio_fx_on_midi(m.0, panic.as_ptr(), 3, 0);
            let mut quiet = [0i16; 256];
            m.process(&mut quiet);
            assert!(quiet.iter().all(|&s| s == 0), "CC {cc}: a filter still rang");
        }
    }

    #[test]
    fn a_kill_on_the_move_takes_its_band_out() {
        let m = Instance::new();
        let level = |m: &Instance| {
            let mut sum = 0.0f64;
            for k in 0..200 {
                let mut b = [0i16; 256];
                for (i, s) in b.iter_mut().enumerate() {
                    let t = (k * 128 + i / 2) as f64;
                    *s = (10_000.0 * (2.0 * std::f64::consts::PI * 60.0 * t / 44_100.0).sin()) as i16;
                }
                m.process(&mut b);
                if k >= 150 {
                    sum += b.iter().map(|s| (*s as f64).powi(2)).sum::<f64>();
                }
            }
            sum
        };
        let open = level(&m);
        m.set("iso_low", "-60");
        assert!(level(&m) < open * 1e-4, "the low band is gone");
    }
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The Schwung shell for NI Bit-Crusher: its `chain_params` and a [`Module`] impl
over [`bc_core`]. The vtable, the transport and the entry points are
`ni_schwung`'s; `bc_render_ab` proves this path and the plugin's agree.

THE PANIC. The v2 vtable has no reset hook, so a host panic reaches a module
only as CC 120 (All Sound Off) or CC 123 (All Notes Off) through on_midi. The
engine resets on either: the held sample lets go and every filter empties, so
the next block starts from silence.

THE MOVE READS ITS ENUMS BACK IN THEIR OWN WORDS. Rate, Cutoff and Slope send
their labels (params.rs), and the knob grid shows the value it reads back; so
a value the declaration lists is reported as that label ("12k", "24 dB"), and
anything else -- a value a preset set between the options -- as its number.
Every other key reads back as the engine reports it.
*/

/* Forces the bc_core_* C ABI into this staticlib: without it nothing here
 * references bc-capi and the linker drops its #[no_mangle] symbols. */
extern crate bc_capi as _bc_capi;

use bc_core::{Engine, Param, SLOPE_LABELS};
use core::fmt::Write;
use ni_dsp::fmt::Buf;
use ni_dsp::Transport;
use ni_schwung::Module;
use std::ffi::c_int;

mod params;

/// The crusher, as a Schwung module.
pub struct Crusher(Engine);

impl Module for Crusher {
    const CHAIN_PARAMS: &'static str = params::CHAIN_PARAMS;
    fn new(sample_rate: f64) -> Self {
        Crusher(Engine::new(sample_rate))
    }
    /* No tempo is needed: a crusher keeps no time but the samples'. */
    fn process_i16(&mut self, buf: &mut [i16], frames: usize, _t: Option<&Transport>) {
        self.0.process_i16(buf, frames);
    }
    fn set_param(&mut self, key: &str, val: &str) {
        self.0.set_param(key, val);
    }
    fn get_param(&self, key: &str, out: &mut [u8]) -> c_int {
        /* The enums that send their labels read back in them too. */
        let label = match key {
            "rate" => rate_label(self.0.num(Param::Rate)),
            "cutoff" => cutoff_label(self.0.num(Param::Cutoff)),
            "slope" => SLOPE_LABELS.get(self.0.num(Param::Slope) as usize).copied(),
            _ => None,
        };
        if let Some(label) = label {
            let mut b = Buf::new(out);
            return if b.write_str(label).is_ok() { b.finish() } else { -1 };
        }
        self.0.get_param(key, out)
    }
    fn on_midi(&mut self, msg: &[u8]) {
        self.0.on_midi(msg);
    }
}

/// The declared option a rate is, if it is one.
fn rate_label(hz: f64) -> Option<&'static str> {
    params::RATE_LABELS.into_iter().find(|l| Param::Rate.parse_value(l) == Some(hz))
}

/// The declared option a cutoff is, if it is one.
fn cutoff_label(hz: f64) -> Option<&'static str> {
    params::CUTOFF_LABELS.into_iter().find(|l| Param::Cutoff.parse_value(l) == Some(hz))
}

ni_schwung::export_audio_fx!(Crusher);

#[cfg(test)]
mod no_alloc;

/*
 * THE MOVE'S PARAMETER PATH, END TO END: the two entry points the chain host
 * calls, with the values it actually sends -- a declared default on a
 * Delete-to-default, an option's label on a turn of the Rate knob -- and the
 * panic through on_midi.
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
        assert_eq!(m.get("ui_hierarchy").as_deref(), Some(""));
        assert_eq!(m.get("no_such_key"), None);
    }

    #[test]
    fn every_declared_default_is_what_the_engine_reads_back() {
        let declared: Value = serde_json::from_str(params::CHAIN_PARAMS).unwrap();
        let m = Instance::new();
        for e in declared.as_array().unwrap() {
            let key = e["key"].as_str().unwrap();
            let default = match &e["default"] {
                Value::String(s) => s.clone(),
                other => other.to_string(),
            };
            m.set(key, &default);
            let got = m.get(key).unwrap_or_else(|| panic!("{key} not served"));
            /* An enum that sends its label reads back in it. */
            if e["wire_format"].as_str() == Some("name") {
                assert_eq!(got, default, "{key}");
            } else {
                assert_eq!(got.parse::<f64>().unwrap(), default.parse::<f64>().unwrap(), "{key}");
            }
        }
    }

    #[test]
    fn every_rate_option_round_trips_as_its_label() {
        let m = Instance::new();
        for label in params::RATE_LABELS {
            m.set("rate", label);
            assert_eq!(m.get("rate").as_deref(), Some(label));
        }
        /* A rate between the options reads back as its number. */
        m.set("rate", "11025");
        assert_eq!(m.get("rate").as_deref(), Some("11025"));
    }

    #[test]
    fn a_host_panic_on_cc_120_or_123_silences_the_next_block() {
        for cc in [120u8, 123] {
            let m = Instance::new();
            m.set("rate", "250");
            m.set("bits", "3");
            let mut loud = [20_000i16; 256];
            m.process(&mut loud);
            let panic = [0xB0u8, cc, 0];
            move_audio_fx_on_midi(m.0, panic.as_ptr(), 3, 0);
            let mut quiet = [0i16; 256];
            m.process(&mut quiet);
            assert!(quiet.iter().all(|&s| s == 0), "CC {cc}: the hold still held");
        }
    }

    #[test]
    fn the_module_crushes() {
        let m = Instance::new();
        m.set("bits", "2");
        m.set("rate", "48k");
        let mut buf = [0i16; 256];
        for (i, s) in buf.iter_mut().enumerate() {
            *s = ((i as f64 / 256.0 * std::f64::consts::TAU).sin() * 30_000.0) as i16;
        }
        /* A second of audio, so the glides from the defaults have landed. */
        for _ in 0..400 {
            let mut warm = buf;
            m.process(&mut warm);
        }
        m.process(&mut buf);
        /* Two bits, mid-tread: five levels, -1, -0.5, 0, 0.5, 1. */
        assert!(buf.iter().all(|s| [-32_768, -16_384, 0, 16_384, 32_767].contains(s)), "{buf:?}");
    }

    #[test]
    fn the_filter_page_writes_and_reads_back_in_its_own_words() {
        let m = Instance::new();
        assert_eq!(m.get("filter").as_deref(), Some("0"), "off by default");
        m.set("filter", "1");
        m.set("cutoff", "1.2k");
        m.set("slope", "48 dB");
        m.set("peak", "6");
        assert_eq!(m.get("filter").as_deref(), Some("1"));
        assert_eq!(m.get("cutoff").as_deref(), Some("1.2k"));
        assert_eq!(m.get("slope").as_deref(), Some("48 dB"));
        assert_eq!(m.get("peak").as_deref(), Some("6"));
    }
}

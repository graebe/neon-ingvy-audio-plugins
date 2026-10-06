// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The Schwung shell for NI Side-Chain: its `chain_params` and a [`Module`] impl
over [`sc_core`]. The vtable, the transport and the entry points are
`ni_schwung`'s; `sc_render_ab` proves this and the plugin agree.

The Move has no sidechain bus -- one interleaved buffer, no aux input -- so
`chain_params` offers Cycle and MIDI only. MIDI is a trigger source here, and
the only way a host panic (CC 120/123) reaches the module.
*/

/* Forces the sc_core_* C ABI into this staticlib: without it nothing here
 * references sc-capi and the linker drops its #[no_mangle] symbols. */
extern crate sc_capi as _sc_capi;

use ni_schwung::Module;
use sc_core::{Instance, Transport};
use std::ffi::c_int;

mod params;

/// The ducker, as a Schwung module.
pub struct Ducker(Instance);

impl Module for Ducker {
    const CHAIN_PARAMS: &'static str = params::CHAIN_PARAMS;
    fn new(sample_rate: f64) -> Self {
        Ducker(Instance::new(sample_rate))
    }
    fn process_i16(&mut self, buf: &mut [i16], frames: usize, t: Option<&Transport>) {
        self.0.process_i16(buf, frames, t);
    }
    fn set_param(&mut self, key: &str, val: &str) {
        self.0.set_param(key, val);
    }
    fn get_param(&self, key: &str, out: &mut [u8]) -> c_int {
        self.0.get_param(key, out)
    }
    /* OFFSET 0: the v2 API has none to give -- up to a block of trigger
     * jitter, and what makes the render A/B meaningful at offset 0. */
    fn on_midi(&mut self, msg: &[u8]) {
        self.0.on_midi(msg, 0);
    }
}

ni_schwung::export_audio_fx!(Ducker);

/*
 * THE MOVE'S PARAMETER PATH, END TO END.
 *
 * The unit tests in `params.rs` pin the declaration against the engine's key
 * list. These drive the same two entry points the chain host calls, with the
 * values the host actually sends: the declared default (on a Delete-to-default)
 * and the option index its learner settles on (on a knob turn).
 */
#[cfg(test)]
mod tests {
    use super::*;
    use ni_schwung::AudioFxApiV2;
    use serde_json::Value;
    use std::ffi::{c_char, c_void, CString};
    use std::sync::Once;

    /// The entry for `key` in the declaration, read with serde_json as
    /// params.rs's tests read the whole of it.
    fn entry(key: &str) -> Value {
        let declared: Value = serde_json::from_str(params::CHAIN_PARAMS).expect("chain_params is not JSON");
        let entries = declared.as_array().expect("chain_params is not a JSON array");
        entries
            .iter()
            .find(|e| e["key"] == key)
            .unwrap_or_else(|| panic!("\"{key}\" is not declared"))
            .clone()
    }

    /// A string field of an entry, e.g. `default` or `wire_format`.
    fn field(key: &str, name: &str) -> Option<String> {
        entry(key)[name].as_str().map(str::to_string)
    }

    fn options(key: &str) -> Vec<String> {
        let e = entry(key);
        let options = e["options"].as_array().unwrap_or_else(|| panic!("\"{key}\" declares no options"));
        options.iter().map(|o| o.as_str().expect("an option is text").to_string()).collect()
    }

    /* The vtable, initialised once: it is process-wide, as on the device,
     * and cargo runs these tests in threads. No host: a stopped transport. */
    fn api() -> &'static AudioFxApiV2 {
        static INIT: Once = Once::new();
        static mut API: *const AudioFxApiV2 = std::ptr::null();
        unsafe {
            INIT.call_once(|| API = move_audio_fx_init_v2(std::ptr::null()));
            &*API
        }
    }

    struct Module(*mut c_void);
    impl Module {
        fn new() -> Self {
            Module((api().create_instance.unwrap())(std::ptr::null(), std::ptr::null()))
        }
        fn set(&self, k: &str, v: &str) {
            let (k, v) = (CString::new(k).unwrap(), CString::new(v).unwrap());
            (api().set_param.unwrap())(self.0, k.as_ptr(), v.as_ptr());
        }
        fn process(&self, buf: &mut [i16; 256]) {
            (api().process_block.unwrap())(self.0, buf.as_mut_ptr(), 128);
        }
        fn get(&self, k: &str) -> String {
            let k = CString::new(k).unwrap();
            let mut buf = [0 as c_char; 256];
            let n = (api().get_param.unwrap())(self.0, k.as_ptr(), buf.as_mut_ptr(), buf.len() as c_int);
            assert!(n >= 0, "{k:?} not served");
            let bytes: Vec<u8> = buf[..n as usize].iter().map(|c| *c as u8).collect();
            String::from_utf8(bytes).unwrap()
        }
    }
    impl Drop for Module {
        fn drop(&mut self) {
            (api().destroy_instance.unwrap())(self.0);
        }
    }

    #[test]
    fn every_enum_the_engine_reports_by_index_declares_it() {
        /*
         * THE HOST LEARNS AN UNDECLARED ENUM'S CONVENTION FROM A READ, and
         * until it has one it writes whatever it holds -- the declared DEFAULT
         * on a Delete, verbatim. An enum whose getter answers an index and
         * whose default is a label speaks two conventions at once; declaring
         * the index is what makes every write path agree with the read.
         */
        for key in ["source", "rate", "curve", "time_mode", "channel", "trigger_note", "midi_mode"] {
            assert_eq!(
                field(key, "wire_format").as_deref(),
                Some("index"),
                "{key} is read back as an index and must declare it"
            );
            let d = field(key, "default").unwrap();
            let n: usize = d.parse().unwrap_or_else(|_| panic!("{key} default {d:?} is not an index"));
            assert!(n < options(key).len(), "{key} default {n} is past its options");
        }
    }

    #[test]
    fn the_trigger_note_default_is_c1_on_the_device() {
        let m = Module::new();
        m.set("trigger_note", "60");
        m.set("trigger_note", &field("trigger_note", "default").unwrap());
        assert_eq!(m.get("trigger_note"), "36");
        assert_eq!(options("trigger_note")[36], "C1");
    }

    #[test]
    fn every_trigger_note_round_trips_as_an_index() {
        let m = Module::new();
        let opts = options("trigger_note");
        assert_eq!(opts.len(), 128);
        for (i, name) in opts.iter().enumerate() {
            m.set("trigger_note", &i.to_string());
            let got = m.get("trigger_note");
            assert_eq!(got, i.to_string(), "index {i}");
            /* A label reaches the same note -- a hand-written patch, or a
             * host that has not learned the convention yet. */
            m.set("trigger_note", "0");
            m.set("trigger_note", name);
            assert_eq!(m.get("trigger_note"), got, "label {name}");
        }
    }

    #[test]
    fn every_rate_round_trips_as_an_index_and_as_a_label() {
        let m = Module::new();
        for (i, name) in options("rate").iter().enumerate() {
            m.set("rate", &i.to_string());
            assert_eq!(m.get("rate"), i.to_string(), "index {i}");
            m.set("rate", "0");
            m.set("rate", name);
            assert_eq!(m.get("rate"), i.to_string(), "label {name}");
        }
        m.set("rate", &field("rate", "default").unwrap());
        assert_eq!(m.get("rate"), "4", "the default is 1/4");
    }

    #[test]
    fn panic_opens_the_gate() {
        /* The v2 vtable has no reset hook: CC 123 is the whole of a host panic. */
        let m = Module::new();
        m.set("source", "1");
        let on = [0x90u8, 36, 127];
        move_audio_fx_on_midi(m.0, on.as_ptr(), 3, 0);
        let mut buf = [10000i16; 256];
        m.process(&mut buf);
        assert!(m.get("duck").parse::<f64>().unwrap() > 0.0);
        let cc = [0xB0u8, 123, 0];
        move_audio_fx_on_midi(m.0, cc.as_ptr(), 3, 0);
        let mut buf = [10000i16; 256];
        m.process(&mut buf);
        assert_eq!(m.get("duck"), "0.0000");
        assert!(buf.iter().all(|&s| s == 10000));
    }
}

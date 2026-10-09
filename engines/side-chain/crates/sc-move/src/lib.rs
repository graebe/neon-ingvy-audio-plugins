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

    /* The vtable, from the init the device calls. It is a constant, the same
     * table whichever init returns it, and init keeps nothing of a host but
     * its clock -- so every test asks, and two asking at once is two writes
     * of the same thing. No host: a stopped transport. */
    fn api() -> &'static AudioFxApiV2 {
        /* SAFETY: init returns its module type's table, a constant that lives
         * as long as the process. */
        unsafe { &*move_audio_fx_init_v2(std::ptr::null()) }
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
    fn a_state_saved_through_the_vtable_restores_in_a_new_instance() {
        /*
         * WHAT SCHWUNG'S AUTOSAVE DOES: read `state`, store it, and on the next
         * load hand it to a fresh instance. A module that answered no `state`
         * at all -- -1, which the host reads as a FAILED read rather than as
         * "this module keeps none" -- made the autosave abandon its whole slot
         * every five seconds, so nothing in that slot was ever saved.
         */
        let a = Module::new();
        for (k, v) in [("source", "1"), ("rate", "7"), ("depth", "0.4"), ("release", "60"),
                       ("curve", "2"), ("channel", "5"), ("trigger_note", "48"), ("vel_sens", "0.3")] {
            a.set(k, v);
        }
        let blob = a.get("state");
        assert!(blob.starts_with("sc1;"), "{blob}");
        let b = Module::new();
        assert_ne!(b.get("params"), a.get("params"));
        b.set("state", &blob);
        assert_eq!(b.get("params"), a.get("params"));
        assert_eq!(b.get("state"), blob);
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
    fn the_device_reads_the_declaration_these_tests_check() {
        /*
         * WHAT THE KNOB GRID DRAWS IS WHAT get_param SERVES, not the constant
         * params.rs's tests read. Asked as the device asks -- and as the Trance
         * Gate's tests/dump_params.c asks -- with a buffer far larger than the
         * declaration, the vtable must hand back exactly that constant, so
         * every check on it is a check on the device's view.
         */
        let m = Module::new();
        let key = CString::new("chain_params").unwrap();
        let mut buf = vec![0 as c_char; 65536];
        let n = (api().get_param.unwrap())(m.0, key.as_ptr(), buf.as_mut_ptr(), buf.len() as c_int);
        assert!(n >= 0, "chain_params is not served");
        let served: Vec<u8> = buf[..n as usize].iter().map(|c| *c as u8).collect();
        assert_eq!(served, params::CHAIN_PARAMS.as_bytes());
        assert_eq!(buf[n as usize], 0, "and it is NUL-terminated");
    }

    #[test]
    fn every_declared_default_is_what_the_engine_reads_back() {
        /*
         * A DELETE-TO-DEFAULT WRITES THE DECLARED `default` VERBATIM, through
         * set_param, and the knob then shows what get_param answers. A default
         * the engine clamps, rounds or refuses is a reset that lands somewhere
         * else -- the bug `trigger_note` had. Every writable control, after a
         * different value first, so a write that is ignored cannot pass.
         */
        let declared: Value = serde_json::from_str(params::CHAIN_PARAMS).unwrap();
        let m = Module::new();
        let mut checked = 0;
        for e in declared.as_array().unwrap() {
            let key = e["key"].as_str().unwrap();
            if e["access"] == "read" || e["type"] == "canvas" {
                continue;
            }
            let (default, other) = match e["type"].as_str().unwrap() {
                "enum" => {
                    let d = field(key, "default").unwrap();
                    let other = if d == "0" { "1" } else { "0" };
                    (d, other.to_string())
                }
                "float" => {
                    let d = e["default"].as_f64().unwrap();
                    let other = if d == e["min"].as_f64().unwrap() { e["max"].as_f64() } else { e["min"].as_f64() };
                    (d.to_string(), other.unwrap().to_string())
                }
                t => panic!("\"{key}\" has a type the knob grid writes that this test does not know: {t}"),
            };
            m.set(key, &other);
            m.set(key, &default);
            let got = m.get(key);
            match e["type"].as_str().unwrap() {
                "enum" => assert_eq!(got, default, "{key}"),
                _ => {
                    let (g, d): (f64, f64) = (got.parse().unwrap(), default.parse().unwrap());
                    let step = e["step"].as_f64().unwrap();
                    assert!((g - d).abs() <= step / 2.0, "{key}: wrote {d}, reads back {g}");
                }
            }
            checked += 1;
        }
        assert!(checked >= 13, "only {checked} writable controls");
    }

    #[test]
    fn a_host_panic_on_cc_120_or_123_opens_the_gate() {
        /*
         * The v2 vtable has no reset hook: CC 120 (All Sound Off) or CC 123
         * (All Notes Off) through on_midi is the whole of a host panic, and
         * either must open the gate at once -- after a trigger, and in Gate
         * mode with the note still held, where nothing else ever would.
         */
        for mode in ["0", "1"] {
            for cc in [120u8, 123] {
                let m = Module::new();
                m.set("source", "1");
                m.set("midi_mode", mode);
                let on = [0x90u8, 36, 127];
                move_audio_fx_on_midi(m.0, on.as_ptr(), 3, 0);
                let mut buf = [10000i16; 256];
                m.process(&mut buf);
                assert!(m.get("duck").parse::<f64>().unwrap() > 0.0, "mode {mode}: the note did not duck");
                let panic = [0xB0u8, cc, 0];
                move_audio_fx_on_midi(m.0, panic.as_ptr(), 3, 0);
                let mut buf = [10000i16; 256];
                m.process(&mut buf);
                assert_eq!(m.get("duck"), "0.0000", "mode {mode}, CC {cc}");
                assert!(buf.iter().all(|&s| s == 10000), "mode {mode}, CC {cc}: the block was still ducked");
            }
        }
    }
}

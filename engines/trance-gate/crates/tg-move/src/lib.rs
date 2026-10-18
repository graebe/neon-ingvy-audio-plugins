// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The Schwung shell for the Trance Gate: its `chain_params` and a [`Module`]
impl over [`tg_core`]. The vtable, the transport and the entry points are
`ni_schwung`'s; the gate itself is the core's, the same one the plugin links.
*/

/* Forces the tg_core_* C ABI into this staticlib: without it nothing here
 * references tg-capi and the linker drops its #[no_mangle] symbols. */
extern crate tg_capi as _tg_capi;

use ni_schwung::Module;
use std::ffi::c_int;
use tg_core::{Instance, Transport};

mod params;

/// The gate, as a Schwung module.
pub struct Gate(Instance);

impl Module for Gate {
    const CHAIN_PARAMS: &'static str = params::CHAIN_PARAMS;
    fn new(sample_rate: f64) -> Self {
        Gate(Instance::new(sample_rate))
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
    /* The panic, CC 120 or 123 (Instance::on_midi). Nothing else: the arrows
     * are claimed as CCs and handled in ui_chain.js, which sees them first. */
    fn on_midi(&mut self, msg: &[u8]) {
        self.0.on_midi(msg);
    }
}

ni_schwung::export_audio_fx!(Gate);

#[cfg(test)]
mod no_alloc;
#[cfg(test)]
mod render_ab;

/*
 * THE PANIC, ON THE GATE. A host panic arrives as CC 120/123 through
 * `move_audio_fx_on_midi`, or as the `panic` key. The gate holds no note, but
 * it does hold a gesture -- an envelope part-way through a stage -- and the
 * panic lets go of it: the gate shuts at once and the step under the playhead
 * strikes again from silence (tg_core's Instance::panic). The test below pins
 * that through the vtable the device calls.
 */
#[cfg(test)]
mod tests {
    use super::*;
    use atomic_float::AtomicF64;
    use ni_schwung::{AudioFxApiV2, HostApiV1};
    use std::ffi::CString;
    use std::sync::atomic::Ordering;
    use std::sync::{Mutex, MutexGuard};

    /* The host's clock, which every test here shares: init keeps the clock
     * process-wide, as the device does. A RUNNING transport, or the gate is
     * open and a comparison proves nothing. */
    pub(crate) static BEATS: AtomicF64 = AtomicF64::new(0.0);
    pub(crate) static BPM: AtomicF64 = AtomicF64::new(120.0);
    extern "C" fn bpm() -> f32 {
        BPM.load(Ordering::SeqCst) as f32
    }
    pub(crate) extern "C" fn beats() -> f64 {
        BEATS.load(Ordering::SeqCst)
    }

    /// THE CLOCK IS ONE PER PROCESS AND CARGO RUNS TESTS ON THREADS, so a test
    /// that renders through the vtable holds this for as long as it moves
    /// the clock. A test that failed while holding it leaves the clock as
    /// any other would, so poisoning is ignored.
    pub(crate) fn hold_the_clock() -> MutexGuard<'static, ()> {
        static CLOCK: Mutex<()> = Mutex::new(());
        CLOCK.lock().unwrap_or_else(|e| e.into_inner())
    }

    /* The vtable, from the init the device calls, with this file's clock.
     * The table is a constant, and init copies the host's two clock functions
     * and its rate and keeps nothing else (ni_schwung's keep_host) -- so the
     * host table can be a local, every test asks, and two asking at once
     * store the same clock. */
    pub(crate) fn api() -> &'static AudioFxApiV2 {
        let host = HostApiV1::with_clock(bpm, beats);
        /* SAFETY: `host` is valid for the call, which is as long as init
         * reads it; init returns its module type's table, a constant that
         * lives as long as the process. A table of the version init expects
         * is never refused. */
        unsafe { &*move_audio_fx_init_v2(&host) }
    }

    /* The block the panic arrives before: the middle of step 2, an ON step
     * of slot 2's "5555" (a step is 43 blocks at 120 bpm), past its attack,
     * so the gate is open and has something to let go of. */
    const PANIC_AT: usize = 110;

    fn render(api: &AudioFxApiV2, panic: bool) -> Vec<i16> {
        BPM.store(120.0, Ordering::SeqCst);
        BEATS.store(0.0, Ordering::SeqCst);
        let inst = (api.create_instance.unwrap())(std::ptr::null(), std::ptr::null());
        let set = |k: &str, v: &str| {
            let (k, v) = (CString::new(k).unwrap(), CString::new(v).unwrap());
            (api.set_param.unwrap())(inst, k.as_ptr(), v.as_ptr());
        };
        set("amount", "0.8");
        /* A second slot with a sound of its own, switched to mid-render, so
         * the panic lands on a gate that is recalling. */
        set("slot", "2");
        set("pattern", "5555");
        set("amount", "0.9");
        set("attack", "40");
        set("slot", "0");
        let mut out = Vec::new();
        for b in 0..200 {
            if b == 40 {
                set("slot", "2");
            }
            if panic && b == PANIC_AT {
                for cc in [120u8, 123] {
                    let msg = [0xB0u8, cc, 0];
                    move_audio_fx_on_midi(inst, msg.as_ptr(), 3, 0);
                }
                set("panic", "1");
            }
            let mut buf = [10000i16; 256];
            (api.process_block.unwrap())(inst, buf.as_mut_ptr(), 128);
            out.extend_from_slice(&buf);
            BEATS.store(beats() + 128.0 / 44100.0 * 2.0, Ordering::SeqCst);
        }
        (api.destroy_instance.unwrap())(inst);
        out
    }

    /*
     * THE KNOB GRID THROUGH A SLOT SWITCH. Every chain param but `slot` is the
     * current slot's, so turning the Slot knob recalls them all: what
     * get_param answers is the new slot's, and turning back brings the old
     * slot's back -- through the vtable the device calls.
     */
    #[test]
    fn the_vtable_reads_and_writes_the_current_slot() {
        let api = api();
        let inst = (api.create_instance.unwrap())(std::ptr::null(), std::ptr::null());
        let set = |k: &str, v: &str| {
            let (k, v) = (CString::new(k).unwrap(), CString::new(v).unwrap());
            (api.set_param.unwrap())(inst, k.as_ptr(), v.as_ptr());
        };
        let get = |k: &str| {
            let k = CString::new(k).unwrap();
            let mut buf = [0 as std::ffi::c_char; 256];
            let n = (api.get_param.unwrap())(inst, k.as_ptr(), buf.as_mut_ptr(), 256);
            assert!(n >= 0);
            unsafe { std::ffi::CStr::from_ptr(buf.as_ptr()) }.to_str().unwrap().to_owned()
        };
        /* Every per-slot key in CHAIN_PARAMS, an A and a B value. */
        let keys = [
            ("length", "7", "23"), ("rate", "1/8", "1/32"), ("legato", "1", "0"),
            ("time_mode", "1", "0"), ("curve", "2", "1"), ("attack", "12.5", "40.0"),
            ("decay", "33.0", "2.5"), ("sustain", "0.40", "0.85"), ("release", "30.0", "75.5"),
            ("hold", "0.60", "0.35"), ("amount", "0.70", "0.25"), ("fade", "0.50", "0.20"),
            ("fade_soft", "1", "0"), ("fade_dir", "1", "0"),
        ];
        for (k, a, _) in keys {
            set(k, a);
        }
        set("slot", "4");
        assert_eq!(get("slot"), "4");
        assert_eq!(get("amount"), "1.00", "a fresh slot's own value");
        for (k, _, b) in keys {
            set(k, b);
        }
        set("slot", "0");
        for (k, a, _) in keys {
            assert_eq!(get(k), a, "{k} in slot 1");
        }
        set("slot", "4");
        for (k, _, b) in keys {
            assert_eq!(get(k), b, "{k} in slot 5");
        }
        /* The ui readout's `slot` field is the editor's cue to re-read the
         * grid -- found by its name, as every reader may now. */
        let at = tg_core::params::UI_FIELDS.iter().position(|f| *f == "slot").unwrap();
        assert_eq!(get("ui").split(':').nth(at), Some("4"), "{}", get("ui"));
        (api.destroy_instance.unwrap())(inst);
    }

    /*
     * THE EDITOR'S FEED: the last declaration, read-only, answered through the
     * vtable, and naming only keys that are NOT declared themselves -- the
     * manager pushes a declared key once and never again, so a declared feed
     * would freeze the page -- each of which the module answers.
     */
    #[test]
    fn the_web_editor_feed_is_last_and_every_key_it_names_is_live() {
        let declared: Vec<serde_json::Value> = serde_json::from_str(params::CHAIN_PARAMS).unwrap();
        let keys: Vec<&str> = declared.iter().filter_map(|d| d["key"].as_str()).collect();
        let last = declared.last().unwrap();
        assert_eq!(last["key"], ni_schwung::WEB_UI_KEY);
        assert_eq!(last["access"], "read");
        let feeds: Vec<&str> = last["extra_keys"].as_array().unwrap().iter().map(|v| v.as_str().unwrap()).collect();
        assert!(!feeds.is_empty() && feeds.len() <= 4, "the host takes at most four per declaration");

        let api = api();
        let inst = (api.create_instance.unwrap())(std::ptr::null(), std::ptr::null());
        let get = |k: &str| {
            let k = CString::new(k).unwrap();
            let mut buf = vec![0 as std::ffi::c_char; 65536];
            (api.get_param.unwrap())(inst, k.as_ptr(), buf.as_mut_ptr(), buf.len() as std::ffi::c_int)
        };
        assert!(get(ni_schwung::WEB_UI_KEY) > 0);
        for f in feeds {
            assert!(!keys.contains(&f), "{f} is declared, so the page would never see it move");
            assert!(get(f) > 0, "the module does not answer {f}");
        }
        (api.destroy_instance.unwrap())(inst);
    }

    #[test]
    fn a_host_panic_shuts_the_gate_and_the_step_strikes_again() {
        let _clock = hold_the_clock();
        let api = api();
        let (with, without) = (render(api, true), render(api, false));
        assert!(without.iter().any(|&v| v < 5000), "the gate never gated -- no transport?");
        /* Up to the panic, the same; at it, shut to what Amount 0.9 leaves of
         * the input -- a tenth -- and opening again from there. */
        let at = PANIC_AT * 256;
        assert_eq!(with[..at], without[..at]);
        assert!(without[at] > 9000, "the gate is open there: {}", without[at]);
        assert!((with[at] - 1000).abs() <= 1, "shut at once: {}", with[at]);
        assert!(with[at..at + 256].windows(2).all(|w| w[1] >= w[0]), "and opening again");
        assert_ne!(with, without);
    }
}

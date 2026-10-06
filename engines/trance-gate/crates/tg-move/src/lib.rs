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
    /* No MIDI: the arrows are claimed as CCs and handled in ui_chain.js,
     * which sees them first. */
}

ni_schwung::export_audio_fx!(Gate);

/*
 * THE PANIC, ON THE GATE. A host panic arrives as CC 120/123 through
 * `move_audio_fx_on_midi`. The gate holds no note -- its gain is a function of
 * the transport and the pattern -- so it must accept the panic and change
 * nothing; this pins that rather than assuming an empty handler is harmless.
 */
#[cfg(test)]
mod tests {
    use super::*;
    use ni_schwung::{AudioFxApiV2, HostApiV1};
    use std::ffi::CString;
    use std::sync::atomic::{AtomicU64, Ordering};
    use std::sync::Once;

    /* The host's clock. A RUNNING transport, or the gate is open and the
     * comparison proves nothing. */
    static BEATS: AtomicU64 = AtomicU64::new(0);
    extern "C" fn bpm() -> f32 {
        120.0
    }
    extern "C" fn beats() -> f64 {
        f64::from_bits(BEATS.load(Ordering::SeqCst))
    }

    /* The vtable, initialised once: it is process-wide, as on the device,
     * and cargo runs these tests in threads -- two inits racing on the
     * shell's statics would be a data race. The host is leaked on purpose:
     * the shell keeps the pointer for the process's life, exactly as it keeps
     * the real host's. */
    fn api() -> &'static AudioFxApiV2 {
        static INIT: Once = Once::new();
        static mut API: *const AudioFxApiV2 = std::ptr::null();
        unsafe {
            INIT.call_once(|| {
                let host: &'static HostApiV1 = Box::leak(Box::new(HostApiV1::with_clock(bpm, beats)));
                API = move_audio_fx_init_v2(host);
            });
            &*API
        }
    }

    fn render(api: &AudioFxApiV2, panic: bool) -> Vec<i16> {
        BEATS.store(0f64.to_bits(), Ordering::SeqCst);
        let inst = (api.create_instance.unwrap())(std::ptr::null(), std::ptr::null());
        let set = |k: &str, v: &str| {
            let (k, v) = (CString::new(k).unwrap(), CString::new(v).unwrap());
            (api.set_param.unwrap())(inst, k.as_ptr(), v.as_ptr());
        };
        set("amount", "0.8");
        /* A second slot with a sound of its own, switched to mid-render: the
         * panic must change nothing about a gate that is recalling. */
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
            if panic && b == 60 {
                for cc in [120u8, 123] {
                    let msg = [0xB0u8, cc, 0];
                    move_audio_fx_on_midi(inst, msg.as_ptr(), 3, 0);
                }
                set("panic", "1");
            }
            let mut buf = [10000i16; 256];
            (api.process_block.unwrap())(inst, buf.as_mut_ptr(), 128);
            out.extend_from_slice(&buf);
            let b = beats() + 128.0 / 44100.0 * 2.0;
            BEATS.store(b.to_bits(), Ordering::SeqCst);
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
        /* The ui readout's last field is the slot: the editor's cue to re-read
         * the grid. */
        assert!(get("ui").ends_with(":4"), "{}", get("ui"));
        (api.destroy_instance.unwrap())(inst);
    }

    #[test]
    fn a_host_panic_is_accepted_and_changes_nothing() {
        let api = api();
        let with = render(api, true);
        assert!(with.iter().any(|&v| v < 5000), "the gate never gated -- no transport?");
        assert_eq!(with, render(api, false));
    }
}

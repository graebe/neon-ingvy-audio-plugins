//! The glue against a module that records what reached it.

use super::*;
use std::ffi::CString;
use std::mem::offset_of;

#[derive(Default)]
struct Probe {
    midi: Vec<u8>,
    last: Option<Transport>,
    key: String,
}

impl Module for Probe {
    const CHAIN_PARAMS: &'static str = r#"[{"key":"x"}]"#;
    fn new(sample_rate: f64) -> Self {
        assert_eq!(sample_rate, SAMPLE_RATE);
        Probe::default()
    }
    fn process_i16(&mut self, buf: &mut [i16], frames: usize, t: Option<&Transport>) {
        assert_eq!(buf.len(), frames * 2);
        self.last = t.copied();
    }
    fn set_param(&mut self, key: &str, val: &str) {
        self.key = format!("{key}={val}");
    }
    fn get_param(&self, _key: &str, out: &mut [u8]) -> c_int {
        out[0] = 0;
        -1
    }
    fn on_midi(&mut self, msg: &[u8]) {
        self.midi.extend_from_slice(msg);
    }
}

extern "C" fn bpm() -> f32 {
    96.0
}
extern "C" fn beats() -> f64 {
    3.5
}

#[test]
fn the_host_table_matches_the_c_layout() {
    /* 64-bit offsets of the two fields read, from plugin_api_v1.h. */
    if std::mem::size_of::<*const u8>() == 8 {
        assert_eq!(offset_of!(HostApiV1, get_bpm), 88);
        assert_eq!(offset_of!(HostApiV1, get_beat_position), 112);
        assert_eq!(std::mem::size_of::<HostApiV1>(), 184);
    }
}

/* ONE TEST drives the vtable: HOST and FX_API are process-wide, as on the
 * device, and cargo runs tests in threads. */
#[test]
fn the_vtable_end_to_end() {
    let get = |api: &AudioFxApiV2, inst, k: &str, n: usize| {
        let k = CString::new(k).unwrap();
        let mut buf = vec![1 as c_char; n];
        let r = (api.get_param.unwrap())(inst, k.as_ptr(), buf.as_mut_ptr(), n as c_int);
        let s: Vec<u8> = buf.iter().take_while(|&&c| c != 0).map(|&c| c as u8).collect();
        (r, String::from_utf8(s).unwrap())
    };

    /* No host: a stopped transport at 120. */
    let api = unsafe { &*init::<Probe>(std::ptr::null()) };
    assert!(api.on_midi.is_none(), "on_midi is the free symbol, never the field");
    let inst = (api.create_instance.unwrap())(std::ptr::null(), std::ptr::null());
    let mut audio = [0i16; 256];
    (api.process_block.unwrap())(inst, audio.as_mut_ptr(), 128);
    let probe = unsafe { &*(inst as *const Probe) };
    let t = probe.last.unwrap();
    assert!(!t.running && t.beats == -1.0 && t.bpm == 120.0);

    assert_eq!(get(api, inst, "chain_params", 64), (13, Probe::CHAIN_PARAMS.to_string()));
    assert_eq!(get(api, inst, "chain_params", 13).0, -1, "no room for the terminator");
    assert_eq!(get(api, inst, "ui_hierarchy", 8), (0, String::new()));
    assert_eq!(get(api, inst, "other", 8).0, -1);

    let (k, v) = (CString::new("rate").unwrap(), CString::new("1/8").unwrap());
    (api.set_param.unwrap())(inst, k.as_ptr(), v.as_ptr());
    (api.set_param.unwrap())(inst, k.as_ptr(), std::ptr::null());
    assert_eq!(unsafe { &*(inst as *const Probe) }.key, "rate=1/8");

    let msg = [0xB0u8, 123, 0];
    on_midi::<Probe>(inst, msg.as_ptr(), 3, 0);
    on_midi::<Probe>(inst, msg.as_ptr(), 9, 0);
    on_midi::<Probe>(inst, std::ptr::null(), 3, 0);
    on_midi::<Probe>(std::ptr::null_mut(), msg.as_ptr(), 3, 0);
    assert_eq!(unsafe { &*(inst as *const Probe) }.midi, msg);

    /* Null instances and buffers are refused, not dereferenced. */
    (api.process_block.unwrap())(std::ptr::null_mut(), audio.as_mut_ptr(), 128);
    (api.process_block.unwrap())(inst, std::ptr::null_mut(), 128);
    assert_eq!((api.get_param.unwrap())(inst, k.as_ptr(), std::ptr::null_mut(), 8), -1);
    (api.destroy_instance.unwrap())(inst);
    (api.destroy_instance.unwrap())(std::ptr::null_mut());

    /* A host with a running clock. */
    let host: &'static HostApiV1 = Box::leak(Box::new(HostApiV1::with_clock(bpm, beats)));
    init::<Probe>(host);
    let t = read_transport();
    assert!(t.running && t.beats == 3.5 && t.bpm == 96.0);
}

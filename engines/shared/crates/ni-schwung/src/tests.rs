// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

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
        /* Every host table in this file is at the Move's rate, or absent. */
        assert_eq!(sample_rate, DEFAULT_SAMPLE_RATE);
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

/// A second module type, for what two types in one binary must not share.
struct Other;

impl Module for Other {
    const CHAIN_PARAMS: &'static str = "[]";
    fn new(_: f64) -> Self {
        Other
    }
    fn process_i16(&mut self, _: &mut [i16], _: usize, _: Option<&Transport>) {}
    fn set_param(&mut self, _: &str, _: &str) {}
    fn get_param(&self, _: &str, _: &mut [u8]) -> c_int {
        -1
    }
}

extern "C" fn bpm() -> f32 {
    96.0
}
extern "C" fn beats() -> f64 {
    3.5
}
extern "C" fn other_bpm() -> f32 {
    150.0
}
extern "C" fn other_beats() -> f64 {
    9.0
}

/*
 * THE MIRRORS, LISTED FIELD BY FIELD AND HELD TO abi/layout.def -- the table
 * abi/layout.c holds the vendored C headers to, so this and the C agree
 * through one list rather than through two copies of its numbers.
 *
 * Each list is also EXHAUSTIVE BY CONSTRUCTION: the closure destructures the
 * struct without `..`, so a field added to a mirror and not listed here is a
 * compile error, not a field nobody checks.
 */
macro_rules! fields {
    ($s:ident { $($f:ident),* $(,)? }) => {{
        let _every_field = |v: $s| {
            let $s { $($f: _),* } = v;
        };
        vec![$((stringify!($s), stringify!($f), offset_of!($s, $f))),*]
    }};
}

/// One `NI_ABI_*(a, b, ...)` line of abi/layout.def, its arguments split.
fn layout_entries(kind: &str) -> Vec<Vec<String>> {
    let def = include_str!("../abi/layout.def");
    let head = format!("NI_ABI_{kind}(");
    def.lines()
        .map(str::trim)
        .filter_map(|l| l.strip_prefix(head.as_str()))
        .map(|rest| {
            let args = rest.strip_suffix(')').unwrap_or_else(|| panic!("{kind}({rest}: one entry per line"));
            args.split(',').map(|a| a.trim().to_string()).collect()
        })
        .collect()
}

#[test]
fn the_mirrors_are_the_layout_the_c_headers_are_held_to() {
    if std::mem::size_of::<*const u8>() != 8 {
        return; /* the table is LP64 (abi/layout.def) */
    }
    let mut ours = fields!(HostApiV1 {
        api_version, sample_rate, frames_per_block, mapped_memory, audio_out_offset,
        audio_in_offset, log, midi_send_internal, midi_send_external, get_clock_status,
        mod_emit_value, mod_clear_source, mod_host_ctx, get_bpm, midi_inject_to_move,
        slot_recv_channel, get_beat_position, reserved,
    });
    ours.extend(fields!(AudioFxApiV2 {
        api_version, create_instance, destroy_instance, process_block, set_param, get_param, on_midi,
    }));
    let table: Vec<(String, String, usize)> = layout_entries("FIELD")
        .into_iter()
        .map(|a| (a[1].clone(), a[2].clone(), a[3].parse().unwrap()))
        .collect();
    let ours: Vec<(String, String, usize)> =
        ours.into_iter().map(|(s, f, o)| (s.to_string(), f.to_string(), o)).collect();
    assert_eq!(ours, table, "the Rust mirrors and abi/layout.def disagree");

    let sizes: Vec<(String, usize)> =
        layout_entries("SIZE").into_iter().map(|a| (a[1].clone(), a[2].parse().unwrap())).collect();
    assert_eq!(
        sizes,
        [
            ("HostApiV1".to_string(), std::mem::size_of::<HostApiV1>()),
            ("AudioFxApiV2".to_string(), std::mem::size_of::<AudioFxApiV2>()),
        ]
    );

    let consts: Vec<(String, f64)> =
        layout_entries("CONST").into_iter().map(|a| (a[1].clone(), a[2].parse().unwrap())).collect();
    assert_eq!(
        consts,
        [
            ("HOST_API_VERSION".to_string(), f64::from(HOST_API_VERSION)),
            ("AUDIO_FX_API_VERSION".to_string(), f64::from(AUDIO_FX_API_VERSION)),
            ("DEFAULT_SAMPLE_RATE".to_string(), DEFAULT_SAMPLE_RATE),
        ]
    );
}

/* ONE TEST drives the vtable: the host's clock is process-wide, as on the
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
    assert_eq!(get(api, inst, WEB_UI_KEY, 8), (1, WEB_UI_VERSION.to_string()));
    assert_eq!(get(api, inst, WEB_UI_KEY, 1).0, -1, "no room for the terminator");
    assert_eq!(get(api, inst, "other", 8).0, -1);

    let (k, v) = (CString::new("rate").unwrap(), CString::new("1/8").unwrap());
    (api.set_param.unwrap())(inst, k.as_ptr(), v.as_ptr());
    (api.set_param.unwrap())(inst, k.as_ptr(), std::ptr::null());
    assert_eq!(unsafe { &*(inst as *const Probe) }.key, "rate=1/8");

    let msg = [0xB0u8, 123, 0];
    unsafe {
        on_midi::<Probe>(inst, msg.as_ptr(), 3, 0);
        on_midi::<Probe>(inst, msg.as_ptr(), 9, 0);
        on_midi::<Probe>(inst, std::ptr::null(), 3, 0);
        on_midi::<Probe>(std::ptr::null_mut(), msg.as_ptr(), 3, 0);
    }
    assert_eq!(unsafe { &*(inst as *const Probe) }.midi, msg);

    /* Null instances and buffers are refused, not dereferenced. */
    (api.process_block.unwrap())(std::ptr::null_mut(), audio.as_mut_ptr(), 128);
    (api.process_block.unwrap())(inst, std::ptr::null_mut(), 128);
    assert_eq!((api.get_param.unwrap())(inst, k.as_ptr(), std::ptr::null_mut(), 8), -1);
    (api.destroy_instance.unwrap())(inst);
    (api.destroy_instance.unwrap())(std::ptr::null_mut());

    /* ONE TABLE PER MODULE TYPE, and nothing writes it: another type's init
     * leaves the first type's table building the first type. The `static mut`
     * this replaced was one table for every type, rewritten by each init, so
     * `api` would have been building Others from here on. */
    let other = unsafe { &*init::<Other>(std::ptr::null()) };
    let o = (other.create_instance.unwrap())(std::ptr::null(), std::ptr::null());
    assert_eq!(get(other, o, "chain_params", 64), (2, Other::CHAIN_PARAMS.to_string()));
    (other.destroy_instance.unwrap())(o);
    let p = (api.create_instance.unwrap())(std::ptr::null(), std::ptr::null());
    assert_eq!(get(api, p, "chain_params", 64), (13, Probe::CHAIN_PARAMS.to_string()));
    (api.destroy_instance.unwrap())(p);

    /* A host with a running clock. */
    let host: &'static HostApiV1 = Box::leak(Box::new(HostApiV1::with_clock(bpm, beats)));
    unsafe { init::<Probe>(host) };
    let t = read_transport();
    assert!(t.running && t.beats == 3.5 && t.bpm == 96.0);

    /*
     * THE CLOCK IS COPIED AT INIT, NOT READ THROUGH THE TABLE LATER. A chain
     * frees its table when it goes; here the table is rewritten in place and
     * then freed, and the transport is the clock init saw, both times. Read
     * through a kept pointer it would be the new clock, and then freed memory.
     */
    let mut table = Box::new(HostApiV1::with_clock(bpm, beats));
    unsafe { init::<Probe>(&*table) };
    *table = HostApiV1::with_clock(other_bpm, other_beats);
    let t = read_transport();
    assert!(t.running && t.beats == 3.5 && t.bpm == 96.0, "{t:?}");
    drop(table);
    let t = read_transport();
    assert!(t.running && t.beats == 3.5 && t.bpm == 96.0, "{t:?}");

    /* The latest init wins, a host without a clock included. */
    unsafe { init::<Probe>(&HostApiV1::with_clock(other_bpm, other_beats)) };
    let t = read_transport();
    assert!(t.running && t.beats == 9.0 && t.bpm == 150.0, "{t:?}");
    unsafe { init::<Probe>(std::ptr::null()) };
    let t = read_transport();
    assert!(!t.running && t.beats == -1.0 && t.bpm == 120.0, "{t:?}");
}

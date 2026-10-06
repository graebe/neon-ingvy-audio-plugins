// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Init from several threads at once, as the Move's chain host does when one
 * module loads into a slot on the SPI callback and into a bus position on the
 * chain's worker at the same moment (chain_bus.c).
 *
 * What a test can assert without a race detector is what every thread sees:
 * a table that is whole and builds the module, every time, and a transport
 * made only of what some init stored -- this host's clock or none -- never a
 * value nobody stored. Under ThreadSanitizer the same test is also the proof
 * that nothing here is written without synchronisation; until the clock moved
 * into atomics and the table became a constant, every one of these inits
 * wrote a `static mut`.
 *
 * Its own binary, because the clock is process-wide and the unit tests assert
 * exact values of it.
 */

use ni_dsp::Transport;
use ni_schwung::{init, read_transport, HostApiV1, Module};
use std::ffi::{c_int, CString};

struct Quiet;

impl Module for Quiet {
    const CHAIN_PARAMS: &'static str = "[]";
    fn new(_: f64) -> Self {
        Quiet
    }
    fn process_i16(&mut self, _: &mut [i16], _: usize, _: Option<&Transport>) {}
    fn set_param(&mut self, _: &str, _: &str) {}
    fn get_param(&self, _: &str, _: &mut [u8]) -> c_int {
        -1
    }
}

extern "C" fn bpm() -> f32 {
    133.0
}
extern "C" fn beats() -> f64 {
    7.25
}

#[test]
fn inits_on_several_threads_at_once_agree() {
    /* Leaked, as the host's table outlives every init that reads it; carried
     * across threads as an address, which is all a raw pointer is. */
    let host = Box::leak(Box::new(HostApiV1::with_clock(bpm, beats))) as *const HostApiV1 as usize;
    let key = CString::new("chain_params").unwrap();

    std::thread::scope(|s| {
        for n in 0..4usize {
            let key = &key;
            s.spawn(move || {
                let mut buf = [0 as std::ffi::c_char; 8];
                for i in 0..5_000usize {
                    /* Half the inits bring the clock and half bring no host. */
                    let h = if (n + i) % 2 == 0 { host as *const HostApiV1 } else { std::ptr::null() };
                    let api = unsafe { &*init::<Quiet>(h) };
                    assert_eq!(api.api_version, 2);
                    assert!(api.on_midi.is_none());
                    let inst = (api.create_instance.unwrap())(std::ptr::null(), std::ptr::null());
                    let len = buf.len() as c_int;
                    assert_eq!((api.get_param.unwrap())(inst, key.as_ptr(), buf.as_mut_ptr(), len), 2);
                    (api.destroy_instance.unwrap())(inst);

                    let t = read_transport();
                    assert!(t.bpm == 120.0 || t.bpm == 133.0, "{t:?}");
                    assert!((!t.running && t.beats == -1.0) || (t.running && t.beats == 7.25), "{t:?}");
                }
            });
        }
    });
}

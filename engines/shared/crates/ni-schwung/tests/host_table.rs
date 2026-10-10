// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What init believes of the host's table: its version, and its sample rate.
 *
 * Its own binary, and one test, because both land in process-wide state --
 * the clock and the rate are the latest accepted init's, as on the device --
 * and every assertion here is about exactly which init that was.
 */

use ni_dsp::Transport;
use ni_schwung::{init, read_transport, AudioFxApiV2, HostApiV1, Module, DEFAULT_SAMPLE_RATE, HOST_API_VERSION};
use std::ffi::c_int;

/// A module that remembers the rate it was created at.
struct Rate(f64);

impl Module for Rate {
    const CHAIN_PARAMS: &'static str = "[]";
    fn new(sample_rate: f64) -> Self {
        Rate(sample_rate)
    }
    fn process_i16(&mut self, _: &mut [i16], _: usize, _: Option<&Transport>) {}
    fn set_param(&mut self, _: &str, _: &str) {}
    fn get_param(&self, _: &str, _: &mut [u8]) -> c_int {
        -1
    }
}

extern "C" fn bpm() -> f32 {
    97.0
}
extern "C" fn beats() -> f64 {
    5.5
}
extern "C" fn other_bpm() -> f32 {
    140.0
}
extern "C" fn other_beats() -> f64 {
    1.25
}

fn host(api_version: u32, sample_rate: c_int) -> HostApiV1 {
    let mut h = HostApiV1::with_clock(bpm, beats);
    h.api_version = api_version;
    h.sample_rate = sample_rate;
    h
}

/// The rate an instance created through `api` runs at.
fn created_at(api: &AudioFxApiV2) -> f64 {
    let inst = (api.create_instance.unwrap())(std::ptr::null(), std::ptr::null());
    let rate = unsafe { &*(inst as *const Rate) }.0;
    (api.destroy_instance.unwrap())(inst);
    rate
}

#[test]
fn the_host_table_is_checked_then_read() {
    /* THE VERSION. Every table the Move builds says MOVE_PLUGIN_API_VERSION;
     * any other number is a table this mirror cannot read, refused with the
     * null every loader on the device handles. */
    let accepted = unsafe { init::<Rate>(&host(HOST_API_VERSION, 48_000)) };
    assert!(!accepted.is_null());
    let api = unsafe { &*accepted };
    assert_eq!(created_at(api), 48_000.0, "the host's rate, not the Move's constant");
    assert_eq!(read_transport().bpm, 97.0);

    for version in [0, 2, 3, u32::MAX] {
        let mut wrong = host(version, 96_000);
        wrong.get_bpm = Some(other_bpm);
        wrong.get_beat_position = Some(other_beats);
        assert!(unsafe { init::<Rate>(&wrong) }.is_null(), "a version-{version} table was accepted");
        /* ...and believed in nothing: the clock and the rate are still the
         * last accepted table's. */
        let t = read_transport();
        assert!(t.bpm == 97.0 && t.beats == 5.5, "a refused version-{version} table moved the clock: {t:?}");
        assert_eq!(created_at(api), 48_000.0, "a refused version-{version} table moved the rate");
    }

    /* THE RATE, believed from 8 kHz to 384 kHz. */
    for rate in [8_000, 22_050, 44_100, 88_200, 192_000, 384_000] {
        let api = unsafe { &*init::<Rate>(&host(HOST_API_VERSION, rate)) };
        assert_eq!(created_at(api), f64::from(rate));
    }
    /* Outside that, the table says nothing about the rate: the Move's. A
     * zero is what a table nobody filled in carries. */
    for rate in [0, -1, -44_100, 7_999, 384_001, c_int::MAX, c_int::MIN] {
        let api = unsafe { &*init::<Rate>(&host(HOST_API_VERSION, rate)) };
        assert_eq!(created_at(api), DEFAULT_SAMPLE_RATE, "a table saying {rate} Hz");
    }

    /* No table at all is a host that tells us nothing: the Move's rate. */
    unsafe { init::<Rate>(&host(HOST_API_VERSION, 32_000)) };
    let api = unsafe { &*init::<Rate>(std::ptr::null()) };
    assert_eq!(created_at(api), DEFAULT_SAMPLE_RATE);
}

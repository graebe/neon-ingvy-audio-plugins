// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The audio thread allocates nothing, asserted rather than claimed: every
 * entry point the Move and the plugin call on the callback, with every kind
 * of node switching in, out and across, the isolator fading, the rate
 * changing and the panic.
 */

use fd_core::{Engine, Field, Param, RING};

use assert_no_alloc::{assert_no_alloc, violation_count, AllocDisabler};

#[global_allocator]
static ALLOCATOR: AllocDisabler = AllocDisabler;

#[test]
fn process_params_rates_and_midi_allocate_nothing() {
    let mut e = Engine::new(48_000.0);
    let mut l: Vec<f32> = (0..256).map(|i| (i as f32 * 0.1).sin() * 0.3).collect();
    let mut r = vec![-0.25f32; 256];
    let mut i16s = vec![8000i16; 256 * 2];
    let mut out = vec![0u8; 64];
    let (mut pre, mut post) = (vec![0.0f32; RING], vec![0.0f32; RING]);

    assert_no_alloc(|| {
        e.set_watching(true);
        for block in 0..400 {
            let slot = (block % 16) as u8;
            e.set_num(Param::Node(slot, Field::Type), (block % 10) as f64);
            e.set_num(Param::Node(slot, Field::Family), (block % 6) as f64);
            e.set_num(Param::Node(slot, Field::Order), (block % 8) as f64);
            e.set_num(Param::Node(slot, Field::Channel), (block % 5) as f64);
            e.set_num(Param::Node(slot, Field::Freq), 20.0 * (block as f64 + 1.0));
            e.set_num(Param::Node(slot, Field::Ripple), 0.1 + (block % 7) as f64 * 0.4);
            e.set_num(Param::IsoOn, (block / 50 % 2) as f64);
            e.set_num(Param::IsoSlope, (block / 30 % 2) as f64);
            e.set_num(Param::Scale, (block % 9) as f64 * 40.0 - 160.0);
            match block % 3 {
                0 => e.process_f32(&mut l, Some(&mut r)),
                1 => e.process_f32(&mut l, None),
                _ => e.process_i16(&mut i16s, 256),
            }
            e.set_param("n03_gain", "-6 dB");
            e.get_param("n03_gain", &mut out);
            e.on_midi(&[0x90, 60, 100]);
            if block % 97 == 0 {
                e.on_midi(&[0xB0, 120, 0]);
            }
            if block == 200 {
                e.set_sample_rate(96_000.0);
            }
        }
        e.rings(&mut pre, &mut post);
    });
    assert_eq!(violation_count(), 0);
}

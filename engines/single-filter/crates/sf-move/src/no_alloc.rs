// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NOTHING THE DEVICE CALLS ON ITS AUDIO THREAD ALLOCATES, for any product.
 *
 * Schwung calls every entry point on the Move's one thread, its audio
 * callback: process_block, set_param, get_param and on_midi. These drive all
 * four through the vtable the device calls, over many blocks of a moving
 * input: every declared knob written with a value it sends -- a float's ends
 * and middle, an enum's options by index or by label as its `wire_format`
 * says -- every declared key and the shell's readouts read, and the panic
 * (CC 120, CC 123 and the `panic` key) and a note through on_midi.
 *
 * Not measured: create_instance and destroy_instance, which are
 * construction, and the keys, values and buffers, made before the blocks --
 * the host hands them in. The guard is assert_no_alloc's: it watches the
 * thread that runs the closure, and counts rather than aborts.
 */

use super::*;
use assert_no_alloc::{assert_no_alloc, violation_count, AllocDisabler};
use serde_json::Value;
use std::ffi::{c_char, CString};

#[global_allocator]
static ALLOCATOR: AllocDisabler = AllocDisabler;

/// What the shell serves besides the declared keys (ni_schwung).
const READOUTS: [&str; 2] = ["chain_params", "ui_hierarchy"];

fn run<P: Product>() {
    /* SAFETY: init returns its module type's table, a constant that lives as
     * long as the process. */
    let api = unsafe { &*ni_schwung::init::<SingleFilter<P>>(std::ptr::null()) };
    let declared: Vec<Value> = serde_json::from_str(P::CHAIN_PARAMS).unwrap();
    let text = |s: String| CString::new(s).unwrap();
    let writes: Vec<(CString, Vec<CString>)> = declared
        .iter()
        .map(|e| {
            let values: Vec<CString> = match e["type"].as_str().unwrap() {
                "float" => {
                    let (lo, hi) = (e["min"].as_f64().unwrap(), e["max"].as_f64().unwrap());
                    [lo, (lo + hi) / 2.0, hi].into_iter().map(|v| text(v.to_string())).collect()
                }
                _ => {
                    let options = e["options"].as_array().unwrap();
                    if e["wire_format"] == "index" {
                        (0..options.len()).map(|i| text(i.to_string())).collect()
                    } else {
                        options.iter().map(|o| text(o.as_str().unwrap().to_owned())).collect()
                    }
                }
            };
            (text(e["key"].as_str().unwrap().to_owned()), values)
        })
        .collect();
    let mut reads: Vec<CString> = declared.iter().map(|e| text(e["key"].as_str().unwrap().to_owned())).collect();
    reads.extend(READOUTS.iter().map(|k| text(k.to_string())));
    let (panic, one) = (CString::new("panic").unwrap(), CString::new("1").unwrap());
    let inst = (api.create_instance.unwrap())(std::ptr::null(), std::ptr::null());
    let mut out = vec![0 as c_char; 65536];
    let mut buf = [0i16; 256];

    for block in 0..256usize {
        let level = if block % 8 == 5 { 0.0 } else { 4000.0 + (block % 7) as f64 * 4000.0 };
        let cycle = 16.0 + (block % 5) as f64 * 24.0;
        for (i, s) in buf.iter_mut().enumerate() {
            *s = (((block * 128 + i / 2) as f64 / cycle * std::f64::consts::TAU).sin() * level) as i16;
        }
        let note = 36 + (block % 48) as u8;
        assert_no_alloc(|| {
            for (k, (key, values)) in writes.iter().enumerate() {
                (api.set_param.unwrap())(inst, key.as_ptr(), values[(block + k) % values.len()].as_ptr());
            }
            let msg = if block % 2 == 0 { [0x90, note, 100] } else { [0x80, note, 0] };
            /* SAFETY: a live instance and a message of its length. */
            unsafe { ni_schwung::on_midi::<SingleFilter<P>>(inst, msg.as_ptr(), 3, 0) };
            if block % 16 == 15 {
                for cc in [120u8, 123] {
                    unsafe { ni_schwung::on_midi::<SingleFilter<P>>(inst, [0xB0, cc, 0].as_ptr(), 3, 0) };
                }
                (api.set_param.unwrap())(inst, panic.as_ptr(), one.as_ptr());
            }
            (api.process_block.unwrap())(inst, buf.as_mut_ptr(), 128);
            for key in &reads {
                (api.get_param.unwrap())(inst, key.as_ptr(), out.as_mut_ptr(), out.len() as c_int);
            }
        });
    }
    let n = violation_count();
    assert_eq!(n, 0, "{:?}: the audio thread allocated or freed {n} times", P::SHAPE);
    for key in &reads {
        assert!((api.get_param.unwrap())(inst, key.as_ptr(), out.as_mut_ptr(), out.len() as c_int) >= 0, "{key:?} is not served");
    }
    (api.destroy_instance.unwrap())(inst);
}

#[test]
fn every_call_on_the_audio_thread_allocates_nothing() {
    run::<HighPass>();
    run::<LowPass>();
    run::<BandPass>();
    run::<Notch>();
    run::<Bell>();
    run::<LowShelf>();
    run::<HighShelf>();
    run::<Tilt>();
    run::<AllPass>();
}

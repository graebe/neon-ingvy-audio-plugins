// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NOTHING THE DEVICE CALLS ON ITS AUDIO THREAD ALLOCATES.
 *
 * Schwung calls every entry point on the Move's one thread, its audio
 * callback: process_block, set_param, get_param and on_midi. These drive all
 * four through the vtable the device calls, over many blocks of a moving input
 * and a running transport: every declared knob written with a value it sends
 * -- a float's ends and middle, an enum's options by index or by label as its
 * `wire_format` says -- every declared key, every feed a declaration names and
 * the engine's other readouts read, and the panic (CC 120, CC 123 and the
 * `panic` key) and a note through on_midi. A patch load, set_param("state"),
 * is a test of its own: it is the one write that reads a whole text.
 *
 * Not measured: create_instance and destroy_instance, which are construction,
 * and the keys, values and buffers, made before the blocks -- the host hands
 * them in.
 *
 * The guard is assert_no_alloc's: it watches the thread that runs the
 * closure, and counts rather than aborts, so the crate's other tests, which
 * run outside it, are not watched.
 */

use super::*;
use assert_no_alloc::{assert_no_alloc, violation_count, AllocDisabler};
use ni_schwung::{AudioFxApiV2, HostApiV1};
use serde_json::Value;
use std::ffi::{c_char, c_void, CString};
use std::sync::atomic::{AtomicU32, Ordering};

#[global_allocator]
static ALLOCATOR: AllocDisabler = AllocDisabler;

/// What the engine serves besides the declared keys and the feeds they name
/// (sc_core's Instance::get_param), and the shell's own (ni_schwung).
const READOUTS: [&str; 12] = [
    "state", "ms_per_cycle", "fires", "duck", "key_level", "advancing", "dropped", "rate_label",
    "curve_label", "source_label", "chain_params", "ui_hierarchy",
];

/* The host's clock: a running transport at 120 bpm whose beat moves a block on
 * at every read, and process_block reads it once a block. */
static READS: AtomicU32 = AtomicU32::new(0);
extern "C" fn bpm() -> f32 {
    120.0
}
extern "C" fn beats() -> f64 {
    f64::from(READS.fetch_add(1, Ordering::Relaxed)) * 128.0 / 44100.0 * 2.0
}

/* The vtable, from the init the device calls, with that clock. The clock is
 * process-wide (ni_schwung's keep_host) and this crate's other tests init with
 * none, on threads of their own, so each block inits again before it runs --
 * outside the measured closure: init is the loader's, not the callback's. A
 * block one of theirs leaves stopped measures the stopped transport instead,
 * which is the Move's too. */
fn init() -> &'static AudioFxApiV2 {
    let host = HostApiV1::with_clock(bpm, beats);
    /* SAFETY: `host` is valid for the call, which is as long as init reads
     * it; init returns its module type's table, a constant that lives as long
     * as the process. */
    unsafe { &*move_audio_fx_init_v2(&host) }
}

fn declared() -> Vec<Value> {
    serde_json::from_str(params::CHAIN_PARAMS).unwrap()
}

/// Each writable knob and the values the knob grid sends for it.
fn knob_writes() -> Vec<(CString, Vec<CString>)> {
    let text = |s: String| CString::new(s).unwrap();
    declared()
        .iter()
        .filter(|e| e["access"] != "read" && e["type"] != "canvas")
        .map(|e| {
            let values: Vec<CString> = match e["type"].as_str().unwrap() {
                "float" => {
                    let (lo, hi) = (e["min"].as_f64().unwrap(), e["max"].as_f64().unwrap());
                    [lo, (lo + hi) / 2.0, hi].into_iter().map(|v| text(v.to_string())).collect()
                }
                "enum" => {
                    let options = e["options"].as_array().unwrap();
                    if e["wire_format"] == "index" {
                        (0..options.len()).map(|i| text(i.to_string())).collect()
                    } else {
                        options.iter().map(|o| text(o.as_str().unwrap().to_owned())).collect()
                    }
                }
                t => panic!("{}: a type the knob grid writes that this test does not know: {t}", e["key"]),
            };
            (text(e["key"].as_str().unwrap().to_owned()), values)
        })
        .collect()
}

/// Every key the device and the editor read: each declared key but a
/// canvas's, each feed a declaration names, and the readouts above.
fn reads() -> Vec<CString> {
    let mut keys: Vec<String> = Vec::new();
    for e in declared() {
        if e["type"] != "canvas" {
            keys.push(e["key"].as_str().unwrap().to_owned());
        }
        for f in e["extra_keys"].as_array().into_iter().flatten() {
            keys.push(f.as_str().unwrap().to_owned());
        }
    }
    keys.extend(READOUTS.iter().map(|k| k.to_string()));
    keys.sort();
    keys.dedup();
    keys.into_iter().map(|k| CString::new(k).unwrap()).collect()
}

fn get(api: &AudioFxApiV2, inst: *mut c_void, key: &CString, out: &mut [c_char]) -> c_int {
    (api.get_param.unwrap())(inst, key.as_ptr(), out.as_mut_ptr(), out.len() as c_int)
}

/// One block of a sine whose level and pitch move from block to block, with
/// every eighth block silent.
fn input(block: usize, buf: &mut [i16; 256]) {
    let level = if block % 8 == 5 { 0.0 } else { 4000.0 + (block % 7) as f64 * 4000.0 };
    let cycle = 16.0 + (block % 5) as f64 * 24.0;
    for (i, frame) in buf.as_chunks_mut::<2>().0.iter_mut().enumerate() {
        let v = ((block * 128 + i) as f64 / cycle * std::f64::consts::TAU).sin() * level;
        *frame = [v as i16; 2];
    }
}

#[test]
fn every_call_on_the_audio_thread_allocates_nothing() {
    let api = init();
    let inst = (api.create_instance.unwrap())(std::ptr::null(), std::ptr::null());
    let (writes, reads) = (knob_writes(), reads());
    let (panic, one) = (CString::new("panic").unwrap(), CString::new("1").unwrap());
    let mut out = vec![0 as c_char; 65536];
    let mut buf = [0i16; 256];

    for block in 0..512usize {
        init();
        input(block, &mut buf);
        let note = 36 + (block % 48) as u8;
        assert_no_alloc(|| {
            /* Each knob a different one of its values every block, so over
             * the run every value is sent. */
            for (k, (key, values)) in writes.iter().enumerate() {
                (api.set_param.unwrap())(inst, key.as_ptr(), values[(block + k) % values.len()].as_ptr());
            }
            let msg = if block % 2 == 0 { [0x90, note, 100] } else { [0x80, note, 0] };
            move_audio_fx_on_midi(inst, msg.as_ptr(), 3, 0);
            if block % 16 == 15 {
                for cc in [120u8, 123] {
                    move_audio_fx_on_midi(inst, [0xB0, cc, 0].as_ptr(), 3, 0);
                }
                (api.set_param.unwrap())(inst, panic.as_ptr(), one.as_ptr());
            }
            (api.process_block.unwrap())(inst, buf.as_mut_ptr(), 128);
            for key in &reads {
                get(api, inst, key, &mut out);
            }
        });
    }

    let n = violation_count();
    assert_eq!(n, 0, "the audio thread allocated or freed {n} times");
    /* A key the engine does not serve would have proved nothing. */
    for key in &reads {
        assert!(get(api, inst, key, &mut out) >= 0, "{key:?} is not served");
    }
    (api.destroy_instance.unwrap())(inst);
}

/*
 * A PATCH LOAD ON THE AUDIO THREAD. Schwung restores a module's `state` through
 * set_param, on its callback like every other write, and what it restores is
 * a blob it stored or one a hand-edit or a cut-short file damaged. So each
 * blob here is one this module saved after a different patch, and the same
 * blob cut short, and a text that is no blob at all.
 */
#[test]
fn a_patch_load_on_the_audio_thread_allocates_nothing() {
    let api = init();
    let state = CString::new("state").unwrap();
    let writes = knob_writes();
    let mut out = vec![0 as c_char; 65536];

    let source = (api.create_instance.unwrap())(std::ptr::null(), std::ptr::null());
    let mut blobs = Vec::new();
    for patch in 0..8usize {
        for (k, (key, values)) in writes.iter().enumerate() {
            (api.set_param.unwrap())(source, key.as_ptr(), values[(patch * 3 + k) % values.len()].as_ptr());
        }
        let n = get(api, source, &state, &mut out);
        assert!(n > 0, "state is not served");
        let blob: Vec<u8> = out[..n as usize].iter().map(|c| *c as u8).collect();
        blobs.push(CString::new(&blob[..blob.len() / 2]).unwrap());
        blobs.push(CString::new(blob).unwrap());
    }
    blobs.push(CString::new("not a patch").unwrap());
    (api.destroy_instance.unwrap())(source);

    let inst = (api.create_instance.unwrap())(std::ptr::null(), std::ptr::null());
    let mut buf = [0i16; 256];
    for (block, blob) in blobs.iter().cycle().take(blobs.len() * 4).enumerate() {
        init();
        input(block, &mut buf);
        assert_no_alloc(|| {
            (api.set_param.unwrap())(inst, state.as_ptr(), blob.as_ptr());
            (api.process_block.unwrap())(inst, buf.as_mut_ptr(), 128);
        });
    }

    let n = violation_count();
    assert_eq!(n, 0, "a patch load allocated or freed {n} times");
    /* And the blobs were patches the engine took, not texts it refused. */
    let whole = &blobs[blobs.len() - 2];
    (api.set_param.unwrap())(inst, state.as_ptr(), whole.as_ptr());
    let n = get(api, inst, &state, &mut out);
    assert_eq!(whole.as_bytes(), out[..n as usize].iter().map(|c| *c as u8).collect::<Vec<u8>>());
    (api.destroy_instance.unwrap())(inst);
}

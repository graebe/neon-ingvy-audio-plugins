/*!
The `sc_core_*` C ABI, called the way a shell calls it.

`engines/side-chain/tests/test_core.c` makes these claims from C against the
hand-written header, which is what keeps the header honest -- but it links a
release staticlib, so cargo's coverage cannot see this crate run. These make
the same claims from Rust, through the same raw pointers: every entry point
survives a null instance or a garbage argument, the two parameter doors agree,
the readouts have the shape the header documents, and the engine ducks through
each of the three process paths.

The DSP itself is `sc-core`'s to test. What is asserted here is the boundary.
*/

use super::*;
use sc_core::params::{Param, PARAM_COUNT};
use std::ffi::CString;
use std::ptr::{null, null_mut};

const SR: f64 = 48_000.0;
const NOTE_ON: [u8; 3] = [0x90, 36, 127];
const ALL_NOTES_OFF: [u8; 3] = [0xB0, 123, 0];

fn running(bpm: f32) -> ScTransport {
    ScTransport { running: 1, beats: 0.0, bpm }
}

/// `sc_core_get_param` as a Rust string, or None for -1.
unsafe fn get(c: *const ScCore, key: &str) -> Option<String> {
    let k = CString::new(key).unwrap();
    let mut buf = vec![0x7fu8; 4096]; /* SC_STATE_MAX */
    let n = sc_core_get_param(c, k.as_ptr(), buf.as_mut_ptr() as *mut c_char, buf.len() as c_int);
    if n < 0 {
        return None;
    }
    assert_eq!(buf[n as usize], 0, "`{key}` is not terminated at its length");
    Some(String::from_utf8(buf[..n as usize].to_vec()).unwrap())
}

unsafe fn set(c: *mut ScCore, key: &str, val: &str) -> c_int {
    let (k, v) = (CString::new(key).unwrap(), CString::new(val).unwrap());
    sc_core_set_param(c, k.as_ptr(), v.as_ptr())
}

unsafe fn num(c: *const ScCore, p: Param) -> f64 {
    sc_core_get_num(c, p as c_int)
}

unsafe fn rate_label(i: c_int) -> Option<String> {
    let mut buf = [0 as c_char; 32];
    let n = sc_core_rate_label(i, buf.as_mut_ptr(), buf.len() as c_int);
    (n >= 0).then(|| ni_dsp::ffi::cstr(buf.as_ptr()).to_string())
}

/// An instance set up to duck hard and fast on `source`, so a trigger is
/// unmistakable in the audio.
unsafe fn ducker(source: &str) -> *mut ScCore {
    let c = sc_core_create(SR);
    assert_eq!(set(c, "source", source), 1);
    assert_eq!(set(c, "rate", "1/4"), 1);
    sc_core_set_num(c, Param::Depth as c_int, 1.0);
    sc_core_set_num(c, Param::Attack as c_int, 0.0);
    sc_core_set_num(c, Param::Hold as c_int, 40.0);
    c
}

/* ------------------------------------------------------------ the boundary */

#[test]
fn create_reports_its_rate_and_takes_a_new_one() {
    unsafe {
        let c = sc_core_create(SR);
        assert!(!c.is_null());
        assert_eq!(sc_core_get_sample_rate(c), SR);
        sc_core_set_sample_rate(c, 44_100.0);
        assert_eq!(sc_core_get_sample_rate(c), 44_100.0);
        sc_core_destroy(c);
    }
}

#[test]
fn every_entry_point_survives_a_null_instance() {
    /* A host that failed to allocate, or one calling in mid-teardown, hands
     * over exactly this -- and a crash there takes the session with it. */
    let (mut l, mut r) = (1.0f32, 1.0f32);
    let mut lr = [1.0f32; 2];
    let mut i16s = [1000i16; 2];
    let mut buf = [0x7f as c_char; 16];
    let t = running(120.0);
    let key = CString::new("depth").unwrap();
    unsafe {
        let c = null_mut::<ScCore>();
        sc_core_destroy(c);
        sc_core_set_sample_rate(c, SR);
        sc_core_reset(c);
        sc_core_push_key_f32(c, &l, &r, 1);
        sc_core_set_key_connected(c, 1);
        sc_core_on_midi(c, NOTE_ON.as_ptr(), 3, 0);
        sc_core_process_f32_split(c, &mut l, &mut r, 1, &t);
        sc_core_process_f32_split_tap(c, &mut l, &mut r, null_mut(), null_mut(), 1, &t);
        sc_core_process_f32(c, lr.as_mut_ptr(), 1, &t);
        sc_core_process_i16(c, i16s.as_mut_ptr(), 1, &t);
        sc_core_set_num(c, Param::Depth as c_int, 1.0);
        assert_eq!(sc_core_get_num(c, Param::Depth as c_int), 0.0);
        assert_eq!(sc_core_set_param(c, key.as_ptr(), key.as_ptr()), 0);
        assert_eq!(sc_core_get_param(c, key.as_ptr(), buf.as_mut_ptr(), 16), -1);
        assert_eq!(sc_core_get_sample_rate(c), 0.0);
        assert_eq!(sc_core_phase01(c), 0.0);
        /* 1, not 0: with no instance the sweep is parked at its right edge,
         * which is what "nothing has fired" means. */
        assert_eq!(sc_core_sweep01(c), 1.0);
        assert_eq!(sc_core_duck(c), 0.0);
        assert_eq!(sc_core_fires(c), 0);
    }
    assert_eq!((l, r, lr, i16s), (1.0, 1.0, [1.0; 2], [1000; 2]), "a null instance touched the audio");
    assert_eq!(buf, [0x7f as c_char; 16], "a null instance wrote the buffer");
}

#[test]
fn garbage_arguments_to_a_live_instance_do_nothing() {
    unsafe {
        let c = sc_core_create(SR);
        let params = get(c, "params");

        /* An out-of-range parameter index is ignored, not indexed. */
        sc_core_set_num(c, PARAM_COUNT, 1.0);
        sc_core_set_num(c, -1, 1.0);
        assert_eq!(get(c, "params"), params, "an unknown index moved a parameter");
        assert_eq!(sc_core_get_num(c, PARAM_COUNT), 0.0);

        /* Unknown keys are told apart from known ones, which is how a shell
         * knows to answer its own. */
        assert_eq!(set(c, "wobble", "1"), 0);
        assert_eq!(set(c, "depth", "0.5"), 1);
        assert_eq!(num(c, Param::Depth), 0.5);
        assert_eq!(get(c, "wobble"), None);

        let ui = CString::new("ui").unwrap();
        let mut buf = [0x7f as c_char; 8];
        assert_eq!(sc_core_get_param(c, null(), buf.as_mut_ptr(), 8), -1, "a NULL key");
        assert_eq!(sc_core_get_param(c, ui.as_ptr(), null_mut(), 8), -1, "a NULL buffer");
        assert_eq!(sc_core_get_param(c, ui.as_ptr(), buf.as_mut_ptr(), 0), -1, "a zero buffer");
        assert_eq!(buf, [0x7f as c_char; 8]);

        /* Degenerate audio buffers are refused without being read. */
        let mut x = [1.0f32; 4];
        let mut s16 = [1000i16; 8];
        let t = running(120.0);
        sc_core_process_f32_split(c, null_mut(), x.as_mut_ptr(), 4, &t);
        sc_core_process_f32_split(c, x.as_mut_ptr(), null_mut(), 4, &t);
        sc_core_process_f32_split(c, x.as_mut_ptr(), x.as_mut_ptr(), 0, &t);
        sc_core_process_f32_split_tap(c, null_mut(), x.as_mut_ptr(), null_mut(), null_mut(), 4, &t);
        sc_core_process_f32_split_tap(c, x.as_mut_ptr(), x.as_mut_ptr(), null_mut(), null_mut(), -1, &t);
        sc_core_process_f32(c, null_mut(), 4, &t);
        sc_core_process_f32(c, x.as_mut_ptr(), 0, &t);
        sc_core_process_i16(c, null_mut(), 4, &t);
        sc_core_process_i16(c, s16.as_mut_ptr(), -2, &t);
        assert_eq!(x, [1.0; 4]);
        assert_eq!(s16, [1000; 8]);
        assert_eq!(sc_core_fires(c), 0, "a refused block ran the engine");
        sc_core_destroy(c);
    }
}

#[test]
fn a_malformed_midi_message_is_dropped_and_a_late_one_clamped() {
    unsafe {
        let c = ducker("MIDI");
        let mut l = [1.0f32; 256];
        let mut r = [1.0f32; 256];
        sc_core_on_midi(c, null(), 3, 0);
        sc_core_on_midi(c, NOTE_ON.as_ptr(), 0, 0);
        sc_core_on_midi(c, NOTE_ON.as_ptr(), 9, 0); /* longer than any real message */
        sc_core_process_f32_split(c, l.as_mut_ptr(), r.as_mut_ptr(), 256, null());
        assert_eq!(sc_core_fires(c), 0, "a malformed message triggered");

        /* A negative offset is the start of the block, and one past its end
         * is clamped into it rather than lost. */
        sc_core_on_midi(c, NOTE_ON.as_ptr(), 3, -5);
        sc_core_process_f32_split(c, l.as_mut_ptr(), r.as_mut_ptr(), 256, null());
        assert_eq!(sc_core_fires(c), 1);
        assert!(l[0] < 1.0, "a negative offset is sample 0");
        sc_core_destroy(c);
    }
}

/* ----------------------------------------------------- the parameter doors */

#[test]
fn the_string_door_clamps_exactly_as_the_number_door_does() {
    unsafe {
        let c = sc_core_create(SR);
        set(c, "depth", "9.5");
        assert_eq!(num(c, Param::Depth), 1.0);
        set(c, "attack", "99999");
        assert_eq!(num(c, Param::Attack), 200.0);
        set(c, "delay", "99999");
        assert_eq!(num(c, Param::Delay), 100.0);
        /* Symmetric: an early sidechain is a whole cycle the other way. */
        set(c, "delay", "-99999");
        assert_eq!(num(c, Param::Delay), -100.0);

        sc_core_set_num(c, Param::Depth as c_int, -3.0);
        assert_eq!(num(c, Param::Depth), 0.0);
        /* A NaN is dropped: clamping one propagates it, and a NaN gain
         * silences a track permanently. */
        sc_core_set_num(c, Param::Depth as c_int, 0.25);
        sc_core_set_num(c, Param::Depth as c_int, f64::NAN);
        assert_eq!(num(c, Param::Depth), 0.25);

        /* atof leniency: a state blob from an older build may carry a unit. */
        set(c, "lockout", "12ms");
        assert_eq!(num(c, Param::Lockout), 12.0);
        set(c, "lockout", "nonsense");
        assert_eq!(num(c, Param::Lockout), 0.0);
        sc_core_destroy(c);
    }
}

#[test]
fn the_string_door_takes_labels_and_the_readouts_give_them_back() {
    unsafe {
        let c = sc_core_create(SR);
        set(c, "rate", "1/8");
        assert_eq!(get(c, "rate_label").as_deref(), Some("1/8"));
        set(c, "curve", "S-Curve");
        assert_eq!(get(c, "curve_label").as_deref(), Some("S-Curve"));
        set(c, "source", "Sidechain");
        assert_eq!(get(c, "source_label").as_deref(), Some("Sidechain"));
        assert_eq!(num(c, Param::Source), 2.0);
        assert_eq!(get(c, "source").as_deref(), Some("2"), "a param key reads its number");
        sc_core_destroy(c);
    }
}

#[test]
fn the_rate_table_is_the_engines_own() {
    unsafe {
        let c = sc_core_create(SR);
        let mut count = 0;
        while let Some(label) = rate_label(count) {
            set(c, "rate", &count.to_string());
            assert_eq!(get(c, "rate_label"), Some(label), "index {count} names another rate");
            count += 1;
        }
        assert_eq!(count, 12);
        assert_eq!(rate_label(-1), None, "a negative index is past the table");
        assert_eq!(rate_label(sc_core_rate_default()).as_deref(), Some("1/4"));

        /* A buffer too small is refused whole: a truncated label is a
         * different label. */
        let mut tiny = [0x7f as c_char; 3];
        assert_eq!(sc_core_rate_label(0, tiny.as_mut_ptr(), 3), -1);
        assert_eq!(tiny, [0x7f as c_char; 3]);
        sc_core_destroy(c);
    }
}

/* ---------------------------------------------------------- the readouts */

#[test]
fn the_readouts_have_the_documented_shape() {
    unsafe {
        let c = sc_core_create(SR);
        let fields = |s: &str| s.split(':').count();
        assert_eq!(fields(&get(c, "ui").unwrap()), 11, "App.jsx parses `ui` by position");
        assert_eq!(fields(&get(c, "params").unwrap()), PARAM_COUNT as usize);
        assert_eq!(fields(&get(c, "stage_ms").unwrap()), 4);

        /* The `connected` field is the shell's word, not the engine's guess. */
        let connected = |c| get(c, "ui").unwrap().split(':').nth(8).unwrap().to_string();
        assert_eq!(connected(c), "0");
        sc_core_set_key_connected(c, 1);
        assert_eq!(connected(c), "1");
        sc_core_set_key_connected(c, 0);
        assert_eq!(connected(c), "0");

        /* Truncation terminates, reports only what it wrote, and is not an
         * error -- the header's contract, and NOT snprintf's. */
        let key = CString::new("params").unwrap();
        let mut tiny = [0x7fu8; 8];
        let n = sc_core_get_param(c, key.as_ptr(), tiny.as_mut_ptr() as *mut c_char, 8);
        assert!(n > 0 && n <= 7, "wrote {n}");
        assert_eq!(tiny[n as usize], 0);
        sc_core_destroy(c);
    }
}

/* ------------------------------------------------------------- it ducks */

#[test]
fn cycle_ducks_on_a_running_transport() {
    unsafe {
        let c = ducker("Cycle");
        let mut l = vec![1.0f32; 4800];
        let mut r = vec![1.0f32; 4800];
        sc_core_process_f32_split(c, l.as_mut_ptr(), r.as_mut_ptr(), 4800, &running(120.0));
        assert!(sc_core_fires(c) >= 1, "the transport triggers it");
        assert!(l[100] < 1.0, "and the audio is attenuated");
        assert_eq!(l, r, "both channels equally");
        assert!(sc_core_duck(c) > 0.0 && sc_core_duck(c) <= 1.0);
        let (ph, sw) = (sc_core_phase01(c), sc_core_sweep01(c));
        assert!((0.0..1.0).contains(&ph) && ph > 0.0, "the playhead moved: {ph}");
        assert!((0.0..=1.0).contains(&sw));
        sc_core_destroy(c);
    }
}

#[test]
fn midi_honours_the_offset_and_a_panic_opens_the_gate() {
    unsafe {
        let c = ducker("MIDI");
        let mut l = vec![1.0f32; 4800];
        let mut r = vec![1.0f32; 4800];
        sc_core_on_midi(c, NOTE_ON.as_ptr(), 3, 512);
        sc_core_process_f32_split(c, l.as_mut_ptr(), r.as_mut_ptr(), 4800, null());
        assert_eq!(sc_core_fires(c), 1, "works with no transport");
        assert_eq!(l[511], 1.0, "untouched before the offset");
        assert!(l[512] < 1.0, "ducked at the offset");

        /* CC 123 is the only panic a Schwung module can receive. */
        sc_core_on_midi(c, ALL_NOTES_OFF.as_ptr(), 3, 0);
        let (mut l, mut r) = ([1.0f32; 64], [1.0f32; 64]);
        sc_core_process_f32_split(c, l.as_mut_ptr(), r.as_mut_ptr(), 64, null());
        assert_eq!(l[0], 1.0, "CC 123 opens the gate");

        /* And sc_core_reset is the same panic from a host. */
        sc_core_on_midi(c, NOTE_ON.as_ptr(), 3, 0);
        sc_core_process_f32_split(c, l.as_mut_ptr(), r.as_mut_ptr(), 64, null());
        assert!(sc_core_duck(c) > 0.0);
        sc_core_reset(c);
        assert_eq!(sc_core_duck(c), 0.0, "reset opens the gate now");
        let (mut l, mut r) = ([1.0f32; 64], [1.0f32; 64]);
        sc_core_process_f32_split(c, l.as_mut_ptr(), r.as_mut_ptr(), 64, null());
        assert_eq!(l[0], 1.0, "and forgets the trigger");
        sc_core_destroy(c);
    }
}

#[test]
fn a_key_triggers_the_sidechain_and_is_cleared_per_block() {
    unsafe {
        let c = ducker("Sidechain");
        sc_core_set_num(c, Param::Threshold as c_int, -24.0);
        sc_core_set_key_connected(c, 1);
        let key: Vec<f32> = (0..4800)
            .map(|i| if i < 480 { 0.9 * (1.0 - i as f32 / 480.0).powi(2) } else { 0.0 })
            .collect();

        /* Degenerate pushes are no key at all. */
        let mut l = vec![1.0f32; 4800];
        let mut r = vec![1.0f32; 4800];
        sc_core_push_key_f32(c, null(), key.as_ptr(), 4800);
        sc_core_push_key_f32(c, key.as_ptr(), null(), 4800);
        sc_core_push_key_f32(c, key.as_ptr(), key.as_ptr(), 0);
        sc_core_push_key_f32(c, key.as_ptr(), key.as_ptr(), -1);
        sc_core_process_f32_split(c, l.as_mut_ptr(), r.as_mut_ptr(), 4800, null());
        assert_eq!(sc_core_fires(c), 0);

        sc_core_push_key_f32(c, key.as_ptr(), key.as_ptr(), 4800);
        sc_core_process_f32_split(c, l.as_mut_ptr(), r.as_mut_ptr(), 4800, null());
        assert_eq!(sc_core_fires(c), 1, "the key triggers it");
        assert!(l[200] < 1.0, "and the audio is attenuated");

        /* A shell that forgets to push gets silence, not the last transient. */
        l.fill(1.0);
        r.fill(1.0);
        sc_core_process_f32_split(c, l.as_mut_ptr(), r.as_mut_ptr(), 4800, null());
        assert_eq!(sc_core_fires(c), 1);
        sc_core_destroy(c);
    }
}

#[test]
fn a_key_longer_than_the_buffer_is_cut_at_sc_max_block() {
    unsafe {
        let c = ducker("Sidechain");
        sc_core_set_num(c, Param::Threshold as c_int, -24.0);
        sc_core_set_key_connected(c, 1);
        /* The only transient sits past SC_MAX_BLOCK: the header says those
         * frames get silence, so it must not trigger. */
        let n = MAX_BLOCK + 2048;
        let mut key = vec![0.0f32; n];
        key[MAX_BLOCK + 100..MAX_BLOCK + 580].fill(0.9);
        sc_core_push_key_f32(c, key.as_ptr(), key.as_ptr(), n as c_int);
        let mut l = vec![1.0f32; n];
        let mut r = vec![1.0f32; n];
        sc_core_process_f32_split(c, l.as_mut_ptr(), r.as_mut_ptr(), n as c_int, null());
        assert_eq!(sc_core_fires(c), 0);

        /* The control: the same transient INSIDE the buffer does trigger, so
         * the silence above was the cut and not a key too quiet to fire. */
        key.fill(0.0);
        key[MAX_BLOCK - 1000..MAX_BLOCK - 520].fill(0.9);
        sc_core_push_key_f32(c, key.as_ptr(), key.as_ptr(), n as c_int);
        sc_core_process_f32_split(c, l.as_mut_ptr(), r.as_mut_ptr(), n as c_int, null());
        assert_eq!(sc_core_fires(c), 1);
        sc_core_destroy(c);
    }
}

#[test]
fn the_three_buffer_formats_apply_one_gain_law() {
    unsafe {
        let t = running(120.0);
        const N: usize = 2400;

        let split = ducker("Cycle");
        let mut l = vec![0.5f32; N];
        let mut r = vec![0.5f32; N];
        sc_core_process_f32_split(split, l.as_mut_ptr(), r.as_mut_ptr(), N as c_int, &t);

        let inter = ducker("Cycle");
        let mut lr = vec![0.5f32; N * 2];
        sc_core_process_f32(inter, lr.as_mut_ptr(), N as c_int, &t);

        let int16 = ducker("Cycle");
        let mut s16 = vec![16_384i16; N * 2];
        sc_core_process_i16(int16, s16.as_mut_ptr(), N as c_int, &t);

        for i in 0..N {
            assert_eq!(lr[i * 2], l[i], "interleaved left differs at {i}");
            assert_eq!(lr[i * 2 + 1], r[i], "interleaved right differs at {i}");
            let want = l[i] * 32_768.0;
            assert!((s16[i * 2] as f32 - want).abs() <= 1.0, "i16 differs at {i}");
        }
        assert!(l.iter().any(|&s| s < 0.5), "nothing ducked, so nothing was compared");
        for c in [split, inter, int16] {
            sc_core_destroy(c);
        }
    }
}

#[test]
fn the_tap_reports_the_gain_it_applied_and_where_each_sample_sat() {
    unsafe {
        let t = running(120.0);
        const N: usize = 2400;
        let tapped = ducker("Cycle");
        let mut l = vec![0.5f32; N];
        let mut r = vec![0.5f32; N];
        let mut gain = vec![-1.0f32; N];
        let mut sweep = vec![-1.0f32; N];
        sc_core_process_f32_split_tap(
            tapped, l.as_mut_ptr(), r.as_mut_ptr(), gain.as_mut_ptr(), sweep.as_mut_ptr(), N as c_int, &t,
        );
        for i in 0..N {
            assert!((0.0..=1.0).contains(&gain[i]), "gain {} at {i}", gain[i]);
            assert!((l[i] - 0.5 * gain[i]).abs() < 1e-6, "the tap is not what was applied, at {i}");
            assert!((0.0..=1.0).contains(&sweep[i]), "sweep {} at {i}", sweep[i]);
        }
        assert!(gain.iter().any(|&g| g < 1.0));

        /* Either tap may be NULL; both NULL is the plain split path. */
        let plain = ducker("Cycle");
        let mut pl = vec![0.5f32; N];
        let mut pr = vec![0.5f32; N];
        sc_core_process_f32_split_tap(plain, pl.as_mut_ptr(), pr.as_mut_ptr(), null_mut(), null_mut(), N as c_int, &t);
        assert_eq!(pl, l);
        let gain_only = ducker("Cycle");
        let mut gl = vec![0.5f32; N];
        let mut gr = vec![0.5f32; N];
        let mut g2 = vec![-1.0f32; N];
        sc_core_process_f32_split_tap(gain_only, gl.as_mut_ptr(), gr.as_mut_ptr(), g2.as_mut_ptr(), null_mut(), N as c_int, &t);
        assert_eq!(g2, gain);
        for c in [tapped, plain, gain_only] {
            sc_core_destroy(c);
        }
    }
}

/* ------------------------------------------------------------ test hooks */

#[test]
fn the_curve_hooks_are_the_engines_shapes() {
    for curve in 0..3 {
        assert_eq!(sc_test_shape(curve, 0.0), 0.0, "curve {curve} starts at 0");
        assert_eq!(sc_test_shape(curve, 1.0), 1.0, "curve {curve} ends at 1");
        let t = 0.37;
        assert!((sc_test_shape_inv(curve, sc_test_shape(curve, t)) - t).abs() < 1e-9);
    }
    /* An index past the table is the Linear fallback, not a read past it. */
    assert_eq!(sc_test_shape(99, 0.37), sc_test_shape(0, 0.37));
}

#[test]
fn inner_is_the_same_instance_the_abi_drives() {
    unsafe {
        let c = sc_core_create(SR);
        (*c).inner().set_num(Param::Depth, 0.125);
        assert_eq!(num(c, Param::Depth), 0.125);
        sc_core_destroy(c);
    }
}

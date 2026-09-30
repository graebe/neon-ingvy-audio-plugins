/*!
Whole-engine tests: the real `Instance`, driven through a transport and its two
parameter doors.

The C suites (`tests/test_core.c`, `tests/test_gate.c`) and the render hashes
pin the sound; these pin the properties a caller of the RUST API relies on,
which the C ABI's own guards used to hide.
*/

use crate::{Instance, Transport, SLOTS};

#[test]
fn a_slot_out_of_range_is_refused_not_a_panic() {
    /*
     * The workspace builds with panic = "abort", so an index out of range from
     * a Rust caller is the host going down. Every door that takes a slot
     * checks it.
     */
    let mut p = Instance::new(44100.0);
    p.reset_depths(SLOTS);
    p.reset_depths(usize::MAX);
    p.randomize(SLOTS, Some(1));
    p.set_param("slot", "8");
    p.set_param("slot", "-1");
    let mut buf = [0u8; 64];
    let n = p.get_param("slot", &mut buf);
    assert_eq!(&buf[..n as usize], b"0");
}

/* ------------------------------------------------ gain continuity, measured */

const SR: f64 = 44100.0;
const BPM: f32 = 120.0;

/// Render DC through the split path, `blocks` blocks of `frames`, calling
/// `edit` before each block. With a DC input of 1 the output IS the gain.
fn render(
    p: &mut Instance,
    blocks: usize,
    frames: usize,
    mut edit: impl FnMut(&mut Instance, usize),
) -> Vec<f32> {
    let mut out = Vec::with_capacity(blocks * frames);
    let mut beats = 0.0;
    for b in 0..blocks {
        edit(p, b);
        let (mut l, mut r) = (vec![1.0f32; frames], vec![1.0f32; frames]);
        let t = Transport { running: true, beats, bpm: BPM };
        p.process_f32_split(&mut l, &mut r, frames, Some(&t));
        out.extend_from_slice(&l);
        beats += frames as f64 / SR * BPM as f64 / 60.0;
    }
    out
}

/// The largest sample-to-sample change in the gain, and where.
fn max_step(v: &[f32]) -> (f32, usize) {
    let mut worst = (0.0f32, 0);
    for i in 1..v.len() {
        let d = (v[i] - v[i - 1]).abs();
        if d > worst.0 {
            worst = (d, i);
        }
    }
    worst
}

/// A gate whose own envelope moves at most ~0.006 per sample even at Width
/// 0.3 (a 12% stage of a 37.5 ms gate is ~200 samples), so anything steeper
/// in a render is the parameter change, not the shape.
fn slow_gate() -> Instance {
    let mut p = Instance::new(SR);
    p.set_param("pattern", "FFFF");
    p.set_param("attack", "12");
    p.set_param("decay", "12");
    p.set_param("sustain", "0.3");
    p.set_param("release", "12");
    p
}

/// A 5 ms glide across a whole 0..1 jump moves under 0.005 a sample at
/// 44.1 kHz, and the gate's own ramps above stay below ~0.006.
const STEP_LIMIT: f32 = 0.01;

#[test]
fn moving_amount_mid_gate_does_not_step_the_gain() {
    let mut p = slow_gate();
    let g = render(&mut p, 200, 64, |p, b| {
        if b > 20 && b % 11 == 0 {
            p.set_param("amount", if (b / 11) % 2 == 0 { "0.2" } else { "1" });
        }
    });
    let (d, at) = max_step(&g);
    assert!(d < STEP_LIMIT, "the gain stepped by {d} at sample {at}");
}

#[test]
fn moving_sustain_mid_sustain_does_not_step_the_gain() {
    let mut p = slow_gate();
    let g = render(&mut p, 200, 64, |p, b| {
        if b > 20 && b % 13 == 0 {
            p.set_param("sustain", if (b / 13) % 2 == 0 { "0.1" } else { "0.9" });
        }
    });
    let (d, at) = max_step(&g);
    assert!(d < STEP_LIMIT, "the gain stepped by {d} at sample {at}");
}

#[test]
fn moving_width_or_a_stage_length_mid_gate_does_not_step_the_gain() {
    /*
     * These two were continuous before any smoothing existed, and this pins
     * why: a stage's length is latched when it is ENTERED (Env keeps its
     * position as 0..1 with a per-stage increment), so a new length applies
     * from the next stage on, and a Width that now ends the gate earlier than
     * where the playhead is enters RELEASE -- a ramp, not a cut.
     */
    let mut p = slow_gate();
    let g = render(&mut p, 300, 64, |p, b| {
        if b > 20 && b % 7 == 0 {
            p.set_param("hold", if (b / 7) % 2 == 0 { "0.3" } else { "1" });
        }
        if b > 20 && b % 17 == 0 {
            p.set_param("attack", if (b / 17) % 2 == 0 { "12" } else { "60" });
            p.set_param("release", if (b / 17) % 2 == 0 { "60" } else { "12" });
        }
    });
    let (d, at) = max_step(&g);
    assert!(d < STEP_LIMIT, "the gain stepped by {d} at sample {at}");
}

#[test]
fn a_steady_patch_is_not_smoothed_at_all() {
    /* The glides only ever run while a value is MOVING. A patch set before
     * the transport starts renders the same bits it always did -- which is
     * also what the golden renders say. */
    let mut a = slow_gate();
    a.set_param("amount", "0.7");
    a.set_param("sustain", "0.45");
    let ga = render(&mut a, 50, 128, |_, _| {});
    let mut b = slow_gate();
    b.set_param("amount", "0.7");
    b.set_param("sustain", "0.45");
    let gb = render(&mut b, 50, 128, |_, _| {});
    assert_eq!(ga, gb);
    /* And after the settle, a value moved and moved back lands exactly. */
    let mut c = slow_gate();
    c.set_param("amount", "0.7");
    c.set_param("sustain", "0.45");
    let mut gc = render(&mut c, 50, 128, |p, b| {
        if b == 10 {
            p.set_param("amount", "0.2");
        }
        if b == 11 {
            p.set_param("amount", "0.7");
        }
    });
    gc.drain(..40 * 128);
    assert_eq!(&ga[40 * 128..], &gc[..]);
}

/* --------------------------------------------------- transport start and stop */

/// `render`, with the transport running only in the blocks `running` says.
/// The playhead restarts from beat 0 at every start, as a host's does.
fn render_transport(
    p: &mut Instance,
    blocks: usize,
    frames: usize,
    running: impl Fn(usize) -> bool,
) -> Vec<f32> {
    let mut out = Vec::new();
    let mut beats = 0.0;
    for b in 0..blocks {
        let run = running(b);
        if !run {
            beats = 0.0;
        }
        let (mut l, mut r) = (vec![1.0f32; frames], vec![1.0f32; frames]);
        let t = Transport { running: run, beats, bpm: BPM };
        p.process_f32_split(&mut l, &mut r, frames, Some(&t));
        out.extend_from_slice(&l);
        if run {
            beats += frames as f64 / SR * BPM as f64 / 60.0;
        }
    }
    out
}

#[test]
fn starting_the_transport_eases_the_gate_in() {
    /*
     * STOPPED IS AN OPEN GATE, and a start used to drop straight onto the
     * pattern: onto a closed step that is 1.0 -> 0.0 in one sample, and onto
     * an open one the attack began from zero -- the same drop, then a rise.
     * The envelope now starts from the open gate: an OFF step releases from
     * it, an ON step attacks from it.
     */
    for pattern in ["FFFF", "0", "5555"] {
        let mut p = slow_gate();
        p.set_param("pattern", pattern);
        let g = render_transport(&mut p, 60, 64, |b| b >= 10);
        assert_eq!(g[10 * 64 - 1], 1.0, "stopped is open");
        let (d, at) = max_step(&g);
        assert!(d < STEP_LIMIT, "pattern {pattern}: the gain stepped by {d} at sample {at}");
    }

    /* And an ON step at full sustain does not dip at all: there is nowhere
     * for an attack from an open gate to go. */
    let mut p = slow_gate();
    p.set_param("sustain", "1");
    let g = render_transport(&mut p, 30, 64, |b| b >= 10);
    assert!(g[..20 * 64].iter().all(|&v| v == 1.0), "the start dipped an open step");
}

#[test]
fn stopping_the_transport_eases_the_gate_open() {
    /* Mid-sustain at 0.3 the stop used to snap straight to 1.0. It glides
     * open now, and lands on exactly 1.0 -- a true bypass again. */
    for pattern in ["FFFF", "0"] {
        let mut p = slow_gate();
        p.set_param("pattern", pattern);
        let g = render_transport(&mut p, 160, 64, |b| b < 60 || b >= 130);
        let (d, at) = max_step(&g);
        assert!(d < STEP_LIMIT, "pattern {pattern}: the gain stepped by {d} at sample {at}");
        assert!(g[60 * 64] < 0.9, "pattern {pattern}: the stop did not glide");
        /* A 5 ms glide lands within ~70 ms, 49 blocks of 64. */
        assert!(g[110 * 64..130 * 64].iter().all(|&v| v == 1.0), "pattern {pattern}: not open once stopped");
    }
}

#[test]
fn a_stop_and_an_immediate_restart_are_continuous() {
    let mut p = slow_gate();
    let g = render_transport(&mut p, 80, 64, |b| b != 40);
    let (d, at) = max_step(&g);
    assert!(d < STEP_LIMIT, "the gain stepped by {d} at sample {at}");
}

/* ------------------------------------------------------------- the i16 path */

#[test]
fn the_i16_path_rounds_like_the_float_path_instead_of_truncating() {
    /*
     * `as i16` TRUNCATES TOWARDS ZERO: every gated sample lost up to a whole
     * LSB, always towards silence, by an amount that scales with the gain
     * being applied -- a bias correlated with exactly what this plugin
     * modulates. sc-core rounds and says so; this path now does the same, so
     * the Move's render is the plugin's float render, rounded.
     */
    let mut f = Instance::new(SR);
    let mut i = Instance::new(SR);
    for p in [&mut f, &mut i] {
        p.set_param("pattern", "5555");
        p.set_param("amount", "0.63");
        p.set_param("sustain", "0.37");
    }
    let n = 512;
    let mut beats = 0.0;
    for _ in 0..40 {
        let src: Vec<i16> = (0..n).map(|k| (((k * 7919) % 60001) as i32 - 30000) as i16).collect();
        let mut fl: Vec<f32> = src.iter().flat_map(|&s| [s as f32, -(s as f32)]).collect();
        let mut il: Vec<i16> = src.iter().flat_map(|&s| [s, -s]).collect();
        let t = Transport { running: true, beats, bpm: BPM };
        f.process_f32(&mut fl, n, Some(&t));
        i.process_i16(&mut il, n, Some(&t));
        for k in 0..n * 2 {
            let want = fl[k].round().clamp(-32768.0, 32767.0) as i16;
            assert_eq!(il[k], want, "sample {k}: float {}", fl[k]);
        }
        beats += n as f64 / SR * BPM as f64 / 60.0;
    }
}

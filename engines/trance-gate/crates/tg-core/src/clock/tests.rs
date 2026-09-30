//! The playhead: the phase-locked loop, and the edge cases a Rust caller can
//! reach that the C ABI's guards used to hide.

use crate::params::Param;
use crate::{Instance, Transport};

fn t(beats: f64) -> Transport {
    Transport { running: true, beats, bpm: 120.0 }
}

#[test]
fn a_zero_frame_block_changes_nothing() {
    /*
     * THE C ABI REFUSES frames <= 0, THE RUST API DID NOT. The loop's
     * correction divided by the frame count, so an empty block made the
     * increment 0/0, and the bypass path -- which advances the phase by
     * `inc * frames` without running the loop -- wrote that NaN into the
     * playhead, where it stayed for the life of the instance.
     */
    let mut p = Instance::new(44100.0);
    p.set_num(Param::Amount, 0.0);
    let mut buf = vec![0.0f32; 256];
    p.process_f32(&mut buf, 128, Some(&t(0.0)));
    let before = p.phase01();
    p.process_f32(&mut buf, 0, Some(&t(128.0 / 44100.0 * 2.0)));
    assert!(p.phase01().is_finite(), "the playhead is NaN");
    assert_eq!(p.phase01(), before, "an empty block moved the playhead");

    /* And with the gate active: the next real block still gates. */
    let mut p = Instance::new(44100.0);
    let mut buf = vec![1.0f32; 256];
    p.process_f32(&mut buf, 128, Some(&t(0.0)));
    p.process_f32(&mut [], 0, Some(&t(0.0)));
    let mut buf = vec![1.0f32; 256];
    p.process_f32(&mut buf, 128, Some(&t(128.0 / 44100.0 * 2.0)));
    assert!(buf.iter().all(|v| v.is_finite()));
}

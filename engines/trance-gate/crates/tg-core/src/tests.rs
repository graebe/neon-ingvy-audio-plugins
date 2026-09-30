/*!
Whole-engine tests: the real `Instance`, driven through a transport and its two
parameter doors.

The C suites (`tests/test_core.c`, `tests/test_gate.c`) and the render hashes
pin the sound; these pin the properties a caller of the RUST API relies on,
which the C ABI's own guards used to hide.
*/

use crate::{Instance, SLOTS};

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

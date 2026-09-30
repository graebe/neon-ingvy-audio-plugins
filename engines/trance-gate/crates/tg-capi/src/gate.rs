/*!
The gate across one cycle, as the engine applies it -- the pattern plot's curve.

Not a model of the envelope: a scratch engine runs the real patch with a DC
input, so the output samples ARE the gain. A release outliving its step, and the
next attack starting from that tail, come out right because the engine made
them.

AMOUNT IS 1 in the scratch engine. The gain `1 - amount*(1 - g)` is affine in
`g`, so Amount is a transform the editor applies when it paints; and at
`amount <= 0` the engine leaves the buffer untouched, which would draw the DC
input -- a solid open gate -- for what is a bypass.

THE TIME BASE IS CHOSEN, NOT SEARCHED FOR. A step's length in ms does not depend
on the sample rate, so setting the scratch's rate to `per_step * 1000 / ms`
makes one step exactly `per_step` samples at 120 BPM.

Two cycles are rendered and the first discarded, so step 0 inherits the tail of
the last step rather than silence.
*/

use ni_dsp::Transport;
use tg_core::Instance;

const BPM: f32 = 120.0;

/// Samples per step for a pattern of `length` steps: whole, so step edges land
/// on samples; about 1024 across the cycle, so the payload stays near a
/// kilobyte; never below 8, so a 128-step pattern still shows each step's shape.
pub fn per_step(length: usize) -> usize {
    let length = length.max(1);
    ((1024 + length / 2) / length).clamp(8, 64)
}

/// 0..1 -> 0..255. A non-finite gain is a shut gate.
pub fn encode_gain(v: f32) -> u8 {
    let c = if v.is_finite() { v.clamp(0.0, 1.0) } else { 0.0 };
    (c * 255.0 + 0.5) as u8
}

/// `"<length>:<per_step>:"` then one raw byte of gain per sample of one cycle
/// -- or None for an empty state or a patch with no step length. Binary: the
/// editor's transport base64-encodes it, so hex inside it would only double it.
pub fn render(state: &str) -> Option<Vec<u8>> {
    if state.is_empty() {
        return None;
    }
    let mut scratch = Instance::new(44100.0);
    scratch.set_param("state", state);
    let length = scratch.pattern().length().clamp(1, tg_core::MAX_STEPS);
    let ms_step = scratch.playhead().ms_per_step as f64;
    if !(ms_step > 0.0) {
        return None;
    }

    let step = per_step(length);
    let sample_rate = step as f64 * 1000.0 / ms_step;
    scratch.set_sample_rate(sample_rate);
    scratch.set_param("amount", "1.0");

    let frames = step * length;
    let mut l = vec![1.0f32; frames * 2];
    let mut r = vec![1.0f32; frames * 2];
    /* One block per step keeps the transport anchored on the step edges, so the
     * engine's phase loop has nothing to chase. */
    for i in 0..length * 2 {
        let off = i * step;
        let t = Transport { running: true, beats: off as f64 * (BPM as f64 / 60.0) / sample_rate, bpm: BPM };
        scratch.process_f32_split(&mut l[off..off + step], &mut r[off..off + step], step, Some(&t));
    }

    let mut out = format!("{length}:{step}:").into_bytes();
    out.reserve(frames);
    out.extend(l[frames..].iter().map(|&g| encode_gain(g)));
    Some(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_step_is_a_whole_number_of_samples_near_a_kilobyte_a_cycle() {
        assert_eq!(per_step(16), 64);
        assert_eq!(per_step(1), 64);
        assert_eq!(per_step(0), 64);
        assert_eq!(per_step(32), 32);
        assert_eq!(per_step(100), 10);
        assert_eq!(per_step(128), 8);
        for n in 1..=128 {
            let total = per_step(n) * n;
            assert!(total <= 1088, "{n} steps -> {total} samples");
        }
    }

    #[test]
    fn a_gain_is_unipolar_and_a_nan_is_shut() {
        assert_eq!(encode_gain(0.0), 0);
        assert_eq!(encode_gain(1.0), 255);
        assert_eq!(encode_gain(0.5), 128);
        assert_eq!(encode_gain(-1.0), 0);
        assert_eq!(encode_gain(2.0), 255);
        assert_eq!(encode_gain(f32::NAN), 0);
        assert_eq!(encode_gain(f32::INFINITY), 0);
    }

    fn state_of(inst: &Instance) -> String {
        let mut buf = vec![0u8; 8192];
        let n = inst.get_param("state", &mut buf);
        String::from_utf8(buf[..n as usize].to_vec()).unwrap()
    }

    #[test]
    fn the_curve_is_the_gain_the_engine_applies() {
        let mut inst = Instance::new(48000.0);
        /* Steps 0 and 2 on, 1 and 3 off, over four steps. */
        inst.set_param("length", "3");
        for (i, on) in [(0, "1"), (1, "0"), (2, "1"), (3, "0")] {
            inst.set_param("cursor", &i.to_string());
            inst.set_param("step", on);
        }
        let got = render(&state_of(&inst)).expect("a patch with steps renders");
        let (head, body) = got.split_at(5);
        assert_eq!(head, b"4:64:");
        assert_eq!(body.len(), 4 * 64, "one raw byte a sample");
        let byte = |i: usize| body[i];
        /* Mid-step: open on an on step, shut once an off step's release is done. */
        assert!(byte(32) > 200, "step 0 is open");
        assert!(byte(64 + 60) < 20, "step 1 has closed by its end");
        assert!(byte(128 + 32) > 200, "step 2 is open");
    }

    #[test]
    fn nothing_to_draw_is_none() {
        assert_eq!(render(""), None);
    }
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The envelope plot's two curves, as the engine applies them -- one gate on its
own, and the envelope as dialled.

The editor used to carry the stage machine in JavaScript to draw these; now a
scratch engine runs them, exactly as gate.rs renders the pattern plot's curve:
a DC input at Amount 1, so the output samples ARE the envelope.

  gated   step 0 on and every step after it off: the gate opens, shuts at
          Width whatever stage is running, releases from the level it reached
          -- and, at each following step's edge, does whatever the engine does
          there.
  ghost   every step on and TIED, at the same Width: the envelope as dialled,
          with no gate closing over it. Stage lengths are percentages of the
          Width, so the ghost has to be rendered at the Width the gate uses.

Both start from silence and span STEPS steps, which holds the
longest stage the knobs reach (attack + decay at 200 % each of a full-width
gate). They are rendered as the SECOND half of a pattern twice as long, whose
first half is silent: a fresh engine opens its Amount from nothing, and the
pre-roll is what lets that settle -- gate.rs discards a first cycle for the
same reason. The x-axis is fractions of a step, which is tempo-independent: the
editor maps it to milliseconds with the step length it already knows.
*/

use ni_dsp::Transport;
use tg_core::Instance;

/// Steps rendered: attack + decay reach 4 steps at 200 % each of Width 100 %.
pub const STEPS: usize = 4;
/// Samples per step: 256 a curve, about a sample per pixel of the plot.
pub const PER_STEP: usize = 64;

const BPM: f32 = 120.0;

#[allow(clippy::neg_cmp_op_on_partial_ord, reason = "a NaN step length must take the guard, not reach the division")]
fn curve(state: &str, pattern: &str, ties: &str) -> Option<Vec<u8>> {
    let mut scratch = Instance::new(44100.0);
    scratch.set_param("state", state);
    let ms_step = scratch.playhead().ms_per_step as f64;
    if !(ms_step > 0.0) {
        return None;
    }
    let sample_rate = PER_STEP as f64 * 1000.0 / ms_step;
    scratch.set_sample_rate(sample_rate);
    scratch.set_param("amount", "1.0");
    scratch.set_param("fade", "1.0");
    scratch.set_param("legato", "0");
    scratch.set_param("length", &(2 * STEPS - 1).to_string());
    scratch.set_param("pattern", pattern);
    scratch.set_param("ties", ties);
    for i in 0..2 * STEPS {
        scratch.set_param("cursor", &i.to_string());
        scratch.set_param("step_amount", "1.0");
    }

    let frames = STEPS * PER_STEP;
    let mut l = vec![1.0f32; 2 * frames];
    let mut r = vec![1.0f32; 2 * frames];
    for i in 0..2 * STEPS {
        let off = i * PER_STEP;
        let t = Transport { running: true, beats: off as f64 * (BPM as f64 / 60.0) / sample_rate, bpm: BPM };
        scratch.process_f32_split(&mut l[off..off + PER_STEP], &mut r[off..off + PER_STEP], PER_STEP, Some(&t));
    }
    Some(l[frames..].iter().map(|&g| crate::gate::encode_gain(g)).collect())
}

/// `"<steps>:<per_step>:"` then the gated curve and the ghost, one raw byte of
/// gain per sample each -- or None for an empty state.
pub fn render(state: &str) -> Option<Vec<u8>> {
    if state.is_empty() {
        return None;
    }
    /* Step STEPS on (and, for the ghost, the rest after it on and tied); the
     * first STEPS steps are the silent pre-roll. */
    let gated = curve(state, "10", "0")?;
    let ghost = curve(state, "F0", "F0")?;
    let mut out = format!("{STEPS}:{PER_STEP}:").into_bytes();
    out.extend_from_slice(&gated);
    out.extend_from_slice(&ghost);
    Some(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn state_with(set: &[(&str, &str)]) -> String {
        let mut inst = Instance::new(48000.0);
        for (k, v) in set {
            inst.set_param(k, v);
        }
        let mut buf = vec![0u8; 8192];
        let n = inst.get_param("state", &mut buf);
        String::from_utf8(buf[..n as usize].to_vec()).unwrap()
    }

    fn curves(state: &str) -> (Vec<f32>, Vec<f32>) {
        let got = render(state).expect("a patch renders");
        let head = format!("{STEPS}:{PER_STEP}:");
        assert_eq!(&got[..head.len()], head.as_bytes());
        let body = &got[head.len()..];
        assert_eq!(body.len(), 2 * STEPS * PER_STEP);
        let f = |b: &[u8]| b.iter().map(|&v| v as f32 / 255.0).collect::<Vec<_>>();
        (f(&body[..STEPS * PER_STEP]), f(&body[STEPS * PER_STEP..]))
    }

    #[test]
    fn the_gate_opens_holds_its_sustain_and_shuts_at_width() {
        /* Width 50 %, short stages, sustain 50 %. */
        let s = state_with(&[("hold", "0.5"), ("attack", "10"), ("decay", "10"),
                             ("sustain", "0.5"), ("release", "10")]);
        let (gated, ghost) = curves(&s);
        assert!(gated[0] < 0.1, "from silence");
        let mid = gated[PER_STEP * 2 / 5];
        assert!((mid - 0.5).abs() < 0.03, "sustaining at 0.5 inside the gate, got {mid}");
        assert!(gated[PER_STEP * 3 / 4] < 0.05, "shut and released by three quarters");
        /* The ghost is the same envelope with no close: still sustaining. */
        assert!((ghost[PER_STEP * 3 / 4] - 0.5).abs() < 0.03, "the ghost holds its sustain");
    }

    #[test]
    fn a_long_decay_is_cut_by_the_gate_and_the_ghost_shows_what_was_dialled() {
        /* Attack + decay far longer than a 25 % gate. */
        let s = state_with(&[("hold", "0.25"), ("attack", "20"), ("decay", "200"),
                             ("sustain", "0.0"), ("release", "20")]);
        let (gated, ghost) = curves(&s);
        /* 0.35 of a step: the gate shut at 0.25 and its 0.05-step release is
         * done; the dialled decay (0.05 to 0.55) is 60 % of the way down. */
        let after = PER_STEP * 35 / 100;
        assert!(gated[after] < 0.05, "the gate shut whatever stage was running");
        assert!(ghost[after] > 0.3, "the dialled decay is still on its way down");
    }

    #[test]
    fn nothing_to_draw_is_none() {
        assert_eq!(render(""), None);
    }
}

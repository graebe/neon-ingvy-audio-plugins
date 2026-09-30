/*!
A scope's x-axis: where in one cycle each sample of a block fell, 0..1.

A display locked to a pattern draws column `k` at cycle phase `k / cols`, so it
needs a phase per sample. The engine reports its phase once either side of a
block; the phase is linear in time across a block, so interpolating between the
two is exact. A block whose phase went backwards, or jumped by more than half a
cycle, is a seek or a stop, not a sweep across the whole axis; so is a stopped
transport, which has no phase at all. In those cases the sweep runs on its own
at one cycle per `cycle_samples`, so the picture keeps its meaning and keeps
moving while the audio passes through audibly.
*/

#[derive(Clone, Copy, Default, Debug, PartialEq)]
pub struct CycleSweep {
    /// Where the sweep ended last block, 0..1.
    pos: f64,
}

impl CycleSweep {
    /// Fill `out` with one block's positions, 0..1. `ph0` and `ph1` are the
    /// engine's cycle phase before and after the block, `advancing` whether
    /// the transport moved it, `cycle_samples` the free-running length.
    pub fn fill(&mut self, ph0: f64, ph1: f64, advancing: bool, cycle_samples: f64, out: &mut [f32]) {
        let frames = out.len();
        if frames == 0 {
            return;
        }
        let mut start = ph0;
        let mut span = ph1 - ph0;
        let mut jumped = false;
        if span < -0.5 {
            span += 1.0;
        } else if span > 0.5 {
            span -= 1.0;
            jumped = true;
        }
        if span < 0.0 {
            jumped = true;
        }
        if !advancing || jumped {
            start = self.pos;
            span = if cycle_samples > 0.0 { frames as f64 / cycle_samples } else { 0.0 };
        }
        self.pos = start + span;
        self.pos -= self.pos.floor();

        for (i, o) in out.iter_mut().enumerate() {
            let mut ph = start + span * (i as f64 / frames as f64);
            ph -= ph.floor();
            *o = ph as f32;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_running_block_is_interpolated_between_its_ends() {
        let mut s = CycleSweep::default();
        let mut out = [0f32; 4];
        s.fill(0.25, 0.5, true, 1000.0, &mut out);
        assert_eq!(out, [0.25, 0.3125, 0.375, 0.4375]);
    }

    #[test]
    fn a_wrap_inside_the_block_continues_past_the_end() {
        let mut s = CycleSweep::default();
        let mut out = [0f32; 2];
        s.fill(0.9, 0.1, true, 1000.0, &mut out);
        assert_eq!(out[0], 0.9);
        assert!((out[1] - 0.0).abs() < 1e-6, "{out:?}");
    }

    #[test]
    fn a_seek_or_a_stop_free_runs_from_where_the_sweep_was() {
        let mut s = CycleSweep::default();
        let mut out = [0f32; 4];
        s.fill(0.0, 0.25, true, 100.0, &mut out);
        /* Backwards: a seek. The sweep carries on at one cycle per 100. */
        s.fill(0.5, 0.3, true, 100.0, &mut out);
        for (o, want) in out.iter().zip([0.25, 0.26, 0.27, 0.28]) {
            assert!((o - want).abs() < 1e-6, "{out:?}");
        }
        /* Stopped: the same, from where it got to. */
        s.fill(0.0, 0.0, false, 100.0, &mut out);
        assert!((out[0] - 0.29).abs() < 1e-6, "{out:?}");
    }

    #[test]
    fn an_empty_block_changes_nothing() {
        let mut s = CycleSweep::default();
        s.fill(0.1, 0.2, true, 10.0, &mut []);
        let mut out = [0f32; 1];
        s.fill(0.0, 0.0, false, 10.0, &mut out);
        assert_eq!(out[0], 0.0);
    }
}

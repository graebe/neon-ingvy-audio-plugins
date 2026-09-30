/*!
The playhead: where in the pattern the host's transport says we are, tracked
per block by a phase-locked loop and advanced per sample by the gain loop.
*/

use crate::envelope::{Stage, StageLens};
use crate::{rates, Instance, Transport, MAX_STEPS};

/// Beyond this much error, jump rather than glide.
const RESYNC_STEPS: f64 = 0.25;
/// Fraction of the phase error absorbed per block.
const TRACK_GAIN: f64 = 0.05;

impl Instance {
    /*
     * PHASE IS ANCHORED PER BLOCK AND ADVANCED PER SAMPLE.
     *
     * The host's beat position is interpolated per block and its intra-tick
     * fraction is CLAMPED, so a late tick freezes phase instead of
     * overshooting. Differencing it per sample therefore renders that plateau
     * as an audible stutter on every late clock tick. Instead the host's
     * answer is an anchor that a local accumulator is pulled gently towards
     * -- a phase-locked loop, not a clock divider.
     *
     * Returns `None` when the block needs no gain applied at all, having
     * already advanced whatever state the next block depends on -- or when it
     * is empty. The caller
     * leaves the buffer untouched, which is exactly right: the dry signal is
     * already in it.
     */
    pub(crate) fn block_setup(&mut self, frames: usize, t: Option<&Transport>) -> Option<Run> {
        /* An empty block is not a block: no time passed, so nothing -- not the
         * playhead, not the transport edge -- may move. The C ABI refuses one
         * before it gets here; a Rust caller is not stopped by that. */
        if frames == 0 {
            return None;
        }
        let length = self.pattern().length.clamp(1, MAX_STEPS);

        let mut bpm = 120.0f32;
        if let Some(t) = t {
            if t.bpm > 1.0 && t.bpm < 1000.0 {
                bpm = t.bpm;
            }
        }

        let beats_per_step = rates::RATES[self.rate_idx].beats;
        let mut samples_per_step = (60.0 / bpm as f64) * self.sample_rate * beats_per_step;
        if samples_per_step < 1.0 {
            samples_per_step = 1.0;
        }
        let mut inc = 1.0 / samples_per_step;

        /* A stopped transport is not beat 0, it is no beat at all. */
        let beats = match t {
            Some(t) if t.running => t.beats,
            _ => -1.0,
        };
        let running = beats >= 0.0;

        self.last_bpm = bpm;
        self.ms_per_step = (samples_per_step * 1000.0 / self.sample_rate) as f32;
        self.advancing = running;

        if running {
            let target = beats / beats_per_step;
            if !self.was_running {
                /* Transport just started: land exactly, do not glide in. The
                 * gate was open until now, so the glides have nothing to be
                 * continuous WITH -- they start at their targets. */
                self.step_pos = target;
                self.last_step = None;
                self.amount_s = self.amount;
                self.sustain_s = self.sustain;
            } else {
                let err = target - self.step_pos;
                if err > RESYNC_STEPS || err < -RESYNC_STEPS {
                    self.step_pos = target; /* loop, seek or tempo jump */
                    /* A jump re-evaluates the step even when it lands on the
                     * same index: the boundary test is `step != last_step`,
                     * so a seek back onto the step we were already on would
                     * fire nothing and leave the envelope where it was. */
                    self.last_step = None;
                } else {
                    inc += (err * TRACK_GAIN) / frames as f64;
                }
            }
        } else {
            /* Stopped means open: hold the gate open and park at step 0, so
             * the next start is a downbeat rather than wherever the pattern
             * stopped. */
            self.step_pos = 0.0;
            self.last_step = None;
            self.env.stage = Stage::Idle;
            self.env.level = 0.0;
            self.was_running = false;
            return None;
        }
        self.was_running = running;

        /* Amount zero is a true bypass -- m collapses to exactly 1.0 -- so do
         * not spend a block proving it, once the glide down to it has landed.
         * The phase still advances, so turning it back up lands on the step
         * the pattern would have reached. */
        if self.amount <= 0.0 && self.amount_s <= 0.0 {
            self.step_pos += inc * frames as f64;
            return None;
        }

        let frac = self.step_pos - self.step_pos.floor();
        let mut step = (self.step_pos.floor() % length as f64) as i64;
        if step < 0 {
            step += length as i64;
        }
        Some(Run {
            lens: self.lens(),
            smooth: crate::smooth::coef(self.sample_rate),
            length,
            inc,
            frac,
            step: step as usize,
        })
    }
}

/// Per-block state the sample loop walks.
pub(crate) struct Run {
    /// The stage lengths, worked out once for the block. Everything they
    /// depend on -- the rate, the tempo, Width and the three stage values --
    /// can only change between blocks, and the sample loop used to rebuild
    /// them every sample, twice when Width was below 1.
    pub(crate) lens: StageLens,
    /// The parameter glide's per-sample coefficient at this sample rate.
    pub(crate) smooth: f32,
    pub(crate) length: usize,
    pub(crate) inc: f64,
    pub(crate) frac: f64,
    pub(crate) step: usize,
}

#[cfg(test)]
mod tests;

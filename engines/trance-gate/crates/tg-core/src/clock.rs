/*!
The playhead: where in the pattern the host's transport says we are, tracked
per block by a phase-locked loop and advanced per sample by the gain loop.
*/

use crate::envelope::{Stage, StageLens};
use crate::{rates, Instance, Transport, MAX_STEPS};
use ni_dsp::phase::Edge;

impl Instance {
    /*
     * THE TRANSPORT STARTED: PICK THE GATE UP WHERE IT IS. The envelope is
     * SEEDED at the level the gain already has and held there, so the first
     * step attacks or releases from it like any other. `step_level` 1 and
     * `env.level` E solve 1 - amount * (1 - E) = g0; clamped at 0 for Amount
     * lowered while a stop was still gliding open.
     */
    fn engage(&mut self) {
        let g0 = 1.0 - self.amount_s * (1.0 - self.env.level * self.step_level);
        self.amount_s = self.amount;
        if self.amount > 0.0 {
            self.step_level = 1.0;
            self.env.level = (1.0 - (1.0 - g0) / self.amount).max(0.0);
            self.env.stage = Stage::Sustain;
        }
    }

    /*
     * Resolve tempo and the playhead for one block; see `ni_dsp::phase` for
     * the loop. `None` when the block needs no gain at all (having advanced
     * whatever the next block depends on) or is empty -- the caller leaves the
     * dry signal in the buffer.
     */
    pub(crate) fn block_setup(&mut self, frames: usize, t: Option<&Transport>) -> Option<Run> {
        /* No time passed, so nothing -- not the playhead, not the transport
         * edge -- may move. */
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

        /* A stopped transport is not beat 0, it is no beat at all. */
        let beats = match t {
            Some(t) if t.running => t.beats,
            _ => -1.0,
        };
        let running = beats >= 0.0;

        self.last_bpm = bpm;
        self.ms_per_step = (samples_per_step * 1000.0 / self.sample_rate) as f32;
        self.advancing = running;

        let target = if running { Some(beats / beats_per_step) } else { None };
        let (inc, edge) = self.phase.follow(target, samples_per_step, frames, self.sample_rate);
        match edge {
            Edge::Started => {
                self.last_step = None;
                self.engage();
            }
            /* A jump re-evaluates the step even when it lands on the same
             * index: the boundary test is `step != last_step`. */
            Edge::Jumped => self.last_step = None,
            Edge::Tracking => {}
            Edge::Stopped | Edge::Parked => {
                /* Stopped means open, parked at step 0 so the next start is a
                 * downbeat. OPEN BY GLIDING, NOT BY CUTTING: the envelope
                 * freezes and Amount glides out -- `amount_s` at 0 is the dry
                 * signal -- and only once that has landed is the block handed
                 * back untouched. */
                self.last_step = None;
                if self.amount_s <= 0.0 {
                    self.env.stage = Stage::Idle;
                    self.env.level = 0.0;
                    return None;
                }
                return Some(Run {
                    opening: true,
                    lens: self.lens(),
                    smooth: ni_dsp::smooth::coef(self.sample_rate),
                    length,
                    inc: 0.0,
                    frac: 0.0,
                    step: 0,
                });
            }
        }

        /* Amount zero is a true bypass -- m collapses to exactly 1.0 -- so do
         * not spend a block proving it, once the glide down to it has landed.
         * The phase still advances, so turning it back up lands on the step
         * the pattern would have reached. */
        if self.amount <= 0.0 && self.amount_s <= 0.0 {
            self.phase.pos += inc * frames as f64;
            return None;
        }

        let frac = self.phase.pos - self.phase.pos.floor();
        let mut step = (self.phase.pos.floor() % length as f64) as i64;
        if step < 0 {
            step += length as i64;
        }
        Some(Run {
            opening: false,
            lens: self.lens(),
            smooth: ni_dsp::smooth::coef(self.sample_rate),
            length,
            inc,
            frac,
            step: step as usize,
        })
    }
}

/// Per-block state the sample loop walks.
pub(crate) struct Run {
    /// The transport has stopped and the gate is gliding open: the envelope
    /// and the playhead are frozen, and only `amount_s` moves.
    pub(crate) opening: bool,
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

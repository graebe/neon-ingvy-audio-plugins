/*!
The playhead: where in the pattern the host's transport says we are, tracked
per block by a phase-locked loop and advanced per sample by the gain loop.
*/

use crate::envelope::{Stage, StageLens};
use crate::{rates, Instance, Transport, MAX_STEPS};

/// Beyond this much error, jump rather than glide.
const RESYNC_STEPS: f64 = 0.25;
/*
 * HOW FAST THE LOOP PULLS IN, AS A TIME CONSTANT.
 *
 * It was "5% of the error per block", which is a rate per CALL: at one sample
 * a block the error was gone in a few milliseconds, at 4096 most of it was
 * still there a quarter of a second later, and the same host tracked
 * differently depending on its buffer size. A time constant is the same pull
 * whatever the block size -- the fraction left after `t` seconds is
 * exp(-t / TRACK_TAU_S) however those seconds were chopped up.
 *
 * 56.6 ms is the old constant where it was tuned: 5% per 128 samples at
 * 44.1 kHz, the Move's block, is -128 / 44100 / ln(0.95) seconds.
 */
pub(crate) const TRACK_TAU_S: f64 = 0.0566;
/// The slowest the playhead may run while it pulls back towards a host that
/// is behind it, as a fraction of its nominal speed. Never zero or less: see
/// where it is applied.
const MIN_SPEED: f64 = 0.5;

impl Instance {
    /*
     * THE TRANSPORT STARTED: PICK THE GATE UP WHERE IT IS.
     *
     * Stopped is an open gate, and a start used to drop straight onto the
     * pattern with the envelope at zero -- the gain fell to 1 - Amount in one
     * sample and, on an ON step, rose again over the attack. Instead the
     * envelope is SEEDED at the level the gain already has (1.0 after a
     * finished stop, or wherever a stop still gliding open had got to) and
     * holds there, so the first step does what any step does from a held
     * level: an ON step attacks from it, an OFF step releases from it. No
     * special case in the gain law; only a starting point.
     *
     * `step_level` 1 and `env.level` E give env * level = E, and
     * 1 - amount * (1 - E) = g0 solves to E below. Clamped at 0 for the one
     * case it cannot reach: Amount lowered while a stop was still gliding.
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
                /* Transport just started: land exactly, do not glide in. */
                self.step_pos = target;
                self.last_step = None;
                self.engage();
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
                    let absorb = 1.0 - (-(frames as f64) / (self.sample_rate * TRACK_TAU_S)).exp();
                    inc += err * absorb / frames as f64;
                }
            }
            /* NEVER BACKWARDS. A host far enough behind, at a long step and a
             * short block, asked for a correction larger than the step's own
             * increment: the playhead reversed, re-crossed the boundary it had
             * just passed and replayed that step. Slowing down is how a
             * playhead waits; only a seek (the resync above) moves it back. */
            inc = inc.max(MIN_SPEED / samples_per_step);
        } else {
            /* Stopped means open: park at step 0, so the next start is a
             * downbeat rather than wherever the pattern stopped.
             *
             * AND OPEN BY GLIDING, NOT BY CUTTING. Mid-gate the gain is
             * wherever the envelope had it; snapping it to 1.0 was a click on
             * every stop. The envelope freezes where it is and Amount's effect
             * glides out -- `amount_s` to 0 is exactly the dry signal -- and
             * only once that has landed is the block handed back untouched. */
            self.step_pos = 0.0;
            self.last_step = None;
            self.was_running = false;
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

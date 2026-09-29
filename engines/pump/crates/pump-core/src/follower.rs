/*!
The sidechain detector: turning a key signal into trigger instants.

WHAT THIS IS NOT. It is not a compressor's gain computer. A compressor's
detector output IS its gain, continuously; this one only has to answer "did the
kick just land", and hand that one bit to the same envelope the cycle and the
MIDI sources drive. That is what keeps three trigger sources on one shape: the
sources disagree about WHEN, and about nothing else.

THE DETECTOR IS PEAK ACROSS CHANNELS, NOT A SUM. A kick panned hard left
halves in a `(l + r) / 2` sum and can fall under a threshold that was set while
it was centred -- the threshold would then be reading the panner. `max(|l|,
|r|)` asks the question that was meant: did anything get loud.

TWO TIME CONSTANTS, AND THE FALL IS SIZED FOR ONE WAVEFORM CYCLE -- NO MORE.

A bare `|x| > threshold` fires on every sample a waveform spends above the line,
so one kick is a few dozen triggers inside a millisecond. Smoothing with a fast
rise and a slow fall turns that burst into one hump with one crossing. The rise
is fast so the edge is not late behind the transient; the fall has to outlast
one cycle of the lowest thing that can trigger, or the detector re-crosses on
every period of the fundamental.

A kick's fundamental sits around 50-60 Hz, so a cycle is 17-20 ms and 25 ms of
fall covers it with margin.

WHY NOT LONGER, WHICH IS THE MISTAKE THIS COMMENT EXISTS TO RECORD. The fall was
60 ms first, and 60 ms does not merely smooth -- it MERGES. Two hits 50 ms apart
became one trigger no matter what the user asked for, because the detector never
came back under the line between them. That quietly took the decision away from
`Lockout`, which is the control that is supposed to own it: a parameter whose
effect is pre-empted by a private constant is a parameter that does not work.

So the split is deliberate. The FALL makes one hit read as one hump -- a
question about waveforms, answered here. The LOCKOUT decides how close two
triggers may be -- a question about music, answered by the user.
*/

/// Detector rise time. Short enough that the edge is not audibly late behind
/// the transient that caused it.
const RISE_MS: f64 = 1.0;
/// Detector fall time. One cycle of a ~50 Hz fundamental plus margin -- long
/// enough that one drum hit is one hump, short enough that separating two hits
/// stays `Lockout`'s decision and not this constant's. See the header.
const FALL_MS: f64 = 25.0;

/// One-pole coefficient for a time constant in ms at a sample rate.
///
/// `1 - exp(-1 / (ms * sr / 1000))`, and the guard is not decoration: a
/// zero or absurd sample rate would otherwise produce a coefficient outside
/// 0..1, and a one-pole with a coefficient above 1 oscillates.
fn coeff(ms: f64, sample_rate: f64) -> f64 {
    let n = ms * sample_rate / 1000.0;
    if !(n > 0.0) {
        return 1.0;
    }
    (1.0 - (-1.0 / n).exp()).clamp(0.0, 1.0)
}

#[derive(Clone, Copy, Debug)]
pub struct Follower {
    env: f64,
    rise: f64,
    fall: f64,
    /// True while the smoothed detector sits above the threshold. The trigger
    /// is the FALSE -> TRUE transition, so a signal that stays loud fires once.
    above: bool,
    /// Samples still to wait before another edge counts.
    lockout_left: f64,
}

impl Follower {
    pub fn new(sample_rate: f64) -> Self {
        Follower {
            env: 0.0,
            rise: coeff(RISE_MS, sample_rate),
            fall: coeff(FALL_MS, sample_rate),
            above: false,
            lockout_left: 0.0,
        }
    }

    pub fn set_sample_rate(&mut self, sample_rate: f64) {
        self.rise = coeff(RISE_MS, sample_rate);
        self.fall = coeff(FALL_MS, sample_rate);
        self.reset();
    }

    /// Forget everything. Called when the source changes away from Sidechain,
    /// so switching back does not fire on a stale edge.
    pub fn reset(&mut self) {
        self.env = 0.0;
        self.above = false;
        self.lockout_left = 0.0;
    }

    /// The smoothed detector level, for the UI's meter.
    pub fn level(&self) -> f64 {
        self.env
    }

    /// Feed one frame of the key signal.
    ///
    /// Returns `true` on the sample the threshold is crossed upwards, at most
    /// once per `lockout` samples. `threshold` is linear amplitude, not dB --
    /// the dB is the user's unit and is converted once, in `params.rs`.
    pub fn next(&mut self, l: f32, r: f32, threshold: f64, lockout: f64) -> bool {
        /* A NaN in the key buffer must not poison the detector for the rest of
         * the session: `abs` propagates it, and a NaN env compares false
         * against everything, so `above` would latch and never clear. */
        let key = {
            let a = l.abs();
            let b = r.abs();
            let m = if a > b { a } else { b } as f64;
            if m.is_finite() {
                m
            } else {
                0.0
            }
        };

        let c = if key > self.env { self.rise } else { self.fall };
        self.env += (key - self.env) * c;

        if self.lockout_left > 0.0 {
            self.lockout_left -= 1.0;
        }

        let now_above = self.env >= threshold;
        /* The edge, and only the edge. `above` is updated whether or not the
         * trigger is allowed, so a hit suppressed by the lockout still arms
         * the next one -- otherwise a busy loop inside the lockout window
         * would leave `above` true and the following kick would fire nothing. */
        let fired = now_above && !self.above && self.lockout_left <= 0.0;
        self.above = now_above;
        if fired {
            self.lockout_left = lockout;
        }
        fired
    }
}

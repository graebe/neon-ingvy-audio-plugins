/*!
A second-order section, and the two Butterworth responses the ground's band needs.

WHY THIS FILE EXISTS AT ALL. Nothing else in this workspace filters by
frequency: the side-chain's detector is broadband, the trance gate shapes an
envelope, and the spectrogram gets its bands out of an FFT it already had. The
ground is the first thing here that has to ask "how loud is the signal BETWEEN
20 and 80 Hz", so it is the first thing that needs a biquad.

THE COEFFICIENTS ARE THE AUDIO EQ COOKBOOK'S, and that is a compatibility
decision rather than a taste one. The design system's reference implementation
(`design/files/project/components/ground.js`) builds its band out of two Web
Audio `BiquadFilterNode`s, and Web Audio specifies exactly these formulas. A
different-but-equally-valid design -- an SVF, a cascade of one-poles -- would
have a different phase response and a different transient, so the same kick
would cross the threshold at a different instant and the previews the design
was judged against would no longer describe what ships.

Q IS 1/sqrt(2) AND NOT A PARAMETER. One section at that Q is maximally flat:
12 dB/oct with no resonant bump at the corner. The design's detector row asks
for "12 dB/oct each side", which is one section each, so there is no cascade
to distribute a Q across and nothing for a caller to tune. A knob nobody may
turn is better spelled as a constant.

STATE AND ARITHMETIC ARE f64, DELIBERATELY. A 20 Hz corner at 96 kHz is a pole
about 0.9987 from the origin; in f32 the difference between that pole and the
unit circle is a few hundred ulps, and a direct-form section there accumulates
audible error in its own feedback. The samples arrive as f32 and the answer
leaves as f64; only the recursion in between is the part that cannot afford it.
*/

/// Anything smaller than this in magnitude is state that has decayed to
/// nothing, and is stored as exactly zero.
///
/// 1e-30 is ~-600 dB: some 280 decades above the subnormal range, so a value is
/// flushed long before it can become one, and some 25 decades below anything
/// the detector's thresholds (`FLUX_FLOOR`, `FLOOR`) can see, so no onset
/// timing moves. See `flush`.
pub(crate) const FLUSH_BELOW: f64 = 1e-30;

/// Flush a recursive state value to zero once it has decayed below audibility.
///
/// WHY THIS IS DONE IN CODE AND NOT BY THE CPU. A one-pole or a biquad fed
/// silence decays geometrically into the SUBNORMAL range, and a one-pole can
/// then sit there forever: `x * (1 - c)` of a subnormal rounds back to the same
/// subnormal once `x * c` underflows. Arithmetic on subnormals takes a microcode
/// assist on x86 and on several ARM cores -- tens to hundreds of cycles an
/// operation, on the audio thread, in every plugin, for the whole of the
/// silence between clips. Nothing in this repository sets FTZ/DAZ (the host
/// owns the thread's floating-point mode, and the Move's aarch64 build is not
/// ours to configure), so the state is kept clean here, where it is portable
/// and deterministic.
#[inline(always)]
pub(crate) fn flush(v: f64) -> f64 {
    if v.abs() < FLUSH_BELOW {
        0.0
    } else {
        v
    }
}

/// `1/sqrt(2)` -- the Butterworth Q for a single second-order section. Web
/// Audio spells this `Math.SQRT1_2`, which is the value the reference passes.
const BUTTERWORTH_Q: f64 = core::f64::consts::FRAC_1_SQRT_2;

/// A direct-form-I second-order section.
///
/// Direct form I rather than II: it keeps the two input delays separate from
/// the two output delays, so `reset` clears a state that cannot hold a
/// half-finished sample, and a coefficient change between blocks cannot make
/// the section jump the way a shared state would.
#[derive(Clone, Copy, Debug, Default)]
pub struct Biquad {
    b0: f64,
    b1: f64,
    b2: f64,
    a1: f64,
    a2: f64,
    x1: f64,
    x2: f64,
    y1: f64,
    y2: f64,
}

impl Biquad {
    /// A 12 dB/oct Butterworth high-pass at `hz`.
    pub fn highpass(hz: f64, sample_rate: f64) -> Self {
        let mut q = Self::default();
        q.set_highpass(hz, sample_rate);
        q
    }

    /// A 12 dB/oct Butterworth low-pass at `hz`.
    pub fn lowpass(hz: f64, sample_rate: f64) -> Self {
        let mut q = Self::default();
        q.set_lowpass(hz, sample_rate);
        q
    }

    pub fn set_highpass(&mut self, hz: f64, sample_rate: f64) {
        match Shape::new(hz, sample_rate) {
            Some(s) => {
                let k = (1.0 + s.cos_w0) / 2.0;
                self.set(k, -2.0 * k, k, s);
            }
            None => self.set_bypass(),
        }
    }

    pub fn set_lowpass(&mut self, hz: f64, sample_rate: f64) {
        match Shape::new(hz, sample_rate) {
            Some(s) => {
                let k = (1.0 - s.cos_w0) / 2.0;
                self.set(k, 2.0 * k, k, s);
            }
            None => self.set_bypass(),
        }
    }

    /// Normalise by `a0` and keep the five coefficients a sample needs.
    fn set(&mut self, b0: f64, b1: f64, b2: f64, s: Shape) {
        let a0 = 1.0 + s.alpha;
        self.b0 = b0 / a0;
        self.b1 = b1 / a0;
        self.b2 = b2 / a0;
        self.a1 = (-2.0 * s.cos_w0) / a0;
        self.a2 = (1.0 - s.alpha) / a0;
        self.reset();
    }

    /* A nonsensical corner -- a zero or negative sample rate, a frequency at or
     * past Nyquist -- becomes a pass-through rather than a NaN factory. The
     * detector above would otherwise latch on the first NaN and never fire
     * again, which is a silent dead background rather than a reported fault. */
    fn set_bypass(&mut self) {
        self.b0 = 1.0;
        self.b1 = 0.0;
        self.b2 = 0.0;
        self.a1 = 0.0;
        self.a2 = 0.0;
        self.reset();
    }

    pub fn reset(&mut self) {
        self.x1 = 0.0;
        self.x2 = 0.0;
        self.y1 = 0.0;
        self.y2 = 0.0;
    }

    /// One sample through the section.
    ///
    /// The output is flushed before it is stored (see `flush`), so the output
    /// delays can never hold a subnormal. The input is stored as given: the
    /// detector cleans it before it gets here, and the second section's input is
    /// the first section's already-flushed output.
    pub fn next(&mut self, x: f64) -> f64 {
        let y = flush(
            self.b0 * x + self.b1 * self.x1 + self.b2 * self.x2
                - self.a1 * self.y1
                - self.a2 * self.y2,
        );
        self.x2 = self.x1;
        self.x1 = x;
        self.y2 = self.y1;
        self.y1 = y;
        y
    }

    /// The four delays, for the tests that pin what silence decays into.
    #[cfg(test)]
    pub(crate) fn state(&self) -> [f64; 4] {
        [self.x1, self.x2, self.y1, self.y2]
    }
}

/// The two quantities both responses are built from.
#[derive(Clone, Copy)]
struct Shape {
    cos_w0: f64,
    alpha: f64,
}

impl Shape {
    fn new(hz: f64, sample_rate: f64) -> Option<Self> {
        if !(sample_rate > 0.0) || !(hz > 0.0) || hz >= sample_rate / 2.0 {
            return None;
        }
        let w0 = 2.0 * core::f64::consts::PI * hz / sample_rate;
        Some(Shape {
            cos_w0: w0.cos(),
            alpha: w0.sin() / (2.0 * BUTTERWORTH_Q),
        })
    }
}

/*!
The bass-onset detector behind the animated ground.

WHAT IT ANSWERS, AND FOR WHOM. The Ultraviolet design system's `Ground` is a
wave field that only moves when a kick lands (`design/files/project/README.md`,
section Motion). Its reference implementation detects that kick in the browser,
off a Web Audio graph -- and a plugin editor is a WebView with no `AudioContext`
fed by the host's audio, so the detection has to happen down here and cross to
the UI as a message. This is that half, and it is the ONLY half that moved: the
field itself is ported verbatim in `ui-kit/src/lib/field.js`.

THE NUMBERS ARE THE DESIGN SYSTEM'S, NOT OURS. Band, envelope times, window,
ratio, re-arm, floor and refractory are the detector row of that Motion table,
and `design/files/project/components/ground.js` is the implementation they were
measured on. Retuning any of them changes how the background reads against a
mix, which is a design decision and does not belong in a DSP file.

WHY NOT REUSE THE SIDE-CHAIN'S FOLLOWER, which already detects onsets and is
forty lines away. Because it is BROADBAND: `sc-core`'s `Follower` takes
`max(|l|, |r|)` and fires on any loud transient, so a snare, a clipped vocal or
a loud hi-hat all move it. That is right for a ducker -- the user chose the key
signal and means it -- and wrong here, where the design asks specifically for
"onsets in 20-80 Hz" and a background that answers a guitar is a background
that never sits still. The two detectors want different questions asked, so
they stay two detectors.

TWO STAGES OF SMOOTHING, AND THEY ARE NOT THE SAME STAGE TWICE.

  rms     20 ms of the band's power, so one waveform cycle at 50 Hz reads as
          one level instead of a sine's worth of zero crossings
  env     attack 5 ms / release 150 ms over that level -- the hump whose rising
          edge IS the onset
  mean    300 ms of `env`, the thing the threshold is relative to

The threshold being RELATIVE is the whole trick, and it is why there is no
"sensitivity" parameter. `env > 1.8 * mean` asks "is there more bass right now
than there has been lately", which is a question with the same answer on a
quiet dub track and a loud master; an absolute threshold would need a knob per
song. `floor` exists only so that silence, where the mean is ~0 and any ratio
is infinite, does not fire.

RMS IS ONE-POLE, NOT A SLIDING WINDOW -- the one place this deviates in shape
from the reference. The reference polls a Web Audio analyser every 10 ms and
takes a true boxcar RMS over the last 20 ms of its buffer. A boxcar down here
would mean a ring buffer of up to 20 ms of samples per plugin instance, and
this runs per sample on the audio thread. A one-pole on x squared with the same
20 ms time constant has the same settling behaviour and costs two multiplies
and no memory. The onset instant it produces differs by well under a frame,
which is the only resolution the ground can show.

THE THREAD RULES. `push` runs on the audio thread and does not allocate, lock or
branch on anything but its own state. `fires` and `strength` are read from the
message thread; the counter is what makes that safe without a lock -- see
`lib.rs`, where the pair is published as two atomics for the same reason
`sc_core_fires` is a counter rather than a flag.
*/

use crate::biquad::Biquad;

/// Band, Hz. One 12 dB/oct Butterworth section at each end.
pub(crate) const BAND_LO_HZ: f64 = 20.0;
pub(crate) const BAND_HI_HZ: f64 = 80.0;
/// Power-averaging time for the band's RMS. One cycle of a 50 Hz fundamental
/// plus margin, so a sine reads as a level rather than as its own zero
/// crossings. See the header on why this is a one-pole.
const RMS_MS: f64 = 20.0;
/// Envelope attack. Short, so the edge is not late behind the transient.
const ATTACK_MS: f64 = 5.0;
/// Envelope release. Long enough that one kick is one hump.
const RELEASE_MS: f64 = 150.0;
/// The running mean the threshold is relative to.
const WINDOW_MS: f64 = 300.0;
/// Onset when the envelope exceeds this multiple of the running mean.
const RATIO: f64 = 1.8;
/// Re-arm when it falls back below this multiple. Between `REARM` and `RATIO`
/// the detector is neither firing nor re-arming, which is the hysteresis that
/// stops a level sitting exactly on the threshold from chattering.
const REARM: f64 = 1.2;
/// Ignore anything below this envelope. Guards the ratio in near-silence.
pub(crate) const FLOOR: f64 = 0.01;
/// Minimum gap between onsets, ms.
const REFRACTORY_MS: f64 = 120.0;

/// Reported strength is clamped to this range: a kick that only just crossed
/// the threshold still has to move the field visibly, and the loudest one must
/// not exceed what the field was tuned for (`gain` assumes `s` in 0..1).
const STRENGTH_MIN: f64 = 0.3;
const STRENGTH_MAX: f64 = 1.0;
/// The reference's scale factor (`0.6 * sqrt(r / ratio)`).
const STRENGTH_SCALE: f64 = 0.6;
/// Below this the running mean is treated as silence and the ratio is taken as
/// this instead of dividing by ~0.
const MEAN_EPSILON: f64 = 1e-6;
const SILENT_RATIO: f64 = 10.0;

/// One-pole coefficient for a time constant in ms at a sample rate.
///
/// The same `1 - exp(-1 / (ms * sr / 1000))` the side-chain's follower uses,
/// and guarded the same way: a zero or absurd sample rate would otherwise
/// produce a coefficient above 1, and a one-pole with a coefficient above 1
/// oscillates instead of smoothing.
fn coeff(ms: f64, sample_rate: f64) -> f64 {
    let n = ms * sample_rate / 1000.0;
    if !(n > 0.0) {
        return 1.0;
    }
    (1.0 - (-1.0 / n).exp()).clamp(0.0, 1.0)
}

/// An onset, as the ground needs it.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Onset {
    /// Kick strength, 0.3..1 -- what `Field.trigger` takes.
    pub strength: f32,
}

#[derive(Clone, Copy, Debug)]
pub struct Detector {
    /// One band per channel. Two pairs rather than one, because the band has to
    /// come BEFORE the rectifier -- see `next`.
    band: [Band; 2],
    /// Mean square of the band, smoothed over `RMS_MS`.
    power: f64,
    env: f64,
    mean: f64,
    rms_c: f64,
    attack_c: f64,
    release_c: f64,
    window_c: f64,
    /// False between an onset and the envelope falling back under `REARM`.
    armed: bool,
    /// Samples still to wait before another onset counts.
    refractory_left: f64,
    refractory_samples: f64,
}

impl Detector {
    pub fn new(sample_rate: f64) -> Self {
        let mut d = Detector {
            band: [Band::new(sample_rate), Band::new(sample_rate)],
            power: 0.0,
            env: 0.0,
            mean: 0.0,
            rms_c: 1.0,
            attack_c: 1.0,
            release_c: 1.0,
            window_c: 1.0,
            armed: true,
            refractory_left: 0.0,
            refractory_samples: 0.0,
        };
        d.set_sample_rate(sample_rate);
        d
    }

    pub fn set_sample_rate(&mut self, sample_rate: f64) {
        for b in &mut self.band {
            b.set_sample_rate(sample_rate);
        }
        self.rms_c = coeff(RMS_MS, sample_rate);
        self.attack_c = coeff(ATTACK_MS, sample_rate);
        self.release_c = coeff(RELEASE_MS, sample_rate);
        self.window_c = coeff(WINDOW_MS, sample_rate);
        self.refractory_samples = (REFRACTORY_MS * sample_rate / 1000.0).max(0.0);
        self.reset();
    }

    /// Forget everything. Called on a transport reset, so a stale hump does not
    /// fire an onset the moment playback starts again.
    pub fn reset(&mut self) {
        for b in &mut self.band {
            b.reset();
        }
        self.power = 0.0;
        self.env = 0.0;
        self.mean = 0.0;
        self.armed = true;
        self.refractory_left = 0.0;
    }

    /// The smoothed band level. Not used by the ground, but it is what makes a
    /// misbehaving detector diagnosable from a test rather than by eye.
    pub fn level(&self) -> f64 {
        self.env
    }

    /// Feed one frame. Returns the onset on the sample it lands, else `None`.
    ///
    /// THE BAND COMES BEFORE THE RECTIFIER, and getting that order wrong is
    /// silent rather than loud -- which is why it is spelled out here. The
    /// side-chain's broadband follower starts with `max(|l|, |r|)`, and copying
    /// that opening move into a band-limited detector destroys the very thing
    /// the band selects: `|sin(2*pi*60*t)|` is a rectified sine, which has a DC
    /// term and harmonics at 120 Hz and up, and NO energy at 60 Hz at all. Fed
    /// through a 20-80 Hz band it comes out at a fraction of its real level,
    /// the level FALLS as the kick's pitch rises, and the detector reads as
    /// merely insensitive rather than as wrong. Filter the signed signal;
    /// rectify afterwards, which the squaring below does anyway.
    ///
    /// The channel MAXIMUM, taken after the band, is kept for the reason
    /// `sc-core` states: a kick panned off centre halves in a sum and would
    /// make the threshold read the panner. This is where it costs a second
    /// filter pair, and the reference implementation does not pay it -- a Web
    /// Audio `AnalyserNode` down-mixes to mono before the RMS. Ten flops a
    /// sample is a cheap price for a hard-panned kick reading the same as a
    /// centred one, and it changes no timing: the sections are identical.
    pub fn next(&mut self, l: f64, r: f64) -> Option<Onset> {
        /* A NaN anywhere in the input must not poison the detector for the rest
         * of the session. A NaN envelope compares false against everything, so
         * `armed` would stay true, the threshold test would never pass, and the
         * ground would be dead until the plugin was reloaded -- silently. And a
         * NaN reaching a biquad poisons its state, not just this sample, so it
         * is stopped here rather than after the band. */
        let clean = |v: f64| {
            if v.is_finite() {
                v
            } else {
                0.0
            }
        };
        let bl = self.band[0].next(clean(l)).abs();
        let br = self.band[1].next(clean(r)).abs();
        let band = if bl > br { bl } else { br };
        /* A denormal or an unstable section would show up here rather than
         * further down, where it would be indistinguishable from silence. */
        let sq = if band.is_finite() { band * band } else { 0.0 };
        self.power += (sq - self.power) * self.rms_c;
        let rms = if self.power > 0.0 { self.power.sqrt() } else { 0.0 };

        let c = if rms > self.env {
            self.attack_c
        } else {
            self.release_c
        };
        self.env += (rms - self.env) * c;

        /* The mean is compared BEFORE this sample's envelope folds into it. The
         * reference does the same, and it matters: a kick loud enough to move a
         * 300 ms mean on one sample would otherwise raise its own threshold. */
        let mean = self.mean;
        self.mean += (self.env - self.mean) * self.window_c;

        if self.refractory_left > 0.0 {
            self.refractory_left -= 1.0;
        }

        if !self.armed && self.env < mean * REARM {
            self.armed = true;
        }

        if self.armed
            && self.env > FLOOR
            && self.env > mean * RATIO
            && self.refractory_left <= 0.0
        {
            self.armed = false;
            self.refractory_left = self.refractory_samples;
            let ratio = if mean > MEAN_EPSILON {
                self.env / mean
            } else {
                SILENT_RATIO
            };
            let strength =
                (STRENGTH_SCALE * (ratio / RATIO).sqrt()).clamp(STRENGTH_MIN, STRENGTH_MAX);
            return Some(Onset {
                strength: strength as f32,
            });
        }
        None
    }
}

/// The 20-80 Hz band: one 12 dB/oct Butterworth section at each end.
///
/// A named pair rather than two loose fields, because there is one per channel
/// and "which high-pass goes with which low-pass" is not a question worth
/// leaving open.
#[derive(Clone, Copy, Debug)]
pub(crate) struct Band {
    hp: Biquad,
    lp: Biquad,
}

impl Band {
    pub(crate) fn new(sample_rate: f64) -> Self {
        Band {
            hp: Biquad::highpass(BAND_LO_HZ, sample_rate),
            lp: Biquad::lowpass(BAND_HI_HZ, sample_rate),
        }
    }

    fn set_sample_rate(&mut self, sample_rate: f64) {
        self.hp.set_highpass(BAND_LO_HZ, sample_rate);
        self.lp.set_lowpass(BAND_HI_HZ, sample_rate);
    }

    fn reset(&mut self) {
        self.hp.reset();
        self.lp.reset();
    }

    pub(crate) fn next(&mut self, x: f64) -> f64 {
        self.lp.next(self.hp.next(x))
    }
}

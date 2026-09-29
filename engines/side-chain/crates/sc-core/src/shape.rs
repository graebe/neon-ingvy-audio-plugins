/*!
The duck's shape: its curves, and the stage machine that walks them.

THE SHAPE IS A WARP ON TIME, the same invariant `tg-core`'s envelope states:
every stage has the form `f(t)` with `t` running 0..1 across it, so a curve is
not a new set of formulas -- it is one function substituted into the two that
exist:

```text
ATTACK    duck = att_from + (scale - att_from) * shape(t)
RELEASE   duck = rel_from * (1 - shape(t))
```

`duck` is the ATTENUATION, not the gain: 0 is untouched, `scale` is as far
down as this trigger goes. The gain a sample is multiplied by is
`1 - depth * duck`, computed once in `lib.rs`, so nothing here has to know what
Depth is.

`scale` IS THE ATTACK'S TARGET, NOT A FACTOR ON THE OUTPUT. A velocity-scaled
duck is a shallower duck, and the difference matters on a retrigger: `from`
anchoring compares the level the envelope is at against the level it is heading
for, and those two have to be measured on the same scale. Scaling after the
fact would make a soft note's release start from a number the machine never
actually held.

Every shape obeys `shape(0) = 0`, `shape(1) = 1` and is monotonic, so a stage
still starts and ends exactly where it did and still takes the time it was
given. Only the path between changes.

THERE IS NO DIRECTION ARGUMENT, AND THERE WAS ONE.

A fourth curve, `Pump`, was asymmetric -- linear going down and a cubic ease-out
coming back up, from `ducker.c:134-155` -- so `shape` took a `Dir` saying which
way the envelope was travelling. The other three ignored it.

That curve is gone and the argument went with it, rather than staying as
something every caller passes and no curve reads. A parameter that no longer
distinguishes anything is worse than no parameter: the next reader has to work
out that it does nothing, and the one after has to work out whether that was
deliberate.

LINEAR RETURNS `t` UNTOUCHED, and the two expressions above are deliberately
not tidied into a shared `lerp`. `rel_from * (1 - w)` and
`rel_from - rel_from * w` are one number in algebra and two in floating point.
*/

/// The path a stage takes between its endpoints.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
#[repr(i32)]
pub enum Curve {
    Linear = 0,
    Exp = 1,
    SCurve = 2,
}

impl Curve {
    pub fn from_i32(v: i32) -> Self {
        match v {
            1 => Curve::Exp,
            2 => Curve::SCurve,
            _ => Curve::Linear,
        }
    }

    pub const COUNT: i32 = 3;

    /// The wire labels, in enum order. The shell re-declares these for the
    /// host; this is the table it is re-declaring.
    pub const LABELS: [&'static str; 3] = ["Linear", "Exponential", "S-Curve"];
}

/// The bend. The same constant `tg-core/src/envelope.rs` uses, so an
/// exponential reads the same in both plugins: halfway through the stage the
/// envelope is ~82% of the way.
const CURVE_K: f64 = 3.0;
/// `1 - exp(-3)`, spelled out exactly as `tg-core` and the C spell it, so the
/// division is the same division. NOT `1.0 - (-CURVE_K).exp()`.
const DENOM: f64 = 0.95021293163213605;

#[inline]
fn curve_exp(t: f64) -> f64 {
    (1.0 - (-CURVE_K * t).exp()) / DENOM
}

#[inline]
fn curve_exp_inv(w: f64) -> f64 {
    let x = 1.0 - w * DENOM;
    if x <= 1e-12 {
        return 1.0;
    }
    -x.ln() / CURVE_K
}

#[inline]
pub fn shape(curve: Curve, t: f64) -> f64 {
    /* `t <= 0.0` rather than `!(t > 0.0)` would let a NaN through as 1.0 via
     * the next test. Ordered this way a NaN falls out of both comparisons and
     * lands in the match, where every arm is monotone nonsense but bounded --
     * so guard it explicitly instead. */
    if !(t > 0.0) {
        return 0.0;
    }
    if t >= 1.0 {
        return 1.0;
    }
    match curve {
        Curve::Exp => curve_exp(t),
        /* TWO EXPONENTIALS, JOINED, and the first one is MIRRORED. The first
         * half is slow-then-accelerating, the second is the exponential the
         * right way up, so the pair is slow-fast-slow and meets in the middle
         * at the same slope -- Einv'(1) being E'(0). A corner there would be
         * a kink in the gain, audible as surely as a step.
         *
         * `0.5 * curve_exp(2t)` on both halves is the version that was wrong
         * in the Trance Gate for as long as it was: it leaves the floor
         * vertically and hits the ceiling vertically, which is the opposite
         * of what an S-curve does at both ends. */
        Curve::SCurve => {
            if t < 0.5 {
                0.5 * (1.0 - curve_exp(1.0 - 2.0 * t))
            } else {
                0.5 + 0.5 * curve_exp(2.0 * t - 1.0)
            }
        }
        Curve::Linear => t,
    }
}

/// The inverse, which is what lets the curve change mid-duck without a click:
/// the level is re-anchored through it in `set_curve`. Monotonic and analytic
/// for all four.
#[inline]
pub fn shape_inv(curve: Curve, w: f64) -> f64 {
    if !(w > 0.0) {
        return 0.0;
    }
    if w >= 1.0 {
        return 1.0;
    }
    match curve {
        Curve::Exp => curve_exp_inv(w),
        Curve::SCurve => {
            if w < 0.5 {
                0.5 * (1.0 - curve_exp_inv(1.0 - 2.0 * w))
            } else {
                0.5 + 0.5 * curve_exp_inv(2.0 * w - 1.0)
            }
        }
        Curve::Linear => w,
    }
}

/// Which part of the duck is running.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Stage {
    /// No duck. `duck` is 0 and the signal passes untouched.
    Idle,
    /// Triggered, but not yet moving -- this is "when it begins".
    Delay,
    /// Ducking down.
    Attack,
    /// Held at the bottom.
    Hold,
    /// Recovering.
    Release,
}

/// The envelope's whole state. One struct so a reset is one assignment.
#[derive(Clone, Copy, Debug)]
pub struct Env {
    pub stage: Stage,
    /// 0 = untouched, `scale` = as deep as this trigger goes.
    pub duck: f64,
    /// Samples elapsed in the current stage.
    pub pos: f64,
    /// Where `duck` stood when the current stage began. Attack reads it as
    /// `att_from`, Release as `rel_from`.
    pub from: f64,
    /// How deep THIS trigger goes, 0..1, after Vel -> Depth. The attack's
    /// target. Depth itself is a parameter and is applied in `lib.rs`; this is
    /// the part that belongs to the trigger.
    pub scale: f64,
}

impl Default for Env {
    fn default() -> Self {
        Env {
            stage: Stage::Idle,
            duck: 0.0,
            pos: 0.0,
            from: 0.0,
            scale: 1.0,
        }
    }
}

/// The four stage lengths, in samples. Computed per block by the caller, which
/// is where `Time Mode` (ms vs % of cycle) is resolved -- the machine below
/// does not care which, only that they are samples.
#[derive(Clone, Copy, Debug, Default)]
pub struct Stages {
    pub delay: f64,
    pub attack: f64,
    pub hold: f64,
    pub release: f64,
}

impl Env {
    /// Open the gate immediately and forget the trigger. This is what CC 120
    /// and CC 123 do, and what a stopped transport does to the Cycle source.
    pub fn reset(&mut self) {
        *self = Env::default();
    }

    /// Fire. `scale` is how deep this trigger goes, 0..1.
    ///
    /// THE ATTACK STARTS FROM WHERE THE ENVELOPE ACTUALLY IS, not from zero.
    /// A retrigger during the release would otherwise jump the gain back up to
    /// unity for one sample before ducking again -- a click, and the exact
    /// fault `tg-core`'s `att_from` exists to prevent.
    ///
    /// The stage is always `Delay`, even when Delay is zero: `next` skips a
    /// stage with no time left, so there is no zero-length special case here
    /// and no second copy of the "what comes after Delay" decision.
    pub fn trigger(&mut self, scale: f64, _s: &Stages) {
        self.from = self.duck;
        self.scale = scale.clamp(0.0, 1.0);
        self.pos = 0.0;
        self.stage = Stage::Delay;
    }

    /// Begin the recovery from wherever the machine had got to.
    ///
    /// NOT FROM THE FLOOR. Releasing a duck that never reached the bottom -- a
    /// short trigger, or a note-off during the attack -- from `scale` would
    /// step the gain DOWN at the moment it was asked to come up.
    ///
    /// A zero-length release needs no branch here either; `next` turns it into
    /// the cut it is.
    pub fn release(&mut self, _s: &Stages) {
        if matches!(self.stage, Stage::Idle) {
            return;
        }
        self.from = self.duck;
        self.pos = 0.0;
        self.stage = Stage::Release;
    }

    #[inline]
    fn enter(&mut self, stage: Stage) {
        self.pos = 0.0;
        self.from = self.duck;
        self.stage = stage;
    }

    /// Advance one sample and return the attenuation, 0..`scale`.
    ///
    /// `gated` is true while the trigger is still held -- Gate mode with a note
    /// down. Hold then does not expire on its own and only `release()` leaves
    /// it. Every other source passes false and Hold times out.
    ///
    /// `pos` COUNTS SAMPLES ALREADY EMITTED, and a stage is finished when
    /// `pos` reaches its length -- tested BEFORE emitting, not after.
    ///
    /// Testing after is the off-by-one this loop exists to avoid: it emits
    /// `t = 0 .. len/len` inclusive, which is `len + 1` samples per stage, so a
    /// nominal 10/10/10 envelope ran for 33 samples. Tested first, the attack
    /// emits `t = 0 .. (len-1)/len` over exactly `len` samples and the floor
    /// arrives as Hold's first sample -- no sample duplicated, none lost.
    ///
    /// THE LOOP IS WHAT MAKES A ZERO-LENGTH STAGE FREE. Each iteration either
    /// emits and returns, or moves to a strictly later stage, so it cannot spin:
    /// Delay -> Attack -> Hold -> Release -> Idle is four transitions, and an
    /// envelope with every stage at zero takes exactly that many. The bound is
    /// five so that a future stage does not silently turn a missed return into
    /// a hang.
    pub fn next(&mut self, curve: Curve, s: &Stages, gated: bool) -> f64 {
        for _ in 0..5 {
            match self.stage {
                Stage::Idle => {
                    self.duck = 0.0;
                    return 0.0;
                }
                Stage::Delay => {
                    if self.pos < s.delay {
                        self.pos += 1.0;
                        /* The delay holds whatever the last duck was, so a
                         * retrigger during a release does not jump. */
                        return self.duck;
                    }
                    self.enter(Stage::Attack);
                }
                Stage::Attack => {
                    if self.pos < s.attack {
                        let t = self.pos / s.attack;
                        self.duck =
                            self.from + (self.scale - self.from) * shape(curve, t);
                        self.pos += 1.0;
                        return self.duck;
                    }
                    /* The attack is over, so the floor IS reached -- set it
                     * explicitly rather than letting the last shaped value
                     * stand in for it. */
                    self.duck = self.scale;
                    self.enter(Stage::Hold);
                }
                Stage::Hold => {
                    if gated || self.pos < s.hold {
                        self.duck = self.scale;
                        /* A gated hold does not age: the note decides when it
                         * ends, so counting would eventually expire it anyway. */
                        if !gated {
                            self.pos += 1.0;
                        }
                        return self.duck;
                    }
                    self.enter(Stage::Release);
                }
                Stage::Release => {
                    if self.pos < s.release {
                        let t = self.pos / s.release;
                        self.duck = self.from * (1.0 - shape(curve, t));
                        self.pos += 1.0;
                        return self.duck;
                    }
                    self.duck = 0.0;
                    self.from = 0.0;
                    self.pos = 0.0;
                    self.stage = Stage::Idle;
                    return 0.0;
                }
            }
        }
        /* Unreachable with four stages; see the bound's comment. */
        self.duck
    }
}

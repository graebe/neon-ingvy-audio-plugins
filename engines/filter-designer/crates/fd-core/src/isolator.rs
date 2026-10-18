// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The isolator, in front of the nodes: a DJ mixer's three bands, each with its
level and its kill.

```text
  x ─┬─ LP(lower) ── AP(upper) ─────── × Low  ─┐
     └─ HP(lower) ─┬─ LP(upper) ────── × Mid  ─┼─ y
                   └─ HP(upper) ────── × High ─┘
```

The crossovers are Linkwitz-Riley, 24 dB (LR4) or 48 dB (LR8) an octave,
whose low and high sum to an all-pass; the low band goes through the upper
crossover's all-pass too, so all three at 0 dB sum to the all-pass of both
crossovers: flat, but not the input sample for sample. That is why the
isolator starts off, and why switching it on and off fades over 20 ms rather
than jumping.

The band levels glide every sample, so a kill -- the level to nothing -- is
5 ms long and never a click. The crossovers glide on the control grid, in
octaves, and across each sub-block the paths move from the last design to
the new one a sample at a time (Cascade::run_toward), as a node's do: no
coefficient steps, so a crossover sweep puts no lines at multiples of fs/16.
The mid-high crossover never goes below the low-mid one.

A change of Slope is two splits, the old fading out as the new fades in.
LR4 and LR8 are 180 degrees apart at a crossover, so the fade dips there
for its 20 ms: what two crossovers of different order summed must do. The
new split starts from nothing -- a crossover's filters settle within a few
milliseconds, and one warmed first would only deepen that dip, both then
at full strength against each other.
*/

use crate::params::Param;
use crate::SUB;
use ni_dsp::smooth::glide;
use ni_filter::{all_pass, family, Band, Cascade, Family, Prototype, State};

/// The paths: low's two, mid's and high's.
const LP_LO: usize = 0;
const HP_LO: usize = 1;
const AP_HI: usize = 2;
const LP_HI: usize = 3;
const HP_HI: usize = 4;

#[derive(Clone, Copy, Debug)]
struct Split {
    order: usize,
    /// Linkwitz-Riley at `order`: what every design of this split maps.
    proto: Prototype,
    /// The crossovers it was designed at, in Hz, and the rate.
    designed: Option<(f64, f64, f64)>,
    paths: [Cascade; 5],
    /// The design the sub-block starts from, while `moving` toward `paths`.
    from: [Cascade; 5],
    moving: bool,
    lanes: [[State; 5]; 2],
}

impl Split {
    fn new(order: usize) -> Split {
        Split {
            order,
            proto: Prototype::new(Family::LinkwitzRiley, order, 1.0, 60.0),
            designed: None,
            paths: [Cascade::identity(); 5],
            from: [Cascade::identity(); 5],
            moving: false,
            lanes: [[State::default(); 5]; 2],
        }
    }

    /// Designed again where the crossovers moved; across the next sub-block
    /// the paths move there from where they were.
    fn update(&mut self, lo: f64, hi: f64, fs: f64) {
        self.from = self.paths;
        self.moving = false;
        if self.designed == Some((lo, hi, fs)) {
            return;
        }
        let (p, q) = (&self.proto, core::f64::consts::FRAC_1_SQRT_2);
        self.paths[LP_LO] = family(p, Band::LowPass, lo, q, fs).0;
        self.paths[HP_LO] = family(p, Band::HighPass, lo, q, fs).0;
        self.paths[AP_HI] = all_pass(hi, q, self.order / 2, fs);
        self.paths[LP_HI] = family(p, Band::LowPass, hi, q, fs).0;
        self.paths[HP_HI] = family(p, Band::HighPass, hi, q, fs).0;
        self.moving = self.designed.is_some_and(|(_, _, was)| was == fs);
        self.designed = Some((lo, hi, fs));
    }

    /// One sample of `lane`, `t` (0..1] of the way through the sub-block.
    #[inline]
    fn band(&mut self, lane: usize, x: f64, g: &[f64; 3], t: f64) -> f64 {
        let (p, from, moving, s) = (&self.paths, &self.from, self.moving, &mut self.lanes[lane]);
        let mut run = |i: usize, x: f64| if moving { from[i].run_toward(&p[i], t, &mut s[i], x) } else { p[i].run(&mut s[i], x) };
        let low = run(LP_LO, x);
        let low = run(AP_HI, low);
        let rest = run(HP_LO, x);
        let mid = run(LP_HI, rest);
        let high = run(HP_HI, rest);
        g[0] * low + g[1] * mid + g[2] * high
    }

    fn reset(&mut self) {
        self.lanes = [[State::default(); 5]; 2];
    }
}

/// The isolator.
#[derive(Clone, Copy, Debug)]
pub struct Isolator {
    /// How far it is heard: 0 off, 1 on; it moves a fade's step a sample.
    mix: f64,
    /// The crossovers, in octaves, as they glide.
    lo: f64,
    hi: f64,
    /// The bands' levels, linear, as they glide every sample.
    gains: [f64; 3],
    targets: [f64; 3],
    on: bool,
    a: Split,
    b: Split,
    t: f64,
    fading: bool,
    /// Where in the sub-block the next sample is: control() is the grid's
    /// first sample, so this counts from it.
    pos: usize,
}

fn level(db: f64, kill: f64) -> f64 {
    if kill >= 0.5 || db <= -60.0 {
        0.0
    } else {
        10f64.powf(db / 20.0)
    }
}

fn order_of(values: &[f64]) -> usize {
    if values[Param::IsoSlope.index()] >= 0.5 {
        8
    } else {
        4
    }
}

impl Isolator {
    pub fn new(values: &[f64]) -> Isolator {
        let mut i = Isolator {
            mix: 0.0,
            lo: 0.0,
            hi: 0.0,
            gains: [1.0; 3],
            targets: [1.0; 3],
            on: false,
            a: Split::new(order_of(values)),
            b: Split::new(order_of(values)),
            t: 0.0,
            fading: false,
            pos: 0,
        };
        i.read(values);
        i.snap();
        i
    }

    fn read(&mut self, values: &[f64]) {
        let v = |p: Param| values[p.index()];
        self.on = v(Param::IsoOn) >= 0.5;
        self.targets = [
            level(v(Param::IsoLow), v(Param::IsoKillLow)),
            level(v(Param::IsoMid), v(Param::IsoKillMid)),
            level(v(Param::IsoHigh), v(Param::IsoKillHigh)),
        ];
    }

    fn crossovers(values: &[f64]) -> (f64, f64) {
        let lo = values[Param::IsoFreqLo.index()];
        let hi = values[Param::IsoFreqHi.index()].max(lo);
        (lo.log2(), hi.log2())
    }

    fn snap(&mut self) {
        self.gains = self.targets;
        self.mix = if self.on { 1.0 } else { 0.0 };
    }

    /// Off, and faded out: nothing to do.
    pub fn is_idle(&self, values: &[f64]) -> bool {
        self.mix == 0.0 && values[Param::IsoOn.index()] < 0.5
    }

    /// One control step: the targets read, the crossovers glided and the
    /// splits designed again, a fade started when Slope changed.
    pub fn control(&mut self, values: &[f64], c: f64, fs: f64) {
        self.pos = 0;
        self.read(values);
        let (lo, hi) = Isolator::crossovers(values);
        if self.mix == 0.0 {
            /* Silent: it starts where it is going. */
            (self.lo, self.hi) = (lo, hi);
        } else {
            self.lo = glide(self.lo, lo, c);
            self.hi = glide(self.hi, hi, c);
        }
        let order = order_of(values);
        if !self.fading && order != self.a.order {
            if self.mix == 0.0 {
                self.a = Split::new(order);
            } else {
                self.b = Split::new(order);
                self.t = 0.0;
                self.fading = true;
            }
        }
        if self.mix > 0.0 || self.on {
            let (flo, fhi) = (self.lo.exp2(), self.hi.exp2());
            self.a.update(flo, fhi, fs);
            if self.fading {
                self.b.update(flo, fhi, fs);
            }
        }
    }

    /// `l` and `r` through the isolator, in place: `c` a level glide's
    /// coefficient a sample, `step` a fade's progress a sample. `mono`: `r`
    /// is `l`'s copy, and only `l` is worked out.
    pub fn run(&mut self, l: &mut [f64], r: &mut [f64], c: f64, step: f64, mono: bool) {
        if self.mix == 0.0 && !self.on {
            return;
        }
        for (x, y) in l.iter_mut().zip(r.iter_mut()) {
            for (g, t) in self.gains.iter_mut().zip(self.targets) {
                *g = glide(*g, t, c);
            }
            self.mix = if self.on { (self.mix + step).min(1.0) } else { (self.mix - step).max(0.0) };
            self.pos += 1;
            /* Held at the sub-block's end: a block that skipped the grid
             * (the engine transparent) leaves `pos` where it was. */
            let t = (self.pos as f64 / SUB as f64).min(1.0);
            let (dl, dr) = (*x, *y);
            let mut wl = self.a.band(0, dl, &self.gains, t);
            let mut wr = if mono { wl } else { self.a.band(1, dr, &self.gains, t) };
            if self.fading {
                let bl = self.b.band(0, dl, &self.gains, t);
                let br = if mono { bl } else { self.b.band(1, dr, &self.gains, t) };
                let s = (core::f64::consts::FRAC_PI_2 * self.t).sin();
                let (wa, wb) = (1.0 - s * s, s * s);
                wl = wa * wl + wb * bl;
                wr = wa * wr + wb * br;
                self.t += step;
                if self.t >= 1.0 {
                    self.a = self.b;
                    self.fading = false;
                }
            }
            *x = (1.0 - self.mix) * dl + self.mix * wl;
            *y = (1.0 - self.mix) * dr + self.mix * wr;
        }
        if self.mix == 0.0 {
            /* Faded out: it lets go of what it held. */
            self.a.reset();
            self.b.reset();
            self.fading = false;
        }
    }

    /// The panic.
    pub fn reset(&mut self, values: &[f64], fs: f64) {
        self.read(values);
        self.snap();
        (self.lo, self.hi) = Isolator::crossovers(values);
        self.a = Split::new(order_of(values));
        self.b = Split::new(order_of(values));
        self.fading = false;
        self.a.update(self.lo.exp2(), self.hi.exp2(), fs);
        self.a.moving = false;
    }

    pub fn flush(&mut self) {
        for lane in self.a.lanes.iter_mut().chain(self.b.lanes.iter_mut()) {
            for s in lane {
                s.flush(1e-30);
            }
        }
    }

    pub fn is_silent(&self) -> bool {
        self.a.lanes.iter().chain(self.b.lanes.iter()).flatten().all(State::is_empty)
    }

    /// How far the isolator is heard, 0..1.
    pub fn mix(&self) -> f64 {
        self.mix
    }
}

/// The isolator's bands as the picture draws them, for `values` at `fs`: the
/// low, mid and high paths' cascades at their crossovers (low first through
/// its all-pass: the product of the two), and the levels.
pub fn picture(values: &[f64], fs: f64) -> ([[Cascade; 2]; 3], [f64; 3]) {
    let mut s = Split::new(order_of(values));
    let (lo, hi) = Isolator::crossovers(values);
    s.update(lo.exp2(), hi.exp2(), fs);
    let v = |p: Param| values[p.index()];
    let levels = [
        level(v(Param::IsoLow), v(Param::IsoKillLow)),
        level(v(Param::IsoMid), v(Param::IsoKillMid)),
        level(v(Param::IsoHigh), v(Param::IsoKillHigh)),
    ];
    (
        [[s.paths[LP_LO], s.paths[AP_HI]], [s.paths[HP_LO], s.paths[LP_HI]], [s.paths[HP_LO], s.paths[HP_HI]]],
        levels,
    )
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The one structure: TPT state-variable sections with mixed outputs, in series.

A SECOND-ORDER SECTION is two trapezoidal integrators in a loop with damping
`k = 1/Q`, the delay-free loop solved in closed form (Zavalishin, The Art of
VA Filter Design; the paper's svf.rs is the same loop in f32):

```text
  hp = (x - (k + g)·s1 - s2) / (1 + g·(k + g))
  bp = g·hp + s1          s1 = bp + g·hp
  lp = g·bp + s2          s2 = lp + g·bp
  y  = m_hp·hp + m_bp·bp + m_lp·lp
```

On the bilinear transform's axis `u = (z - 1)/(z + 1)` -- which takes the
digital frequency ω to `j·tan(ω/2)` -- it is exactly

```text
  H(u) = (m_hp·u² + m_bp·g·u + m_lp·g²) / (u² + k·g·u + g²)
```

so every second-order transfer with its poles in the left half plane is one
section, by its mix. A FIRST-ORDER SECTION is the TPT one-pole,
`(m_hp·u + m_lp·g) / (u + g)`.

Both stay stable while g and k move, for any g > 0 and k > 0: the state is
the integrators' and nothing in the loop is recursive through a coefficient
that changed.
*/

use num_complex::Complex64;

/// The most sections a cascade holds: a band pass or notch of order 8.
pub const MAX_SECTIONS: usize = 8;

/// One section's coefficients.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Section {
    g: f64,
    k: f64,
    hp: f64,
    bp: f64,
    lp: f64,
    first_order: bool,
    /// 1 / (1 + g·(g + k)) for a second-order section, g / (1 + g) for a
    /// first-order one: the loop's solution, once.
    d: f64,
}

impl Default for Section {
    /// A wire: y = x.
    fn default() -> Section {
        Section::first(1.0, 1.0, 1.0)
    }
}

impl Section {
    /// A second-order section: `g` > 0 its frequency on the warped axis, `k`
    /// > 0 its damping (1/Q), and the mix of its outputs.
    pub fn second(g: f64, k: f64, hp: f64, bp: f64, lp: f64) -> Section {
        Section { g, k, hp, bp, lp, first_order: false, d: 1.0 / (1.0 + g * (g + k)) }
    }

    /// A first-order section: `g` > 0 its frequency on the warped axis, and
    /// the mix of its high- and low-pass outputs.
    pub fn first(g: f64, hp: f64, lp: f64) -> Section {
        Section { g, k: 0.0, hp, bp: 0.0, lp, first_order: true, d: g / (1.0 + g) }
    }

    pub fn g(&self) -> f64 {
        self.g
    }

    pub fn k(&self) -> f64 {
        self.k
    }

    /// The mix (m_hp, m_bp, m_lp).
    pub fn mix(&self) -> (f64, f64, f64) {
        (self.hp, self.bp, self.lp)
    }

    pub fn is_first_order(&self) -> bool {
        self.first_order
    }

    /// One sample through the section, `s` its two integrators.
    #[inline]
    pub fn run(&self, s: &mut [f64; 2], x: f64) -> f64 {
        if self.first_order {
            let v = (x - s[0]) * self.d;
            let lp = v + s[0];
            s[0] = lp + v;
            self.hp * (x - lp) + self.lp * lp
        } else {
            let (g, k) = (self.g, self.k);
            let hp = (x - (k + g) * s[0] - s[1]) * self.d;
            let v1 = g * hp;
            let bp = v1 + s[0];
            s[0] = bp + v1;
            let v2 = g * bp;
            let lp = v2 + s[1];
            s[1] = lp + v2;
            self.hp * hp + self.bp * bp + self.lp * lp
        }
    }

    /// Samples its free response takes to fall by `db` dB: its slower pole's.
    /// A pole at `u` on the warped axis is the digital pole z = (1 + u)/(1 - u),
    /// and ln|z|² = ln(1 + 4·Re u / |1 - u|²), exact however near 1 |z| is.
    pub fn decay_samples(&self, db: f64) -> f64 {
        let g = self.g;
        let poles = if self.first_order {
            [Complex64::new(-g, 0.0); 2]
        } else {
            let h = 0.5 * self.k;
            let root = Complex64::new(h * h - 1.0, 0.0).sqrt();
            [g * (root - h), g * (-root - h)]
        };
        let ln_r2 = poles.iter().map(|u| (4.0 * u.re / (1.0 - u).norm_sqr()).ln_1p()).fold(f64::NEG_INFINITY, f64::max);
        if ln_r2 < 0.0 {
            db * core::f64::consts::LN_10 / (10.0 * -ln_r2)
        } else {
            f64::INFINITY
        }
    }

    /// The transfer at `u` on the warped axis.
    pub fn transfer(&self, u: Complex64) -> Complex64 {
        let g = self.g;
        if self.first_order {
            (u * self.hp + self.lp * g) / (u + g)
        } else {
            (u * u * self.hp + u * (self.bp * g) + self.lp * g * g) / (u * u + u * (self.k * g) + g * g)
        }
    }
}

/// The integrators of a cascade's sections.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct State {
    s: [[f64; 2]; MAX_SECTIONS],
}

impl State {
    /// Empty: what a section starts from.
    pub fn reset(&mut self) {
        self.s = [[0.0; 2]; MAX_SECTIONS];
    }

    /// Integrators that have decayed below `floor` become exactly 0: what a
    /// block's end does, so a silent filter never reaches the denormals.
    pub fn flush(&mut self, floor: f64) {
        for s in self.s.iter_mut().flatten() {
            if s.abs() < floor {
                *s = 0.0;
            }
        }
    }

    /// Whether every integrator is 0.
    pub fn is_empty(&self) -> bool {
        self.s.iter().flatten().all(|s| *s == 0.0)
    }
}

/// Sections in series, and a gain after them.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Cascade {
    sections: [Section; MAX_SECTIONS],
    len: usize,
    gain: f64,
}

impl Default for Cascade {
    fn default() -> Cascade {
        Cascade::identity()
    }
}

impl Cascade {
    /// No sections and unity gain: y = x, exactly.
    pub const fn identity() -> Cascade {
        Cascade {
            sections: [Section { g: 1.0, k: 0.0, hp: 1.0, bp: 0.0, lp: 1.0, first_order: true, d: 0.5 }; MAX_SECTIONS],
            len: 0,
            gain: 1.0,
        }
    }

    /// Add a section; one past [`MAX_SECTIONS`] is dropped (no design here
    /// makes one).
    pub fn push(&mut self, s: Section) {
        if self.len < MAX_SECTIONS {
            self.sections[self.len] = s;
            self.len += 1;
        }
    }

    pub fn set_gain(&mut self, gain: f64) {
        self.gain = gain;
    }

    pub fn gain(&self) -> f64 {
        self.gain
    }

    pub fn sections(&self) -> &[Section] {
        &self.sections[..self.len]
    }

    pub fn len(&self) -> usize {
        self.len
    }

    pub fn is_empty(&self) -> bool {
        self.len == 0
    }

    /// Whether this is exactly y = x.
    pub fn is_identity(&self) -> bool {
        self.len == 0 && self.gain == 1.0
    }

    /// One sample through every section and the gain.
    #[inline]
    pub fn run(&self, st: &mut State, x: f64) -> f64 {
        let mut y = x;
        for (sec, s) in self.sections[..self.len].iter().zip(st.s.iter_mut()) {
            y = sec.run(s, y);
        }
        y * self.gain
    }

    /// The transfer at the digital frequency `w` (radians a sample, 0..π):
    /// the response the cascade has as it runs. At π itself the warped axis
    /// is infinite, so the frequency is held a hair under it.
    pub fn response(&self, w: f64) -> Complex64 {
        let t = (0.5 * w.clamp(0.0, core::f64::consts::PI * (1.0 - 1e-9))).tan();
        let u = Complex64::new(0.0, t);
        self.sections().iter().fold(Complex64::new(self.gain, 0.0), |h, s| h * s.transfer(u))
    }

    /// Samples its free response takes to fall by `db` dB, bounded above:
    /// each section's decay in turn, as the series rings one into the next.
    pub fn decay_samples(&self, db: f64) -> f64 {
        self.sections().iter().map(|s| s.decay_samples(db)).sum()
    }

    /// The magnitude in dB at `hz` for a rate `fs`, floored at -300 dB.
    pub fn db(&self, hz: f64, fs: f64) -> f64 {
        let m = self.response(2.0 * core::f64::consts::PI * hz / fs).norm();
        20.0 * m.max(1e-15).log10()
    }
}

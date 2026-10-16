// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
Filter design for every Neon Ingvy engine (docs/adr/0005-iir-filter-design.md):
the classic analog families at any order up to eight, the equaliser's shapes,
and one structure to run them all in.

ONE STRUCTURE. Every filter is a [`Cascade`] of TPT (zero-delay feedback)
state-variable sections whose three outputs are mixed,

```text
  y = m_hp·hp + m_bp·bp + m_lp·lp,     g = tan(·),  k = 1/Q
```

plus a TPT one-pole for an odd order. The three outputs span every
second-order numerator, so the one structure is a low-pass, a high-pass, a
band pass, a notch, a bell, a shelf or an all-pass by its mix alone; and it
stays stable while its coefficients move, for any g > 0 and k > 0, which a
direct-form biquad does not.

THE FAMILIES ([`Family`], [`Prototype`]): Butterworth, Bessel, Chebyshev I and
II, Elliptic and Linkwitz-Riley, each an analog low-pass prototype of poles
and zeros, normalised so that 1 rad/s is its -3 dB point (-6 dB for
Linkwitz-Riley, so a low and a high cut at one frequency sum flat).
[`family`] maps one onto the bilinear transform's warped axis as a low or high
cut, a band pass or a notch, with ONE prewarp for the whole filter: a cascade
is the bilinear transform of its prototype only when every section shares one
frequency scaling.

THE EQUALISER'S SHAPES ([`bell`], [`low_shelf`], [`high_shelf`], [`tilt`]) are
matched to their analog magnitude at DC, at Nyquist and at their frequency
(Vicanek, Matched Second Order Digital Filters, 2016), so a shelf at 15 kHz is
not squeezed by the bilinear transform; the poles are the analog ones,
impulse-invariant. [`all_pass`] is the bilinear transform of Butterworth's
poles, which is what a phase shifter wants.

THE RESPONSE IS THE CASCADE'S OWN ([`Cascade::response`]): the transfer of the
sections as they run, evaluated at a frequency -- what an editor draws, never a
second model of it.

THE COST HAS TWO TIERS. A [`Prototype`] (family, order, ripple, stopband) is
the dear part and changes rarely; mapping it to sections at a frequency is a
handful of products and one `tan`. Nothing here allocates: every cascade holds
its [`MAX_SECTIONS`] in place.
*/

mod cascade;
mod design;
mod elliptic;
mod proto;

pub use cascade::{Cascade, Section, State, MAX_SECTIONS};
pub use design::{all_pass, bell, family, family_matched, high_shelf, low_shelf, tilt, Band, MAX_HZ_FRACTION, MIN_HZ};
pub use proto::{Family, Prototype, MAX_ORDER};

/// Complex numbers, as the response is given.
pub use num_complex::Complex64;

#[cfg(test)]
mod tests;

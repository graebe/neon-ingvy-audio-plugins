# 0004. Filters are TPT state-variable sections, designed here

- Status: accepted, 2026-10-12 (the owner approved the design and asked for
  its implementation)
- Decided by: Torben Gräber
- For: NI Filter Designer ([concept](../concepts/filter-designer.md)), and
  later every product that filters
- Departs from: [0003](0003-established-rust-crates.md), for filter design only

## Context

NI Filter Designer needs:

- Six filter families (Butterworth, Bessel, Chebyshev I and II, Elliptic,
  Linkwitz-Riley) at orders 1 to 8.
- Every response type: high-pass, low-pass, band-pass, notch (band-stop), bell, shelves, all-pass, tilt.
- Resonance, ripple and stopband.
- An EQ Three style isolator.

All of this must:

- automate without clicks;
- stay stable at 10 Hz at 192 kHz and near Nyquist;
- run without allocating on the audio thread;
- publish its own exact response for the editor to draw, because the kit's
  rule is "draw what the engine renders" (`plugins/_shared/ui/src/Plot.h`).

ADR 0003 prefers established crates to hand-written code. The crates that
exist do not fit:

- **`biquad`** (already used by Bit-Crusher) is direct form 1 with the cookbook
  types only. It has no families, no orders beyond two, and no response
  evaluation, and a direct form misbehaves under fast coefficient changes.
- **`fundsp`** has an SVF and bells but no Chebyshev, Elliptic or Bessel
  designs. It is a graph framework, not a filter library.
- **SciPy-style crates (`sci-rs`, `iir_filters`)** design zpk filters on the
  heap. They are not real-time safe, and they have no stable section identity
  to glide between.

## Decision

1. **One structure for every filter:** a cascade of TPT (zero-delay feedback)
   state-variable sections with mixed outputs, `y = m_hp·hp + m_bp·bp + m_lp·lp`,
   plus a TPT one-pole for odd orders.
   - The three outputs span every second-order numerator, so one structure
     realises every type.
   - It is modulation-safe for any g > 0 and k > 0.
   - Zeros on the jω axis land exactly on the unit circle.
2. **One prewarp per filter, not per section.** A cascade is the bilinear
   transform of its analog prototype only when every section shares one
   frequency scaling. Freq is the −3 dB point in every family; for
   Linkwitz-Riley it is −6 dB.
3. **Matched-magnitude coefficients (Vicanek, 2016) for the equaliser's
   shapes:** the Bell, the shelves (each second-order section of a
   Holters-Zölzer cascade, and the first-order one) and the tilt. The poles
   are the analog ones, impulse-invariant; the numerator meets the analog
   magnitude at DC, at Nyquist and at Freq. Measured (ni-filter's tests): a
   bell below a quarter of the rate stays within 0.35 dB of its analog shape
   everywhere; a broad bell above it is exact at the three points and within
   2.5 dB on its skirt (2.3 dB at 19 kHz, Q 1, 44.1 kHz), where the bilinear
   transform would squeeze it whole. Where no real numerator meets the three
   points the design falls back to the prewarped bilinear transform.
   Amended 2026-10-13: a shelf section from fs/8 up is whichever of the
   matched (either numerator sign at Nyquist) and the bilinear design strays
   less from the analog shape on a 24-point grid. A high shelf's pole f0·√A
   can lie past Nyquist, and the matched section then strayed up to 13.6 dB
   between its three points. The
   family designs -- high-pass, low-pass, band-pass and notch of every order -- stay on
   the prewarped bilinear transform, the correct mapping for maximally flat
   and equiripple responses, with their -3 dB edges exact. No oversampling
   in v1.
4. **f64 inside** for coefficients and state; f32 only at the I/O and in the
   editor's curves.
5. **The design code is written here**, in a new product-free crate,
   `engines/shared/crates/ni-filter`, of about 600–800 lines:
   - prototypes in closed form -- Orfanidis for the elliptic -- and Bessel's
     as the reverse Bessel polynomial's roots, a constant table for orders
     1 to 8 (amended 2026-10-13: the Durand-Kerner solve it replaced ran on
     the audio thread; the test holds the table to the polynomial);
   - frequency transforms and section assembly;
   - response evaluation.

   It uses crates already in the workspace: `num-complex` for the design math
   and `realfft` for the analyzer, with `assert_no_alloc` and `proptest` in its
   tests.

### Amendment 2026-10-13: matched cuts

The owner approved a matched option for the high-pass and low-pass, in the
Bit-Crusher session and in this one.

- **Why.** A bilinear low-pass high in the band steepens into the
  transform's zero at Nyquist. At 44.1 kHz, a 48 dB/oct filter at 18 kHz
  with +24 dB of peak was 47 dB off its analog shape.
- **What.** `ni_filter::family_matched` designs each section from its analog
  one. The poles are impulse-invariant. The numerator follows Vicanek: a
  low-pass with b2 = 0, a high-pass b0·(1 − z⁻¹)², and a three-point match
  where a section has stopband zeros. A first-order section is matched at
  DC and the cutoff. Where a matched section cannot reach the cutoff, or
  strays more than the bilinear one, the bilinear section is used. Every
  section meets its analog magnitude at the cutoff, so the cutoff is exactly
  the family's −3 dB (−6 dB for Linkwitz-Riley), raised by the resonance.
- **Measured** with Bit-Crusher's tests on a Butterworth low-pass of order
  2, 4 and 8, up to 24 dB of peak, at 44.1, 48 and 96 kHz, cutoffs up to
  20 kHz:
  - within 2.1 dB of the analog filter above −6 dB;
  - within 4.5 dB to −20 dB;
  - within a quarter of the depth to −60 dB.
- **In the Filter Designer** it is the global switch Matched Cuts,
  appended at index 174 so the earlier indices hold. It is off by default,
  which keeps the exact bilinear family designs.
- **Bit-Crusher** moves its tone filter onto it in its own branch.

The resonance now raises the sharpest section's Q by exactly Q/0.7071, with
a guard at 10⁴ instead of the general clamp at 40. An order-8 Butterworth
with 24 dB of peak needs 40.6.

A shelf section from fs/8 up is whichever of its matched and bilinear
designs strays less. A matched high shelf's pole can lie past Nyquist; at
44.1 kHz, an 18 kHz shelf was 13.6 dB off its analog shape at 8.5 kHz.

### Crates considered (2026-10-13)

The owner asked for established audio crates where they exist. None covers
what `ni-filter` does:

- **sci-rs** implements Butterworth only; its Chebyshev, elliptic and Bessel
  designs are `todo!()`.
- **iir_filters** is Butterworth only.
- **ellip** has no Jacobi elliptic functions of complex argument, which the
  elliptic poles need.
- **fundsp**'s TPT state-variable filter is bound to its audio graph and
  has no first-order section.

None designs without allocating on the audio thread. The owner's decision:
stay with our own implementation for now.

## Consequences

- **This is the filter mathematics ADR 0003 calls domain code** ("the DSP
  stays domain code"). It is owned, tested against analytic formulas, and
  audited here. The concept's test table is the bar: analytic magnitudes to
  1e-9, digital response equal to the prewarped analog one, and the drawn
  curve equal to an impulse response's FFT.
- **Bit-Crusher's private filters** (`bc-core/src/filters.rs`, direct form 1
  through `biquad`) can move onto `ni-filter` now that it has matched cuts
  (amendment above). Its golden renders will have to be re-recorded, with
  the reason stated.
- **Existing crates may still serve as oracles.** A SciPy-style design crate
  can be a test-only dev-dependency once `cargo deny` passes it.
- **A linear-phase or oversampled mode can be added later** without breaking
  saved sets, because VST3 IDs derive from the parameter keys.

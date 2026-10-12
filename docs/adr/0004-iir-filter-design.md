# 0004. Filters are TPT state-variable sections, designed here

- Status: proposed, 2026-10-12
- Decided by: Torben Gräber (pending)
- For: NI Filter Designer ([concept](../concepts/filter-designer.md)), and
  later every product that filters
- Departs from: [0003](0003-established-rust-crates.md), for filter design only

## Context

NI Filter Designer needs:

- Six filter families (Butterworth, Bessel, Chebyshev I and II, Elliptic,
  Linkwitz-Riley) at orders 1 to 8.
- Every response type: cuts, band pass, notch, Bell, shelves, all-pass, tilt.
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
3. **Matched-magnitude coefficients (Vicanek, 2016) for the second-order EQ
   shapes:** Bell, first and second-order shelves, tilt, order-1 notch and
   band pass. They follow the analog shape up to Nyquist with no
   oversampling, no latency and no phase change. The family designs stay on
   the prewarped bilinear transform, which is the correct mapping for
   maximally flat and equiripple responses. No oversampling in v1.
4. **f64 inside** for coefficients and state; f32 only at the I/O and in the
   editor's curves.
5. **The design code is written here**, in a new product-free crate,
   `engines/shared/crates/ni-filter`, of about 600–800 lines:
   - prototypes in closed form: Orfanidis for the elliptic, a constant table
     for Bessel;
   - frequency transforms and section assembly;
   - response evaluation.

   It uses crates already in the workspace: `num-complex` for the design math
   and `realfft` for the analyzer, with `assert_no_alloc` and `proptest` in its
   tests.

## Consequences

- **This is the filter mathematics ADR 0003 calls domain code** ("the DSP
  stays domain code"). It is owned, tested against analytic formulas, and
  audited here. The concept's test table is the bar: analytic magnitudes to
  1e-9, digital response equal to the prewarped analog one, and the drawn
  curve equal to an impulse response's FFT.
- **Bit-Crusher's private filters** (`bc-core/src/filters.rs`, direct form 1
  through `biquad`) can move onto `ni-filter` later. That is not part of this
  decision, and its golden renders would have to be re-recorded with a
  reason.
- **Existing crates may still serve as oracles.** A SciPy-style design crate
  can be a test-only dev-dependency once `cargo deny` passes it.
- **A linear-phase or oversampled mode can be added later** without breaking
  saved sets, because VST3 IDs derive from the parameter keys.

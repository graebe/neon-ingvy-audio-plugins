# NI Filter Designer: concept

Status: concept, 2026-10-12. Nothing is built yet. The window is drawn on the
**NI Plugin Layouts** canvas as *NI Filter Designer — proposed* (mirrored in
`design/designs/FilterDesigner-Proposed.dc.html`, notes FD1–FD9), and the
decisions behind the DSP are in [ADR 0004](../adr/0004-iir-filter-design.md).

## What it is

A filter design tool you play as an equalizer. Up to sixteen filters sit on
one response curve. Each is a Bell, a shelf, a cut, a band pass, a notch, an
all-pass or a tilt, and the cuts, band passes and notches come in every
classic family at every order: Butterworth, Bessel, Chebyshev I and II,
Elliptic and Linkwitz-Riley, from 6 to 48 dB/oct, with resonance. In front
of them sits an isolator in the manner of EQ Three: two crossovers, three
bands, a kill on each.

So one window covers what Live's EQ Eight (serial parametric bands) and EQ
Three (a parallel band split with kills) do, and adds what neither offers:
the choice of family and order, ripple and stopband, notches, and a
per-filter channel (Left, Right, Mid, Side).

It is minimum phase only: IIR filters, no latency, like EQ Eight. A
linear-phase mode is not in scope; the parameter keys leave room for one.

## The signal path

```
in ─► isolator (optional) ─► node 1 ─► node 2 ─► … ─► node 16 ─► Output ─► out
      LR4/LR8 band split       each in Stereo, L, R, Mid or Side
```

The chain runs in slot order. Order only matters audibly when L/R and M/S
nodes are mixed, which do not commute. M = (L+R)/2 and S = (L−R)/2, so the
round trip is exact.

## The DSP (a shared crate, `ni-filter`)

The filter design lives in a new product-free crate,
`engines/shared/crates/ni-filter`, so Bit-Crusher's private filters can later
move onto it. Everything inside runs in f64. Only the audio I/O and the
editor's curves are f32.

### One structure: the TPT state-variable filter

Every filter is a cascade of second-order sections, plus one first-order
section for an odd order. Each second-order section is a TPT (zero-delay
feedback) state-variable filter whose three outputs are mixed:

    y = m_hp·hp + m_bp·bp + m_lp·lp,    g = tan(·),  k = 1/Q

Those three outputs span every second-order numerator. So one structure
realises low-pass, high-pass, band pass and band stop, a Bell, shelves and an
all-pass (`x − 2k·bp`). Zeros on the jω axis are `m_hp = 1, m_bp = 0,
m_lp = (ωz/ωp)²`, which puts the digital zeros exactly on the unit circle:
notches are infinitely deep by construction. The structure stays stable
under per-sample modulation, which a direct-form biquad does not. The
starting point is the paper's SVF
(`papers/subtractive-synthesis/code/src/filter/svf.rs`), generalised to
output mixing and f64.

### Family designs

Each family is an analog prototype, as zeros and poles of a normalised
low-pass:

| family | prototype | parameters |
|---|---|---|
| Butterworth | poles on the unit circle | – |
| Bessel | a constant pole table for orders 1–8, tested against the reverse Bessel polynomial | – |
| Chebyshev I | equiripple passband | Ripple |
| Chebyshev II | equiripple stopband, zeros on jω | Stopband |
| Elliptic | both, Orfanidis's closed form (Landen/Jacobi) | Ripple, Stopband |
| Linkwitz-Riley | Butterworth squared (odd orders round up) | – |

A frequency transform then makes the type: LP→LP, LP→HP, LP→BP or LP→BS.
A Band Pass or Notch of order N has 2N poles. Sections take their identity
from the closed-form pole index and are never re-sorted, so they cannot swap
places mid-glide when Ripple moves.

**Prewarp once per filter, not once per section.** A cascade is the bilinear
transform of its prototype only if every section shares one frequency
scaling. Cuts use `Ω_c = tan(π·fc/fs)`. Band Pass and Notch prewarp both
−3 dB edges, then take `Ω0 = √(Ω1·Ω2)` and `B = Ω2 − Ω1`. Prewarping each
section on its own would bend Butterworth and equiripple shapes above about
fs/8 and move the Chebyshev II and Elliptic zeros.

**Freq is the −3 dB point in every family.** The exception is Linkwitz-Riley,
whose Freq is the −6 dB point, so a Low Cut and a High Cut at one Freq sum
flat. Each prototype is normalised once by bisection on |H|². The
normalisation depends only on Family, Order, Ripple and Stopband, never on
Freq, so switching families leaves the curve under the handle. Ripple is
capped at 3 dB, so the −3 dB crossing is unique.

**Resonance** on a cut of order two or more scales only the section with the
highest Q: `Q_top' = Q_top·(Q/0.7071)`, clamped at 40. At order 2 that is
exactly EQ Eight's resonant 12 dB cut. Order 1 has no resonance.

### The EQ shapes

The Bell, the first and second-order shelves, Tilt, and the order-1 Notch and
Band Pass use matched-magnitude coefficients (Vicanek, *Matched Second Order
Digital Filters*, 2016). They are converted into the SVF's (g, k, m) from
their poles and numerator. A matched design follows the analog shape up to
Nyquist, so a 15 kHz shelf is not squeezed by the bilinear transform. This
costs no oversampling, no latency and no change of phase. Shelves of order
3–4 are Holters–Zölzer cascades.

**Adaptive Q** (a Bell only) is our own definition: `Q_eff = Q·10^(|G|/40)`,
×1.41 at 6 dB and ×2 at 12 dB. EQ Eight's curve is undocumented.

**Scale** multiplies every gain (Bell, shelves, Tilt) at once, as EQ Eight's
does.

### What each type uses

The editor draws what a type does not use as disabled, never hidden, so
nothing in the window moves when the type changes.

| type | Order | Family | Q | Gain | Ripple / Stopband |
|---|---|---|---|---|---|
| Low Cut, High Cut | 1–8 (6N dB/oct) | all six | Resonance (order ≥ 2) | – | by family |
| Band Pass, Notch | 1–8 (2N poles) | all but LR (plays as Butterworth) | bandwidth | – | by family |
| Bell | – | – | bandwidth | ± | – |
| Low Shelf, High Shelf | 1–4 | – | top section (order ≥ 2) | ± | – |
| All Pass | 1–8 | – | order ≥ 2 | – | – |
| Tilt | – | – | – | ± (ends at ±G/2) | – |

### The isolator

- **Crossovers:** Linkwitz-Riley, LR4 (BW2²) or LR8 (BW4²).
- **Bands:**
  - Low = LP(lower) followed by the all-pass of the upper crossover. Without that all-pass the bands do not sum flat.
  - Mid = HP(lower) → LP(upper).
  - High = HP(lower) → HP(upper).
- The upper crossover never falls below the lower one.
- Band gains glide per sample, so a kill does not click.
- With every gain at 0 dB the isolator is an all-pass: flat, but not bit-transparent. That is why it starts off.

### Cost, smoothing and discrete changes

- **Two tiers of recomputation.** The prototype (Family, Order, Ripple, Stopband) is recomputed only when one of those changes, at most every 64 samples while Ripple or Stopband glide. The mapping (Freq, Q, Gain, Scale, sample rate) is recomputed on a 16-sample control grid counted from a global sample counter, so the output is identical for every block size. Coefficients are interpolated linearly inside the sub-block. The SVF needs only g > 0 and k > 0 to stay stable under that.
- **Smoothing** reuses `ni_dsp::smooth` at control rate (`coef(fs/16)`, the house 5 ms): Freq glides in log₂, Q in log, Gain in dB.
- **Type, Family, Order, Channel and bypass crossfade.** Each slot holds two preallocated instances. A change designs the second one from zero state and crossfades over 20 ms, with cos² and sin² weights. A change that arrives during a fade waits for its end. A bypassed or empty node does not run, and its state is zero.
- **Safety:**
  - Every edge is clamped to 10 Hz … 0.49·fs. A Band Pass whose upper edge clamps keeps its lower edge and narrows, and the curve shows that.
  - Denormals are flushed at block end inside the core, because the Move path has no `ScopedNoDenormals`.
  - `reset()` zeroes all state, completes fades and snaps every glide.
- **Worst case:** 16 nodes × 8 sections × 2 channels is 256 f64 SVF ticks per sample, doubled briefly during fades. That is affordable as scalar code.

## Parameters (174)

Keys are stable, and VST3 IDs derive from the keys. **Choice label lists are
frozen at v1:** appending a label changes the normalised value of every saved
automation point, so every type and family goes in now.

| keys | kind | range | default |
|---|---|---|---|
| `output` | float, bipolar | −24…+24 dB | 0 |
| `scale` | float, bipolar | −200…+200 % | 100 |
| `adaptive_q` | switch | Off, On | Off |
| `analyzer` | choice, not automatable | Off, Pre, Post, Both | Post |
| `iso_on` | switch | Off, On | Off |
| `iso_low`, `iso_mid`, `iso_high` | float | −60…+6 dB, −60 is −inf | 0 |
| `iso_kill_low`, `_mid`, `_high` | switch | Off, On | Off |
| `iso_freq_lo` | float, log | 30…2000 Hz | 250 |
| `iso_freq_hi` | float, log | 500…16000 Hz | 2500 |
| `iso_slope` | choice | 24 dB, 48 dB | 24 dB |
| `nNN_type` ×16 | choice | Empty, Bell, Low Shelf, High Shelf, Low Cut, High Cut, Band Pass, Notch, All Pass, Tilt | Empty |
| `nNN_on` ×16 | switch | Off, On | On |
| `nNN_family` ×16 | choice | Butterworth, Bessel, Chebyshev I, Chebyshev II, Elliptic, Linkwitz-Riley | Butterworth |
| `nNN_order` ×16 | choice | 1…8 | 2 |
| `nNN_freq` ×16 | float, log | 10…22000 Hz | spread from 30 Hz to 16 kHz by slot |
| `nNN_gain` ×16 | float, bipolar | −30…+30 dB | 0 |
| `nNN_q` ×16 | float, log | 0.1…40 | 0.7071 |
| `nNN_ripple` ×16 | float | 0.01…3 dB | 1.0 |
| `nNN_stopband` ×16 | float | 20…120 dB | 60 |
| `nNN_channel` ×16 | choice | Stereo, Left, Right, Mid, Side | Stereo |

**Empty is a type, and On is the bypass.**

- **Add a node:** the slot gets a type. This needs no extra parameter.
- **Remove a node:** its type goes back to Empty. Its other values stay, so undo is one step.
- **Bypass:** On goes Off. A bypassed node keeps its handle on the curve (hollow) and its glyph (dim), so it can be found and switched back on. An Off-only model could not tell a bypassed node from an empty slot.

**An empty instance is bit-exact:** every type is Empty, the isolator is off,
and Output 0 dB multiplies by exactly 1.0. A test holds it to that.

The host's bypass goes through `ni::Processor`, as in every product.

## The window (888 × 708, Ultraviolet)

The canvas board is interactive and is the reference. In short:

### RESPONSE

One well, 824×288, on PlotWell's rules.

- **Axes:** 10 Hz to 22 kHz across in equal steps of pitch. Up is ±6, ±12, ±18 or ±30 dB; you click the caption to change it.
- **Layers, back to front:**
  - the analyzer, on its own scale and only while the window is open;
  - the rulers (there are no grid lines in the system);
  - 0 dB as the grey line;
  - each filter alone, and the isolator's bands, as plot-ghost lines;
  - the selected filter alone in ink on a bg-000 casing (what you asked for);
  - the total in uv (what you get).
- **Handles** are the kit's `PlotHandle`, which arrives with the Bit-Crusher's filter well on `feature/bit-crusher-plots`:
  - A gain filter's dot sits at its Freq and Gain.
  - A cut's dot sits on its curve, and dragging up raises the resonance.
  - A Band Pass, Notch or All Pass sits on 0 dB, and dragging up narrows it.
  - The selected Bell, Band Pass or Notch shows its −3 dB edges as two ticks that drag its Q.
- **The isolator** draws its crossovers as rules with a tab, and its band gains as dots L, M and H.

### NODE ROW

- Sixteen 40px cells on the StepGrid's pitch, plus an ISO cell.
- A cell shows its type's glyph: uv while it plays, dim while it is bypassed, nothing while it is empty.
- Click selects a cell; Alt-click bypasses it.

### INSPECTOR (two columns wide)

- **A filter:** Type, Family, Order and Channel as selects, then Freq, Gain, Q (named Resonance on a cut), Ripple and Stopband as knobs.
- **The isolator:** Low, Mid, High, Lower, Upper and Slope as knobs, a Kill under each band, and On at the title.

### OUTPUT

Output, Scale and Adaptive.

### Interaction

The system's conventions, plus:

- Double-click the picture to add a filter in the first free slot: a Low Cut below 30 Hz, a High Cut above 15 kHz, a Bell between them, at the pointer's gain.
- Alt-drag or the wheel sets Q; Shift is five times finer.
- Double-click a handle to reset it; Delete removes the filter.
- Every drag is one grouped host gesture, so undo is one step.

### The one amber mark

It is kept for a single warning: a filter whose edge the sample rate clamps.

### New pieces for the design system

Each is promoted when the plugin is built, not before:

- ResponseWell: PlotWell with a log-frequency ruler and a dB ruler. The kit's `Axis` gains a log mode. That means extracting the file-local `logFraction()` from `plugins/vocalize/editor/Plots.cpp` and the mapping in `plugins/spectrogram/editor/Scales.cpp`, not writing a third copy.
- NodeCell.
- Nine 16px type glyphs.

## Engine → editor: draw what the engine renders

The editor never models the DSP. The engine publishes `FdPicture` through
shell-core's bridge, as Vocalize publishes its envelopes
(`engines/vocalize/crates/vz-capi/src/shell.rs`).

- **Curves:** 512 log-spaced points from 10 Hz to min(22 kHz, Nyquist).
  - Each node's own curve, `node_db[16][512]`.
  - The total, `total_db[2][512]`, in the L/R or M/S basis the editor asks for.
  - The three isolator bands.
  - Each curve is evaluated from the exact digital sections.
- **Analyzer bands:** `pre_db[256]` and `post_db[256]`, through realfft. The band mapping moves from spectro-core into ni-dsp, because products may not depend on each other.
- **Serials:** one per node, so the editor rebuilds a path only when its node changed.
- **Size and cadence:** about 45 KB per frame, at shell-core's 100 Hz.
- **Budget:**
  - Only dirty curves are recomputed, at most four node curves per publish.
  - Nothing is computed while unwatched (`Command::Watching`) or with the analyzer off.

## The Move version (Schwung, second priority)

`fd-move` is the same `fd-core` without its picture feature: no FFT and no
curves. It has two pages of eight knobs, under the plugin's own keys:

- **Isolator:** `iso_low`, `iso_mid`, `iso_high`, `iso_freq_lo`, `iso_freq_hi`, `iso_slope`, `iso_on`, `output`. The isolator starts on here; a gain at its minimum is the kill.
- **Cuts:** `n01_on`, `n01_freq`, `n01_order`, `n01_q`, and the same four for `n02`. Slot 1 is preset as a Low Cut and slot 2 as a High Cut, both Butterworth.

Frequencies and orders travel as name-format enums, as Bit-Crusher's Rate
does. The panic, CC 120 or 123, calls `reset()`. Its test is Bit-Crusher's
`a_host_panic_on_cc_120_or_123_silences_the_next_block`, plus a check that
the state is zero.

## Testing

| level | what |
|---|---|
| ni-filter, analytic | Butterworth, Chebyshev I/II against their closed forms at 400 points, orders 1–8, relative error 1e-9; Elliptic passband ripple and stopband floor to 1e-6 dB; Bessel poles as roots of the reverse Bessel polynomial; LR low + high = 1 |
| digital = analog | Hd(e^jω) = Ha(j·tan(ω/2)·scale) within 1e-9 (the single-prewarp test); −3 dB at Freq for every family and order at 20 Hz, 1 kHz, 10 kHz and 0.45·fs, at 44.1–192 kHz |
| drawn = rendered | an impulse through the SVF cascade, 65536-point FFT, against the published curve, under 1e-6 dB |
| stability | proptest across type, family, order, Freq, Q, Ripple and Stopband extremes and sample rates: every g and k finite and positive, the output bounded; a 10 ms sweep of 20 Hz → 20 kHz at Q 40 |
| clicks | Type, Family or Order flipped every 50 ms under a −6 dBFS sine: nothing above 10 kHz past −60 dBFS; no step from a +24 dB gain jump |
| invariance | an empty instance bit-exact; output bit-identical at block sizes 1, 7, 64, 8192 and random ones; a 1 kHz Bell within 0.01 dB from 44.1 to 384 kHz |
| real time | `tests/no_alloc.rs` around process, every setter, discrete switches, reset and a watched publish |
| shells | C ABI test through the generated header; JUCE processor round trip, host golden render, editor snapshots; Move render A/B within one 16-bit step and the panic test |

Coverage stays above the 80 % floor, which applies to new units automatically.
The board's own curves were checked the same way (−3 dB at Freq for every
family and order, elliptic and Chebyshev II floors, the isolator flat to
1e-6 dB), so the picture on the canvas is the response the engine will have.

## Milestones

Each one starts on its own go-ahead.

| | delivers |
|---|---|
| M0 | this concept, ADR 0004, the canvas board |
| M1 | `ni-filter`: designs, sections, response evaluation, the analytic and stability tests |
| M2 | `fd-core` and `fd-capi`: the chain, isolator, parameters, smoothing, crossfades, the picture |
| M3 | `plugins/filter-designer` (`NIFilterDesigner`, "NI Filter Designer", code `FlDs`, `Fx EQ`), the kit's ResponseWell and NodeCell, Ultraviolet promotion |
| M4 | `fd-move` and `modules/filter-designer` |
| M5 | the tutorial docs (`docs/live.md`, `docs/schwung.md`), README, CHANGELOG, the site |

M1 to M3 also touch the repository-wide lists a product joins:

- `NI_PRODUCTS` and `ni_add_rust_engine` in the root `CMakeLists.txt`
- `tests/CMakeLists.txt`
- `versions.json` and `tests/versions.test.mjs`
- `tests/ground_shells.test.mjs`
- the release workflows' tag lists
- `THIRD_PARTY_LICENSES.md`

Vocalize's merge is the checklist for these.

## Open questions for the owner

- Should the isolator offer a third crossover (four bands, as DJ mixers increasingly do)? The concept stops at three, as EQ Three does.
- Is a group-delay view wanted beside the magnitude, since the families differ most in phase (Bessel vs Elliptic)? It would be a second caption mode of the RESPONSE well, not a second well.

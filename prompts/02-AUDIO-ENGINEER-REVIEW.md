Review the signal processing of this audio plugin codebase as a senior audio
DSP engineer. Judge audio quality, correctness, and real-time behaviour, not
code style. Be direct and factual.

Context: [plugin type(s), framework (e.g. JUCE), formats, target sample
rates and buffer sizes, CPU budget, intended sound: clean vs. coloured]

First reconstruct the signal path of each plugin from the code: stages in
order, sample rate at each stage, where state lives, latency per stage.
State it in 5-10 lines per plugin. Mark anything inferred rather than read.

Then assess every item below. If an item does not apply, say so in one line;
do not skip it silently.

1. Real-time safety: No allocation, locks, I/O, logging, exceptions, or
   unbounded work on the audio thread. Lock-free and wait-free handover
   between UI and audio thread. All buffers sized in prepare, not in process.
2. Aliasing: Identify every nonlinearity (saturation, clipping, waveshaping,
   fast gain changes, audio-rate modulation). Is aliasing controlled by
   oversampling, ADAA, band-limited methods, or a combination? Is the
   chosen factor justified by the harmonic content?
3. Oversampling: Factor, filter type (linear vs. minimum phase, FIR vs.
   polyphase IIR), passband ripple, stopband rejection, pre-ringing, CPU
   cost. Is only the nonlinear part oversampled? Separate quality for
   real-time and offline render?
4. Latency: Sources (lookahead, linear-phase filters, oversampling, FFT).
   Reported correctly to the host, constant while parameters change, kept
   in bypass, and dry path delayed to match the wet path.
5. Parameters: Per-sample smoothing where audible, correct smoothing law
   (linear vs. exponential, dB vs. linear gain), sample-accurate automation,
   no clicks on bypass, preset change, or mode switch.
6. Filters: Topology (TPT/ZDF state variable vs. biquad), stability under
   fast modulation and near Nyquist, frequency warping, coefficient updates
   off the per-sample path where possible, precision at low frequencies
   and high sample rates.
7. Nonlinear and analog modelling: Method used (static waveshaper, ZDF with
   solver, wave digital filter, neural), solver convergence and worst-case
   iteration count, behaviour at extreme drive.
8. Dynamics: Detector type (peak, RMS, true peak), attack and release
   ballistics independent of sample rate, lookahead implementation, gain
   reduction smoothing, knee, stereo linking, sidechain filtering,
   program-dependent release. For limiters: inter-sample peak handling
   and ceiling guarantee.
9. Time-based effects: Delay-line interpolation quality, modulation without
   clicks or pitch artifacts, feedback stability, tail length reported
   to the host.
10. Spectral processing: Window, overlap, perfect reconstruction, latency,
    transient smearing.
11. Numerical robustness: Denormals, NaN and Inf guards, DC offset, division
    by zero, feedback runaway, float vs. double where precision matters.
12. Gain staging: Headroom between stages, unity gain at default settings,
    output level compensation, behaviour above 0 dBFS, correct dry/wet mix.
13. Sample-rate and block-size independence: Identical sound at 44.1 to
    192 kHz; identical output for block sizes from 1 to 8192 and for
    varying block sizes; correct handling of silence and zero-length blocks.
14. Channel handling: Mono, stereo, and other bus layouts; mid/side
    correctness; channel-independent state.
15. State and lifecycle: Reset clears all state, prepare can be called
    repeatedly, state save and restore round-trips, behaviour on transport
    start and stop.
16. Metering: Standards compliance where claimed (ITU-R BS.1770 loudness and
    true peak), ballistics, thread-safe transfer to the UI.
17. Performance: Per-sample cost, SIMD use, branch-free inner loops,
    avoidable recomputation, worst-case versus average CPU.
18. Testing: pluginval at high strictness in CI, null tests, impulse and
    frequency response tests, aliasing measurement with sine sweeps,
    sample-rate and block-size sweeps, automation stress, long NaN/Inf
    soak runs, latency verification, state round-trip.

Rules:
- Tie every finding to a file, function, and stage of the signal path.
- State the audible or measurable consequence, not only the flaw.
- Prefer measurement to opinion: propose the test signal and the metric
  that would confirm each finding (e.g. 1 kHz sine at -6 dBFS, aliasing
  components below -100 dB).
- Name the trade-off (quality, CPU, latency) and one concrete alternative
  with a reference to the method.
- Distinguish "wrong" from "acceptable for this plugin type".
- If you could not verify something by reading the code, say so.
- Do not modify any code.

Output:
1. Signal path summary per plugin.
2. Verdict in 3 sentences.
3. Findings by severity (critical = audible defect or crash risk,
   major = quality or compatibility loss, minor = polish).
4. Checklist table: all 18 items with pass / fail / not applicable / not
   verifiable.
5. Missing tests, in priority order.
6. Top 5 changes with estimated effort (S/M/L).
# spectro

A short-time Fourier analyzer that produces **spectrogram columns**: one byte per
log-spaced frequency band, handed to a UI through a lock-free ring.

MIT, © 2026 Torben Gräber. **No dependencies at all** — the FFT is one file,
`crates/spectro-core/src/fft.rs`, checked against a naive DFT.

```
cargo test              # the FFT, the band mapping, equivalence, no-allocation proofs
cargo test --release -p spectro-core --lib -- --ignored --nocapture fft_timings
cargo test --release -p spectro-recv --test bench -- --ignored --nocapture
```

## Why it has one shell and not two

The Trance Gate's engine carries two wrappers — `tg-capi` for the plugin and
`tg-move` for the Schwung module — because it has two hosts. This one has one:
the Move has no screen to draw a spectrogram on. So there is no `spectro-move`,
and `cmake/Schwung.cmake` says the same thing where a build would look for it.

A crate belongs to exactly one product, which is also why the two engines never
depend on each other.

## The shape of it

| | |
|---|---|
| `fft.rs` | real-input FFT: an N/2 complex radix-2/4 transform and a split pass |
| `window.rs` | Hann, periodic, with the coherent gain that makes a full-scale sine read 0 dB |
| `bands.rs` | log-spaced bands, peak power per band, dB to byte, power tables for sums |
| `lib.rs` | the analyzer and the SPSC column ring |
| `reference.rs` | the analysis before it was optimised, kept as the equivalence oracle |
| `crates/spectro-recv` | several sources into one picture, pumped by a worker thread |
| `crates/spectro-capi` | the C ABI; `include/spectro_core.h` and `spectro_recv.h` are the contract |

**The transform runs on whichever thread feeds the analyzer** — one per hop, a
bounded and constant cost. Through `spectro_push_f32` that is the audio thread.
The Spectrogram plugin feeds its analyzers from **spectro-recv's worker**
instead: ProcessBlock only copies its mono sum into a ring, and a thread owned
by the receiver drains it together with every Listen-In bus, so every source
gets the same frames and neither the audio thread nor the host's UI thread runs
a transform. Nothing allocates after `configure`, and the `no_alloc` tests fail
the build if that stops being true — for the worker too.

## The thread rules are part of the ABI

```
spectro_new / free / configure   one thread, nothing else in flight
spectro_push_f32                 the audio thread, and only it
spectro_take_columns             the message thread, and only it
spectro_set_range                the message thread, any time
```

`push` and `take_columns` may overlap — that is what the ring is for. Two
pushers, or a `configure` racing either, is undefined.

In Rust the rule is the type system's: `Analyzer::new(cfg).split()` gives a
`Producer` for the audio thread and a `Consumer` for the message thread, each
working through `&mut self`, so safe code cannot push from two threads. The C
handle holds both halves and every entry point touches only its own thread's.

**`set_range` is the exception, and deliberately so**: the frequency range is a
dropdown in a plugin editor, so it has to be changeable while audio is running.
It allocates nothing, swaps nothing and locks nothing — it stores a request, and
the audio thread rebuilds its own band table in place at its next frame. The
axis (`spectro_band_hz`) is derived from the request rather than read from that
table, so the two threads share no mutable state at all, and each column carries
the range it was measured under so a stale one is never handed out under a new
scale.

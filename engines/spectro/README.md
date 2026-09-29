# spectro

A short-time Fourier analyzer that produces **spectrogram columns**: one byte per
log-spaced frequency band, computed on the audio thread and handed to a UI
through a lock-free ring.

MIT, © 2026 Torben Gräber. **No dependencies at all** — the FFT is ninety lines
in `crates/spectro-core/src/fft.rs`, checked against a naive DFT.

```
cargo test              # the FFT, the band mapping, and the no-allocation proof
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
| `fft.rs` | radix-2 complex FFT, in place, precomputed twiddles and bit reversal |
| `window.rs` | Hann, periodic, with the coherent gain that makes a full-scale sine read 0 dB |
| `bands.rs` | log-spaced bands, peak per band, dB to byte |
| `lib.rs` | the analyzer and the SPSC column ring |
| `crates/spectro-capi` | the C ABI; `include/spectro_core.h` is the contract |

**The FFT runs on the audio thread.** The alternative — ship samples out and
transform them on the message thread — makes the picture's time axis stretch and
squeeze with the host's UI load. Here the columns are produced by the audio clock
and UI jitter can only make several arrive at once. Nothing allocates after
`configure`, and `tests/no_alloc.rs` fails the build if that stops being true.

## The thread rules are part of the ABI

```
spectro_new / free / configure   one thread, nothing else in flight
spectro_push_f32                 the audio thread, and only it
spectro_take_columns             the message thread, and only it
spectro_set_range                the message thread, any time
```

`push` and `take_columns` may overlap — that is what the ring is for. Two
pushers, or a `configure` racing either, is undefined.

**`set_range` is the exception, and deliberately so**: the frequency range is a
dropdown in a plugin editor, so it has to be changeable while audio is running.
It allocates nothing, swaps nothing and locks nothing — it stores a request, and
the audio thread rebuilds its own band table in place at its next frame. The
axis (`spectro_band_hz`) is derived from the request rather than read from that
table, so the two threads share no mutable state at all, and each column carries
the range it was measured under so a stale one is never handed out under a new
scale.

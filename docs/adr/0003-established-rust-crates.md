# 0003. Established Rust crates replace hand-written code

- Status: accepted, 2026-10-06
- Decided by: Torben Gräber
- Reverses: "zero external crates", the workspace's rule since it was formed
- Follows from: [0001](0001-gpl-3.0-or-later.md)

## Context

The Cargo workspace has had no external crates. Every `[dependencies]` entry
is a `path` into the repository. The reason was the licence, not taste. Under
MIT every byte in the artefact was to be "permissive AND ACCOUNTED FOR", and
"a crate that needs no attribution is cheaper than one that does"
(spectro-core's Cargo.toml, as it read until this decision). The architecture
notes added that it was "what makes the licence audit finish in one sitting".

The result is a body of code that does what a well-known crate would do, and
is owned, tested and audited here instead:

- **The FFT** is one file, `spectro-core/src/fft.rs` (355 lines), with its own
  window and a naive-DFT reference to test it against.
- **The plumbing between threads is hand-made**: the shell's command queue,
  snapshot and handoff, the Spectrogram's ring, and bit casts through
  `AtomicU32` for floats.
- **The patch formats are parsed by hand**: the Trance Gate's state, slot files
  and paste, and the Side-Chain's parameters, including migrations across
  seven state versions.
- **Numbers are formatted by hand** (`ni-dsp/src/fmt.rs`), and the
  no-allocation tests use a home-made counting allocator (`ni-testkit`).
- **The C headers are hand-written.** Such a header can drift from the Rust
  side without either one failing to compile, so a test exists only to
  exercise that seam.
- **The shared-memory bus declares its POSIX calls itself** rather than taking
  `libc` (`bus-core/src/shm.rs`). It works on macOS only, so Listen-In cannot
  feed a Spectrogram on Windows or Linux, where the JUCE shell will now build
  ([0002](0002-juce-native-editors.md)).

## Decision

**Established, well-maintained crates are preferred to hand-written code.**
Each one must be under a licence on the allowlist in
[0001](0001-gpl-3.0-or-later.md), and must come from crates.io.

| hand-written today | replaced by |
|---|---|
| the FFT, its window and its reference | `realfft` / `rustfft` |
| the MIDI decode in `sc-core` (the panic path, CC 120/123 to Reset, stays as it is) | `wmidi` |
| xorshift and the tests' LCGs | `fastrand`, seeded |
| the command queue, the Spectrogram's ring and its columns | `rtrb`, with typed command enums |
| the snapshot | `triple_buffer` |
| the handoff | `basedrop` or `arc-swap` |
| `AtomicU32` bit casts | `atomic_float` |
| the Trance Gate's state, slot files and paste, and the Side-Chain's parameters | `serde` and `serde_json`: the migrations become a versioned enum, and every old blob still reads |
| `ni-dsp`'s number formatting | `core::fmt` and `arrayvec`; `lexical-core` where C-compatible text is a protocol, as Schwung's is |
| the POSIX declarations in `bus-core` | `libc` and `windows-sys`, behind one shared-memory type; the seqlock ring stays |
| `ni-testkit`'s counting allocator | `assert_no_alloc`, and `proptest` beside it |
| the hand-written C headers | `cbindgen`, from each crate's `build.rs` |
| the cargo and `lipo` glue in CMake | Corrosion, with `lipo` kept only where a universal build needs it |

The DSP stays domain code: the gate, the ducker and the Ground's beat clock are
what this repository is for.

These rules do not change, and every crate has to fit them:

- **Nothing on the audio thread allocates, locks or parses.**
  `assert_no_alloc` proves it in the tests.
- **A panic aborts** (`panic = "abort"` in the root `Cargo.toml`). An unwind
  out of `extern "C"` into a host is undefined behaviour.
- **One static library per plugin.** Two Rust staticlibs in one binary each
  carry the Rust runtime.
- **The C ABI stays.** A test keeps `tg-capi`'s symbols source-compatible.
- **The Move's panic path stays intact, with its tests.** MIDI CC 120 and 123
  reset the Side-Chain.
- **A changed sound is re-recorded, never accepted silently.** Golden renders
  may move only within a tolerance: −120 dBFS, or ±1 step of the reference
  for the spectrogram. Each re-recording names its reason.

## Consequences

- **`Cargo.lock` gains crates from crates.io, and each one is checked.**
  `cargo deny check licenses bans sources` runs in the quick tier: an
  unlisted licence, a source other than crates.io, or a duplicated crate is
  caught before it ships.
- **The crates' notices are generated.** The Rust section of
  THIRD_PARTY_LICENSES.md is written by cargo-about
  (`scripts/gen-rust-notices.sh`). `scripts/check-licenses.mjs` still checks
  that what is listed is exactly what ships.
- **The comments that defend zero dependencies are rewritten** as the code
  they describe is replaced.
- **The first build needs the network**, or a populated cargo cache, to fetch
  the crates.

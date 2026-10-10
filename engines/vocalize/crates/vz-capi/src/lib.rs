// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The `vz_*` C ABI: NI Vocalize's engine as its shells hold it.

WHAT THIS CRATE IS ALLOWED TO DO: null-check, bound-check, copy into C memory,
and hand the call to `vz-core`. What a vowel is is decided there.

TWO HEADERS. `vz_core.h` is `engine.rs`: the parameter table, the vowel's
formants, each mode's latency, and one engine instance -- everything the C
tests and the Move module reach. `vz_shell.h` is `shell.rs`, the plugin's: the
engine behind shell-core's bridge, with the picture the editor draws. The Move
build leaves the shell out (the `shell` feature), and the Spectral engine with
it (`spectral`).

THE PARAMETER TABLE IS THE ENGINE'S. A shell declares its parameters from
`vz_param_*` -- count, key, name, kind, range, unit, a choice's labels --
prints a value with `vz_param_text` and reads typing with `vz_param_parse`,
so the plugin, the Move and the manual print the same words.

THREADS. The table, `vz_formants` and `vz_latency` are pure: any thread. An
instance belongs to one thread at a time -- on the plugin, the audio thread's
between `vz_shell_begin` and `vz_shell_end`. Nothing an audio thread calls
allocates, and the workspace aborts on panic, because unwinding out of
`extern "C"` is undefined.
*/

/* The ground's C ABI (gnd_*), which every plugin archive carries: without a
 * reference from here the linker would drop its #[no_mangle] symbols. */
#[cfg(feature = "shell")]
use ground_capi as _;

mod engine;
pub use engine::*;

#[cfg(feature = "shell")]
pub mod shell;

#[cfg(test)]
mod tests;

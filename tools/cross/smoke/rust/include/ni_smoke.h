/* The C ABI of the smoke plugin's Rust engine (rust/src/lib.rs). */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Multiplies `len` samples at `samples` by `gain`, in place. Null is a no-op. */
void ni_smoke_apply_gain(float* samples, size_t len, float gain);

#ifdef __cplusplus
}
#endif

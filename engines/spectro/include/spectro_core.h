/*
 * spectro-core -- the C ABI.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * A short-time Fourier analyzer that produces SPECTROGRAM COLUMNS: one byte per
 * log-spaced frequency band, produced on the audio thread and drained on the
 * message thread through a lock-free ring.
 *
 * THE THREAD RULES ARE PART OF THE ABI:
 *
 *   spectro_new / free / configure   one thread, nothing else in flight
 *   spectro_push_f32                 the audio thread, and only it
 *   spectro_take_columns             the message thread, and only it
 *
 * push and take_columns may overlap -- that is what the ring is for. Two
 * pushers, or a configure racing either, is undefined.
 *
 * This header is written by hand rather than generated, and it is the contract:
 * if it and crates/spectro-capi/src/lib.rs disagree, they disagree silently.
 * tests/spectro_columns.c compiles against THIS file and links the real
 * library, which is what keeps them honest.
 */
#ifndef SPECTRO_CORE_H
#define SPECTRO_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct spectro_t spectro_t;

/* The defaults, for a caller that wants to state them explicitly.
 *
 * THE WINDOW AND THE HOP ARE NOT CONSTANTS IN PRACTICE: both depend on the
 * sample rate, and spectro_pick_fft_size / spectro_pick_hop below are what a
 * host-facing caller should use. These two are what those functions return at
 * 48 kHz, and they are here so a test can state a configuration without a rate.
 *
 * 8192 at 48 kHz is bins 5.9 Hz apart -- which is what SPECTRO_F_MIN of 10 Hz
 * requires, since an axis cannot start below the first bin above DC. It is 171
 * ms of window, so time resolution is the price. -96 dB is 16-bit silence. */
#define SPECTRO_FFT_SIZE 8192
#define SPECTRO_HOP      1024
#define SPECTRO_BANDS    256
#define SPECTRO_F_MIN    10.0f
#define SPECTRO_F_MAX    20000.0f
#define SPECTRO_DB_FLOOR (-96.0f)
#define SPECTRO_DB_CEIL  0.0f
/* Columns the ring holds before it starts dropping them: ~5 s at the defaults.
 * A drainer running at 60 Hz leaves at most one behind. */
#define SPECTRO_COLUMN_CAPACITY 256

/* An analyzer at the defaults above. Null only if the allocator failed. */
spectro_t *spectro_new(void);
/* Null is a no-op. */
void spectro_free(spectro_t *s);

/* Re-configure and clear. ALLOCATES: a prepare-to-play call, never an
 * audio-thread one. Out-of-range arguments are CLAMPED, not rejected --
 * fft_size is rounded down to a power of two, f_max down to Nyquist, and hop
 * into fft_size/32 .. fft_size. */
void spectro_configure(spectro_t *s, float sample_rate, int fft_size, int hop,
                       int bands, float f_min, float f_max,
                       float db_floor, float db_ceil);

/* Change the frequency range the picture covers -- the zoom behind the editor's
 * range dropdown.
 *
 * MESSAGE THREAD, AND SAFE WHILE AUDIO IS RUNNING, which spectro_configure is
 * not: this allocates nothing and swaps nothing. It stores a request, and the
 * audio thread rebuilds its own band table at its next frame. Columns already
 * queued from before the change are dropped rather than handed out under the new
 * scale -- they are answers to a different question.
 *
 * spectro_band_hz reflects the new range IMMEDIATELY, before the audio thread has
 * run: the axis is derived from the request, not read from the table.
 *
 * A range that cannot be drawn (not finite, f_min below 1 Hz, or less than half
 * an octave wide) is ignored. */
void spectro_set_range(spectro_t *s, float f_min, float f_max);

/* Feed n mono samples. Audio thread only. Allocates nothing, locks nothing. */
void spectro_push_f32(spectro_t *s, const float *mono, int n);

/* Drain finished columns into out, spectro_bands() bytes each, oldest first,
 * band 0 = lowest frequency. out must hold max_cols * spectro_bands() bytes.
 * Returns the columns written, which may be 0. Message thread only. */
int spectro_take_columns(spectro_t *s, unsigned char *out, int max_cols);

/* Bytes per column. */
int spectro_bands(const spectro_t *s);

/* Band centre frequencies in Hz, ascending, up to max. Returns how many were
 * written. THE UI'S AXIS LABELS COME FROM HERE, so the log mapping exists in
 * exactly one place. */
int spectro_band_hz(const spectro_t *s, float *out, int max);

/* The window length that resolves 10 Hz at this rate, as a power of two: 8192
 * at 44.1/48 kHz, 16384 at 88.2/96 kHz, capped there. Past 96 kHz the bins
 * widen rather than the window growing without limit.
 *
 * THE RULE LIVES IN THE ENGINE because it is arithmetic about sound, not about
 * a host: bins are sample_rate / fft_size apart, and an axis that starts at
 * 10 Hz needs them finer than that. */
int spectro_pick_fft_size(float sample_rate);

/* The hop that scrolls at ~47 columns a second at this rate, so a window holds
 * the same thirteen seconds whatever the session runs at. Never longer than
 * fft_size. */
int spectro_pick_hop(float sample_rate, int fft_size);

/* Columns dropped because nothing drained the ring. A diagnostic: nonzero means
 * the drainer stopped, not that the analysis broke. */
int spectro_dropped(const spectro_t *s);

#ifdef __cplusplus
}
#endif

#endif /* SPECTRO_CORE_H */

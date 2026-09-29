/*
 * The analyzer, through the C ABI the plugin actually links.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * WHY THIS EXISTS WHEN cargo test ALREADY PASSES. The Rust tests check the
 * analyzer; this checks the BOUNDARY -- that spectro_core.h and the crate agree
 * about argument order, about what `bands` means, and about who owns the output
 * buffer. A header written by hand (which this one is, deliberately) can drift
 * from its implementation without either side failing to compile: swap two float
 * parameters and every call still builds.
 *
 * It is the same argument as the Trance Gate's render_plugin.c, one level down:
 * plain C against the ABI, so it survives whatever the shell is written in.
 */
#include "spectro_core.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SR 48000.0f
#define BLOCK 512

static int failures = 0;

static void check(int ok, const char* what)
{
  if (!ok) { printf("FAIL  %s\n", what); failures++; }
  else     { printf("ok    %s\n", what); }
}

/* The loudest band in a column. */
static int peak_band(const unsigned char* col, int bands)
{
  int best = 0;
  for (int b = 1; b < bands; b++)
    if (col[b] > col[best]) best = b;
  return best;
}

/* The band whose centre is nearest `hz`. */
static int nearest_band(const float* centres, int bands, float hz)
{
  int best = 0;
  for (int b = 1; b < bands; b++)
    if (fabsf(centres[b] - hz) < fabsf(centres[best] - hz)) best = b;
  return best;
}

/* Feed `frames` of a sine, in blocks, the way a host would. */
static void push_sine(spectro_t* s, float hz, float amp, int frames)
{
  static float buf[BLOCK];
  static double phase = 0.0;
  const double step = 2.0 * M_PI * (double) hz / (double) SR;
  for (int off = 0; off < frames; off += BLOCK)
  {
    const int n = (frames - off < BLOCK) ? frames - off : BLOCK;
    for (int i = 0; i < n; i++) { buf[i] = amp * (float) sin(phase); phase += step; }
    spectro_push_f32(s, buf, n);
  }
}

int main(void)
{
  spectro_t* s = spectro_new();
  check(s != NULL, "spectro_new returned a handle");
  if (!s) return 1;

  /* THE SIZES COME FROM THE ENGINE, the way the plugin's OnReset takes them --
   * so this test exercises the rule as well as the analysis. */
  const int fft = spectro_pick_fft_size(SR);
  const int hop = spectro_pick_hop(SR, fft);
  check(fft == SPECTRO_FFT_SIZE, "the picked window is the documented default at 48 kHz");
  check(hop == SPECTRO_HOP, "the picked hop is the documented default at 48 kHz");
  /* The claim the window length exists for: bins finer than the 10 Hz the axis
   * is asked to start at. */
  check(SR / (float) fft < SPECTRO_F_MIN, "the bins are finer than the axis floor");

  spectro_configure(s, SR, fft, hop, SPECTRO_BANDS,
                    SPECTRO_F_MIN, SPECTRO_F_MAX, SPECTRO_DB_FLOOR, SPECTRO_DB_CEIL);

  const int bands = spectro_bands(s);
  check(bands == SPECTRO_BANDS, "spectro_bands is the configured band count");

  float* centres = (float*) calloc((size_t) bands, sizeof(float));
  const int n_hz = spectro_band_hz(s, centres, bands);
  check(n_hz == bands, "spectro_band_hz filled every band");

  /* Ascending, and inside the range asked for -- the picture's axis is drawn
   * from these, so an unordered one would draw a scrambled scale. */
  int ordered = 1;
  for (int b = 1; b < bands; b++)
    if (!(centres[b] > centres[b - 1])) ordered = 0;
  check(ordered, "the band centres ascend");
  check(centres[0] >= SPECTRO_F_MIN * 0.5f && centres[bands - 1] <= SR * 0.5f,
        "the band centres are inside the audible range");
  /*
   * THE AXIS STARTS WHERE IT WAS ASKED TO. It did not before: at a 1024-point
   * window the bins were 46.9 Hz apart, Bands clamped the bottom of the axis up
   * to meet them, and the picture began at 47 Hz however low f_min was set --
   * silently, since a spectrogram starting an octave up still looks like a
   * spectrogram.
   */
  check(fabsf(centres[0] - SPECTRO_F_MIN) < 1.0f,
        "the axis starts at the requested floor, not at the first bin");

  unsigned char* cols =
    (unsigned char*) calloc((size_t) bands * SPECTRO_COLUMN_CAPACITY, 1);

  /* ---------------------------------------------------------------- a tone */
  push_sine(s, 1000.0f, 1.0f, 16384);
  int got = spectro_take_columns(s, cols, SPECTRO_COLUMN_CAPACITY);
  check(got > 0, "a tone produced columns");

  /* Reassigned by each stage below, so not const-pointing. */
  const unsigned char* last = cols + (size_t) (got - 1) * bands;
  const int peak = peak_band(last, bands);
  const int want = nearest_band(centres, bands, 1000.0f);
  check(abs(peak - want) <= 1, "the peak is the 1 kHz band");
  check(last[peak] > 250, "a full-scale tone reaches the top of the ramp");

  /* And it is a line rather than a smear: three bands out is already far down
   * the ramp. 255/96 bytes per dB, 20 dB down. */
  int clean = 1;
  for (int b = 0; b < bands; b++)
    if (abs(b - peak) >= 3 && last[b] > 255 - (int) (20.0 * 255.0 / 96.0)) clean = 0;
  check(clean, "the tone did not smear across the picture");

  /* ------------------------------------------------------- a LOW tone, 30 Hz */
  /* Unresolvable at the old window length: the nearest bin to 30 Hz was the one
   * at 46.9 Hz, so a bass note and a kick fundamental were the same pixels. */
  push_sine(s, 30.0f, 1.0f, 96000);
  got = spectro_take_columns(s, cols, SPECTRO_COLUMN_CAPACITY);
  check(got > 0, "a low tone produced columns");

  last = cols + (size_t) (got - 1) * bands;
  const int low_peak = peak_band(last, bands);
  const int low_want = nearest_band(centres, bands, 30.0f);
  /*
   * THE PEAK DOWN HERE IS A PLATEAU, NOT A POINT, and the test has to say so.
   *
   * Bands are 3% apart -- under a hertz at 30 Hz -- while bins are 5.9 Hz apart,
   * so half a dozen neighbouring bands all read the SAME bin and carry the same
   * byte. Which of them an argmax returns is a tie-break, not a measurement:
   * this C one takes the lowest, Rust's max_by_key takes the highest, and
   * asserting on either is asserting on the tie-break.
   *
   * So the claim is that the band nearest 30 Hz is AT the maximum -- true
   * whatever the tie-break does, and false if the tone lands anywhere else.
   */
  check(last[low_want] == last[low_peak], "the 30 Hz band is at the peak");
  check(last[nearest_band(centres, bands, 60.0f)] < last[low_peak] / 2,
        "30 Hz is told apart from 60 Hz");

  /* --------------------------------------------------------------- silence */
  /* Long enough to flush the tone out of the analysis window entirely. */
  static float quiet[BLOCK];
  memset(quiet, 0, sizeof quiet);
  for (int i = 0; i < 64; i++) spectro_push_f32(s, quiet, BLOCK);
  got = spectro_take_columns(s, cols, SPECTRO_COLUMN_CAPACITY);
  check(got > 0, "silence produced columns");

  int floored = 1;
  const unsigned char* quiet_col = cols + (size_t) (got - 1) * bands;
  for (int b = 0; b < bands; b++)
    if (quiet_col[b] != 0) floored = 0;
  check(floored, "silence is exactly the floor");

  /* ------------------------------------------------------------- the zoom */
  /*
   * THE RANGE CHANGES WHILE AUDIO IS RUNNING, which is the whole reason
   * spectro_set_range exists beside spectro_configure: no allocation, no swap,
   * and the audio thread adopts it at its next frame.
   */
  spectro_set_range(s, 200.0f, 4000.0f);

  /* The axis reports the new range IMMEDIATELY -- it is derived from the
   * request, not read from the table the audio thread is about to rebuild. */
  check(spectro_band_hz(s, centres, bands) == bands, "the zoomed axis filled");
  check(fabsf(centres[0] - 200.0f) < 4.0f, "the zoomed axis starts at 200 Hz");
  check(fabsf(centres[bands - 1] - 4000.0f) < 80.0f, "the zoomed axis ends at 4 kHz");

  /* Columns measured against the old range are dropped rather than handed out
   * under the new scale. */
  check(spectro_take_columns(s, cols, SPECTRO_COLUMN_CAPACITY) == 0,
        "no stale column survived the range change");

  /* And a tone lands where the new axis says it should. */
  push_sine(s, 1000.0f, 1.0f, 65536);
  got = spectro_take_columns(s, cols, SPECTRO_COLUMN_CAPACITY);
  check(got > 0, "the zoomed range produced columns");
  last = cols + (size_t) (got - 1) * bands;
  check(abs(peak_band(last, bands) - nearest_band(centres, bands, 1000.0f)) <= 1,
        "1 kHz peaks in the 1 kHz band under the Mid range");

  /* An undrawable range is ignored rather than acted on. */
  spectro_set_range(s, 0.0f, 100.0f);
  spectro_set_range(s, 1000.0f, 1000.0f);
  spectro_band_hz(s, centres, bands);
  check(fabsf(centres[0] - 200.0f) < 4.0f, "an undrawable range left the axis alone");

  spectro_set_range(s, SPECTRO_F_MIN, SPECTRO_F_MAX);   /* back to the full view */
  push_sine(s, 1000.0f, 0.5f, 65536);
  (void) spectro_take_columns(s, cols, SPECTRO_COLUMN_CAPACITY);

  /* --------------------------------------------------- the ring, and draining */
  check(spectro_dropped(s) == 0, "nothing was dropped while the ring was drained");

  /* An undrained ring drops rather than blocking or growing. */
  push_sine(s, 1000.0f, 0.5f, SPECTRO_HOP * (SPECTRO_COLUMN_CAPACITY + 32) + SPECTRO_FFT_SIZE);
  check(spectro_dropped(s) > 0, "an undrained ring reported drops");
  got = spectro_take_columns(s, cols, SPECTRO_COLUMN_CAPACITY);
  check(got == SPECTRO_COLUMN_CAPACITY, "a full ring gives back its whole capacity");

  /* ------------------------------------------------------------ null safety */
  /* The shell calls these before OnReset and after its destructor runs; every
   * one of them has to be a no-op rather than a crash in a host. */
  spectro_push_f32(NULL, quiet, BLOCK);
  spectro_take_columns(NULL, cols, 1);
  check(spectro_bands(NULL) == 0, "spectro_bands(NULL) is 0");
  check(spectro_band_hz(NULL, centres, bands) == 0, "spectro_band_hz(NULL) is 0");
  spectro_free(NULL);
  check(1, "null arguments are no-ops");

  free(centres);
  free(cols);
  spectro_free(s);

  printf(failures ? "\n%d check(s) failed\n" : "\nall checks passed\n", failures);
  return failures ? 1 : 0;
}

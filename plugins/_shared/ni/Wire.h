// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * ni::wire -- what every product's processor does to a block, a clock or a
 * number on its way between the host and the engine: the transport, the
 * chunking, a plot's quantisers and locale-free numbers.
 *
 * No JUCE type and no engine: tests/cpp/ni_wire.cpp links this alone.
 *
 * NUMBERS ARE WRITTEN AND READ WITHOUT THE C LOCALE. printf and atof follow
 * LC_NUMERIC, and a host that sets a comma-decimal locale turns "0.5" into
 * "0,5" on the way out and 0.5 into 0 on the way in. These use '.' always.
 * std::to_chars for floating point needs macOS 13.3 and the plugins load on 11,
 * so the fraction is formatted as an integer.
 */
#pragma once

#include <string>
#include <string_view>

namespace ni {
namespace wire {

/* -1..1 -> 0..255 for a waveform bound. Clamped; a non-finite value is
 * mid-scale, where silence sits, rather than an undefined cast. */
unsigned char encode_bipolar(float v);

/* 0..1 -> 0..255 for a gain. Clamped; a non-finite value is 0. */
unsigned char encode_unipolar(float v);

/* `beats` advanced by `frames` at `bpm`. Unchanged for a non-positive frame
 * count, tempo or sample rate, rather than an infinity in a phase. */
double advance_beats(double beats, int frames, double bpm, double sampleRate);

/* ------------------------------------------------------------- numbers */

/*
 * `v` with exactly `decimals` fraction digits (0..9), as printf's "%.*f" in
 * the C locale: ties to even, a negative zero keeps its sign, "nan" and "inf"
 * spelled out. NUL-terminated into `buf`; returns the length, or -1 when
 * `cap` is too small.
 */
int format_fixed(char* buf, int cap, double v, int decimals);
void append_fixed(std::string& out, double v, int decimals);

/* As append_fixed, with trailing fraction zeros (and a bare point) dropped:
 * "0.5", "1", "0.583333333". */
void append_decimal(std::string& out, double v, int maxDecimals);

void append_int(std::string& out, long long v);

/*
 * The leading number of `s`, as atof reads one -- leading space, a sign,
 * digits, a fraction, an exponent, anything after it ignored, 0 for none --
 * but with '.' as the point whatever the locale.
 */
double parse_number(std::string_view s);

/* A whole decimal int and nothing else; false (and `out` untouched) for
 * anything that is not exactly one. */
bool parse_int(std::string_view s, int& out);

/* -------------------------------------------------------------- blocks */

/* The host's clock as every engine's transport struct holds it. A stopped
 * transport is beats -1, not a stale position; a running one with a negative
 * or non-finite position (no musical position, or a host bug) is treated as
 * stopped; a tempo of 0 is 120. */
struct Transport
{
  int running;
  double beats;
  float bpm;
};
Transport host_transport(bool running, double tempo, double ppq);

/*
 * A host may hand over a longer block than it announced, and growing a buffer
 * on the audio thread allocates. So a block is processed in chunks of what was
 * reserved: f(offset, frames) for each.
 */
template <class F>
void for_each_chunk(int frames, int cap, F&& f)
{
  if (cap <= 0)
    return;
  for (int off = 0; off < frames; off += cap)
    f(off, frames - off < cap ? frames - off : cap);
}

} // namespace wire
} // namespace ni

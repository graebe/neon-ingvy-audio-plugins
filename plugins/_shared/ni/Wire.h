// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * ni::wire -- what every product's processor does to a block, a clock or a
 * number on its way between the host and the engine: the transport and its
 * loop, the chunking, a plot's quantisers and locale-free numbers.
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

#include <cstdint>
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

/* ---------------------------------------------------------------- loops */

/*
 * THE HOST'S LOOP. A host reports one position per block, and a loop's end
 * can fall anywhere inside the block: the frames after it are back at the
 * loop's start, which the block's position does not say. A product that
 * carries the position on through the block from the host's -- every chunk
 * of for_each_chunk -- has to wrap it there, or it plays on past the end
 * until the next block corrects it: up to a whole block of wrong beat.
 *
 * WHERE THE END FALLS. A frame is past the end when its position is within
 * `loop_edge` beats of it or beyond: an end that falls exactly on a frame is
 * that frame, whichever way the host's ppq was rounded. A billionth of a beat
 * is far below a frame at any tempo and rate a host plays, and far above the
 * rounding of a position in a long set.
 */
constexpr double loop_edge = 1e-9;

/* The host's cycle, in beats. Off for none, and for one that cannot be
 * played: a start that is not a finite position at or after 0, or an end not
 * past it. */
struct Loop
{
  bool on = false;
  double start = 0.0;
  double end = 0.0;
};
Loop host_loop(bool looping, double start, double end);

/* How many of `frames` from `beats` at `bpm` play before the loop's end: the
 * first frame at or past it starts the next span. All of them while there is
 * no loop, no tempo or rate, or the position is already at or past the end --
 * a host plays on past a loop the playhead was set beyond. At least 1 for a
 * positive frame count. */
int frames_before_loop_end(double beats, int frames, double bpm, double sampleRate, Loop loop);

/* `beats` advanced by `frames`, wrapped back to the loop's start, plus what it
 * overshot the end by, when it reaches the end: where the host's own position
 * would be. As advance_beats while the position is not before the end. */
double advance_beats(double beats, int frames, double bpm, double sampleRate, Loop loop);

/*
 * A block in chunks of `cap`, and each chunk split again where a running
 * transport reaches the loop's end: f(offset, frames) for each span, with
 * `t.beats` where that span starts. `t` is left where the block ends.
 */
template <class F>
void for_each_span(int frames, int cap, Transport& t, Loop loop, double sampleRate, F&& f)
{
  for_each_chunk(frames, cap, [&](int off, int n) {
    while (n > 0)
    {
      const int span = t.running ? frames_before_loop_end(t.beats, n, double(t.bpm), sampleRate, loop) : n;
      f(off, span);
      if (t.running)
        t.beats = advance_beats(t.beats, span, double(t.bpm), sampleRate, loop);
      off += span;
      n -= span;
    }
  });
}

/*
 * WHERE A SPAN STARTS ON THE HOST'S TIMELINE, in samples: what the audio bus
 * stamps a run with (ni::HostClock::timeInSamples), so a reader on another
 * track lines it up with its own audio.
 *
 * The block's first sample `blockSample` sits at `block.beats`; a span of
 * for_each_span starts at `spanBeats`. Until the loop wraps that is the block's
 * sample plus the span's offset; after it, the loop start's sample plus what
 * the span overshot the start by -- where the host's own timeline says that
 * frame is. Both come from the beats, converted once at the block's tempo,
 * which for a span before the wrap is exactly blockSample + offset. With no
 * running transport, tempo or rate there are no beats to go by, and it is
 * blockSample + offset.
 */
std::int64_t span_timeline(std::int64_t blockSample, const Transport& block, int offset, double spanBeats,
                           double sampleRate);

} // namespace wire
} // namespace ni

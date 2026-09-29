/*
 * The Trance Gate's wire arithmetic, on its own so it can be tested.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * WHY THIS IS NOT IN TranceGate.cpp ANY MORE. The same reason as the
 * Spectrogram's Wire.h, which states it at length: everything in that file is
 * a method on a class whose base is the format wrapper, so none of it could be
 * reached without building and loading a plugin bundle.
 *
 * What moved is only the arithmetic -- no iPlug2 type appears below, and
 * nothing here reads a member. The host calls (GetParam, GetPPQPos,
 * SendParameterValueFromUI) stay where they are, because a test could not
 * honestly exercise them anyway; tg_au is what covers those, against a real
 * host.
 *
 * These four are the ones where being wrong is SILENT:
 *
 *   split_pair          a dropped edit, indistinguishable from not clicking
 *   encode_sample       a scope that draws a plausible wrong waveform
 *   clamp_editor_height a window that grows to fill the screen, or refuses to
 *   advance_beats       a gate that drifts against the grid over a long block
 */
#pragma once

#include <string>

namespace tg {
namespace wire {

/*
 * "<a>:<b>" -> a, b. False when there is no colon, and then neither output is
 * touched.
 *
 * The editor sends every step edit through this shape -- "<index>:<mode>" for
 * a step, "<index>:<value>" for a depth, "<index>:<text>" for a typed readout.
 * A payload that fails to split is DROPPED rather than half-applied: applying
 * the index without the value would move the cursor and edit whatever it
 * landed on, which is an edit the user did not ask for at a place they were
 * not looking.
 *
 * The split is at the FIRST colon. A typed readout may contain another one.
 */
bool split_pair(const std::string& arg, std::string& a, std::string& b);

/*
 * -1..1 -> 0..255 for the scope, clamped, with a non-finite input becoming
 * mid-scale rather than a random byte.
 *
 * The clamp is not defensive tidiness: an uninitialised capture column can
 * hold a NaN, and a NaN cast to int is undefined -- in practice a value that
 * draws as a full-scale spike and reads as a real transient. Mid-scale is the
 * honest answer for "no audio here", because that is where silence sits.
 */
unsigned char encode_sample(float v);

/*
 * The editor's requested height, refused when it is outside what a real editor
 * can want. Returns 0 for "do not resize".
 *
 * The UI reports the height it needs because it is the side that knows both
 * the row count and the scale it had to apply. That makes the number
 * UNTRUSTED input to this side: a bug in the editor's arithmetic arrives here
 * as a request to make the window 40 000 pixels tall, and the host will
 * honour it.
 */
int clamp_editor_height(int requested);

/*
 * Advance a transport position by `frames` at `bpm` and `sampleRate`, in
 * beats. Returns the new position.
 *
 * A block is processed in chunks when the host hands over more than was
 * reserved, and every chunk is a CONTINUATION of the same block -- without
 * this the engine would gate an entire block on one instant, which at a long
 * block is an audible stutter locked to the buffer size rather than the grid.
 *
 * Beats per sample is bpm / 60 / sampleRate. A non-positive sample rate
 * returns the position unchanged: there is no meaningful advance, and
 * dividing by it would put an infinity into the engine's phase.
 */
double advance_beats(double beats, int frames, double bpm, double sampleRate);

/* As the Spectrogram's: base64's extra third plus the frame's fixed 32. */
constexpr int framed_size(int nBytes) { return nBytes * 4 / 3 + 32; }

} // namespace wire
} // namespace tg

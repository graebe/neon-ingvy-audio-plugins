/*
 * NI Pump's wire arithmetic, on its own so it can be tested.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * WHY THIS IS NOT IN Pump.cpp. The same reason the Trance Gate's and the
 * Spectrogram's Wire.h state at length: everything in that file is a method on
 * a class whose base is the format wrapper, and `iplug_configure_target`
 * refuses a non-format target while `IPlug_include_in_plug_hdr.h` #errors
 * outside one. So none of it can be reached without building and loading a
 * plugin bundle, and a test that needs a DAW is a test nobody runs.
 *
 * What moved is only the arithmetic -- no iPlug2 type appears below and nothing
 * here reads a member. The host calls (GetParam, GetPPQPos, IsChannelConnected)
 * stay where they are, because a test could not honestly exercise them anyway;
 * `pump_au` covers those against a real host.
 *
 * These are the ones where being wrong is SILENT:
 *
 *   split_pair           a dropped edit, indistinguishable from not clicking
 *   encode_sample        a scope that draws a plausible wrong waveform
 *   encode_unipolar      a gain-reduction trace at half resolution, which
 *                        looks like a coarse meter rather than a bug
 *   clamp_editor_height  a window that grows to fill the screen, or refuses to
 *   advance_beats        a duck that drifts against the grid over a long block
 *   key_is_duplicate     a sidechain that ducks on its own input
 */
#pragma once

#include <string>

namespace pump {
namespace wire {

/*
 * "<a>:<b>" -> a, b. False when there is no colon, and then neither output is
 * touched.
 *
 * A payload that fails to split is DROPPED rather than half-applied: applying
 * the index without the value would edit whatever the index landed on, which is
 * an edit the user did not ask for at a place they were not looking.
 *
 * The split is at the FIRST colon, because a typed readout may contain another.
 */
bool split_pair(const std::string& arg, std::string& a, std::string& b);

/*
 * -1..1 -> 0..255 for the scope's waveform bounds, clamped, with a non-finite
 * input becoming mid-scale rather than a random byte.
 *
 * The clamp is not defensive tidiness: an uninitialised capture column can hold
 * a NaN, and A NaN CAST TO INT IS UNDEFINED -- in practice a value that draws as
 * a full-scale spike and reads as a real transient. Mid-scale is the honest
 * answer for "no audio here", because that is where silence sits.
 */
unsigned char encode_sample(float v);

/*
 * 0..1 -> 0..255 for the gain-reduction trace.
 *
 * A SEPARATE FUNCTION RATHER THAN encode_sample, because the duck is unipolar
 * and pushing it through the bipolar encoder spends half the byte on a range it
 * can never enter: every value lands in 128..255, so the trace arrives at 7
 * bits and reads as a coarse meter. Same clamp, same NaN rule, twice the
 * resolution where it is actually used.
 */
unsigned char encode_unipolar(float v);

/*
 * The editor's requested height, refused when it is outside what a real editor
 * can want. Returns 0 for "do not resize".
 *
 * The UI reports the height it needs because it is the side that knows both its
 * content and the scale it had to apply. That makes the number UNTRUSTED input
 * here: a bug in the editor's arithmetic arrives as a request to make the window
 * 40 000 pixels tall, and the host will honour it.
 */
int clamp_editor_height(int requested);

/*
 * Advance a transport position by `frames` at `bpm` and `sampleRate`, in beats.
 *
 * A block is processed in chunks when the host hands over more than was
 * reserved, and every chunk is a CONTINUATION of the same block -- without this
 * the engine would place an entire block's triggers on one instant, which at a
 * long block is an audible stutter locked to the buffer size rather than to the
 * grid.
 *
 * A non-positive sample rate returns the position unchanged: there is no
 * meaningful advance, and dividing by it would put an infinity into the
 * engine's phase.
 */
double advance_beats(double beats, int frames, double bpm, double sampleRate);

/*
 * Whether a "sidechain" buffer is really just the main input again.
 *
 * LOGIC AND GARAGEBAND COPY BUS 1 INTO THE SIDECHAIN BUS when nothing is
 * patched, a bug old enough to have its own forum threads. iPlug2's own example
 * works around it with a memcmp and a comment calling the workaround imperfect,
 * and saying the better answer is an explicit enable.
 *
 * Pump takes the better answer -- the Source parameter IS the explicit enable --
 * so this is not load-bearing for correctness. It is used for one thing: to stop
 * the UI reporting a key signal that is actually the track's own audio, which
 * would read as "the sidechain is working" while nothing is routed.
 *
 * Compares exactly, because that is what the bug does: it hands over the same
 * samples, not similar ones. Two genuinely identical buffers are possible --
 * both silent, most obviously -- so a true answer here means "indistinguishable
 * from the main input", never "definitely unpatched", and the caller must treat
 * it as the weaker claim.
 */
bool key_is_duplicate(const float* main, const float* key, int frames);

/* base64's extra third plus the frame's fixed 32, as the Spectrogram's. */
constexpr int framed_size(int nBytes) { return nBytes * 4 / 3 + 32; }

} // namespace wire
} // namespace pump

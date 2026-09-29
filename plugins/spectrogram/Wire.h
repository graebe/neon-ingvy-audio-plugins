/*
 * The Spectrogram's wire format, on its own so it can be tested.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * WHY THIS IS NOT IN Spectrogram.cpp ANY MORE.
 *
 * The encoder lived inside OnIdle, which is a method on a class whose base is
 * the FORMAT WRAPPER -- iplug::Plugin is a typedef to IPlugVST3 or IPlugAU
 * depending on which define is set. Nothing can construct one of those outside
 * a plugin bundle, so the hex encoder could not be reached by a test at all,
 * and the editor's copy of it was the only thing describing the format:
 *
 *   ui/test/columns.test.mjs  "this is a transcription of it, which is the
 *                              point: if the two disagree, this test is the
 *                              disagreement"
 *
 * A transcription is not an oracle. That is the same arrangement the curves
 * had -- shape() in the engine and shape() in JavaScript, agreeing with each
 * other by hand -- and the S-curve was wrong in one of them for as long as
 * both existed.
 *
 * So the format is stated ONCE, here, in free functions over plain data. The
 * plugin calls them and sends the result; the test calls them and pins the
 * result to a table the editor is checked against too. Neither side is
 * compared against the other.
 */
#pragma once

#include <string>

namespace spectro {
namespace wire {

/*
 * "<cols>:<bands>:" then two upper-case hex characters per byte, column after
 * column, band 0 (lowest frequency) first within each.
 *
 * UPPER CASE is not cosmetic. The editor's decoder is a hand-written nibble
 * map rather than parseInt, and the boundary it can get wrong is 9 -> A --
 * char codes 57 to 65, with six characters in between that are neither.
 * Lower-case hex would land on a different run and decode to nonsense that
 * still draws.
 */
std::string encode_columns(const unsigned char* cols, int nCols, int bands);

/*
 * "20.6,41.2,..." -- one decimal, comma separated, no trailing separator.
 *
 * One decimal is well past what a label shows. The editor needs the whole list
 * rather than the two ends because it draws a tick wherever the scale crosses
 * a decade, and the scale is not linear in either axis.
 */
std::string encode_axis(const float* hz, int n);

/*
 * "<f_min>:<f_max>" -> lo, hi. Returns false and leaves both untouched when
 * there is no colon; anything else is handed to atof and passed on.
 *
 * That is deliberately the pre-existing contract and not a stricter one -- the
 * engine is what refuses an undrawable range, and moving the judgement here
 * would be a behaviour change wearing a refactor's clothes. See the note at
 * the implementation.
 */
bool parse_range(const std::string& arg, float& lo, float& hi);

/*
 * What `n` bytes of payload cost once the transport has formatted them:
 * base64 inflates by a third, and the message frame costs a fixed 32 on top.
 * Spelled here because the number is a property of the wire, not of the class
 * that happens to send it.
 */
constexpr int framed_size(int nBytes) { return nBytes * 4 / 3 + 32; }

} // namespace wire
} // namespace spectro

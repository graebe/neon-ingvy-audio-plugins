/*
 * The Spectrogram's wire format, on its own so it can be tested.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * Stated ONCE, in free functions over plain data: the plugin sends what these
 * return, and tests/cpp/spectro_wire.cpp pins it to ui/test/wire_table.txt,
 * the table the editor's decoder is checked against too. Neither side is
 * compared with the other.
 */
#pragma once

#include <string>
#include <vector>

namespace spectro {
namespace wire {

/*
 * "<ch>:<cols>:<bands>:" then two upper-case hex characters per byte, column
 * after column, band 0 (lowest frequency) first within each.
 *
 * THE CHANNEL LEADS, and it is there because a receiver sends one message PER
 * SOURCE rather than one frame holding all of them. The budget is a product --
 * channels x columns x bands -- and three channels at the full catch-up budget
 * overflows the transport's cap by 32 bytes. Per channel, each source keeps its
 * own budget and the static_assert in Spectrogram.h stays the thing that proves
 * it; the alternative was a ragged multi-channel frame with a new failure mode
 * and a decoder nobody had tested.
 *
 * UPPER CASE is not cosmetic. The editor's decoder is a hand-written nibble
 * map rather than parseInt, and the boundary it can get wrong is 9 -> A --
 * char codes 57 to 65, with six characters in between that are neither.
 * Lower-case hex would land on a different run and decode to nonsense that
 * still draws.
 */
std::string encode_columns(const unsigned char* cols, int nCols, int bands, int ch);

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
 * there is no colon; each half is read as atof would, with '.' as the point
 * whatever the locale. The engine is what refuses an undrawable range.
 */
bool parse_range(const std::string& arg, float& lo, float& hi);

/*
 * "<slot>,<slot>,..." -> the slots, appended in order.
 *
 * An unreadable field is SKIPPED rather than failing the list: "2,x,5" should
 * listen to 2 and 5, because the alternative is a picker that silently does
 * nothing because one field was mangled, which is the harder fault to see.
 * Bounds are the engine's -- it refuses a slot outside 1..abus_max_slot() and
 * caps how many it will take.
 */
void parse_slots(const std::string& arg, std::vector<unsigned int>& out);

/*
 * "<ch>,<ch>,..." -> channel indices, appended in order.
 *
 * SEPARATE FROM parse_slots BECAUSE ZERO MEANS SOMETHING HERE. A bus slot is
 * 1-based and slot 0 is a mistake, so parse_slots refuses it -- but channel 0
 * is the track the plugin is sitting on, which is the one channel a view is
 * most likely to contain. Reusing that parser and patching the zero back in
 * afterwards is the kind of fix that reads fine and is wrong for "10,0".
 */
void parse_channels(const std::string& arg, std::vector<int>& out);

/*
 * "<a>:<b>:<on>" -> which two channels the clash is measured between, and
 * whether to send it at all.
 *
 * Returns false and leaves all three untouched when the shape is wrong. A
 * comparison is not a range -- the two halves are channel INDICES and the third
 * is a flag -- so it gets its own parser rather than being squeezed through
 * parse_range, which would read "0:1:1" as a very small frequency band.
 */
bool parse_compare(const std::string& arg, int& a, int& b, bool& on);

/*
 * "<ppq>:<bpm>:<num>:<denom>:<running>:<ppqPerCol>:<sampleRate>" -- where the
 * host's transport is, how much musical time one column covers, and what rate
 * the session runs at.
 *
 * THE RATE IS HERE because it is a host fact and this is the host-facts
 * message. The editor needs it to say which Listen-In buses cannot be compared
 * with this one: a different rate picks a different window and so a different
 * group delay, and two pictures offset by an amount nobody can see is worse
 * than one that says it will not draw.
 *
 * THE PLUGIN REPORTS THE CLOCK AND NOTHING ABOUT THE PICTURE. How many bars the
 * window spans, and therefore which pixel a position lands on, is a layout
 * decision and stays in the editor -- which is why this carries no bar count
 * and why there is no message going the other way to set one.
 *
 * `ppqPerCol` is hop / sampleRate * bpm / 60, and it exists so a catch-up batch
 * of columns can be spread across the positions it actually covers instead of
 * being stacked on the newest one.
 *
 * Six decimals on the position: at 200 BPM a column is ~0.06 beats, and three
 * would quantise two columns onto one value.
 */
std::string encode_sync(double ppq, double bpm, int num, int denom, bool running,
                        double ppqPerCol, int sampleRate);

} // namespace wire
} // namespace spectro

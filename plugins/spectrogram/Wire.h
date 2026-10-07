// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The text of NI Spectrogram's saved session, read back: the four shapes its
 * strings take (State.cpp writes them). On their own, in free functions over
 * plain data, so tests/cpp/spectro_wire.cpp pins each one alone.
 *
 * WHAT A STRING THAT DOES NOT READ DOES is each reader's to say, because the
 * strings are not alike: a mangled slot is skipped and the rest kept, a range
 * without its colon is refused whole. Bounds are the engine's -- it refuses a
 * slot outside 1..abus_max_slot(), an undrawable range and a channel past
 * what opened -- so nothing here needs to know them.
 */
#pragma once

#include <string>
#include <vector>

namespace spectro::wire
{

/*
 * "<f_min>:<f_max>" -> lo, hi: the zoom, and the clash's floor and balance.
 * False, both untouched, when there is no colon; each half is read as atof
 * would, with '.' as the point whatever the locale.
 */
bool parse_range (const std::string& arg, float& lo, float& hi);

/*
 * "<slot>,<slot>,..." -> the slots, appended in order.
 *
 * An unreadable field is SKIPPED rather than failing the list: "2,x,5" listens
 * to 2 and 5, because a session that silently opens nothing because one field
 * was mangled is the harder fault to see.
 */
void parse_slots (const std::string& arg, std::vector<unsigned int>& out);

/*
 * "<ch>,<ch>,..." -> channel indices, appended in order.
 *
 * SEPARATE FROM parse_slots BECAUSE ZERO MEANS SOMETHING HERE. A bus slot is
 * 1-based and slot 0 is a mistake, so parse_slots refuses it -- but channel 0
 * is the track the plugin is sitting on, the one channel a view is most
 * likely to contain.
 */
void parse_channels (const std::string& arg, std::vector<int>& out);

/*
 * "<a>:<b>:<on>" -> which two channels the clash is measured between, and
 * whether it is. False, all three untouched, when the shape is wrong or a
 * channel is negative. A comparison is not a range -- the halves are channel
 * INDICES and the third a flag -- so it has its own reader.
 */
bool parse_compare (const std::string& arg, int& a, int& b, bool& on);

} // namespace spectro::wire

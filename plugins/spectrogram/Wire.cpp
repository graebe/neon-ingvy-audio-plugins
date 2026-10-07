// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The session's text, read back. See Wire.h.
 *
 * No JUCE and no engine, so tests/cpp/spectro_wire.cpp links it alone.
 */
#include "Wire.h"
#include "ni/Wire.h"

#include <cstdlib>
#include <string_view>

namespace spectro::wire
{

bool parse_range (const std::string& arg, float& lo, float& hi)
{
    /* THE COLON IS THE WHOLE VALIDATION. An empty half reads as 0, and the
     * engine refuses anything undrawable. */
    const size_t sep = arg.find (':');
    if (sep == std::string::npos)
        return false;

    lo = float (ni::wire::parse_number (std::string_view (arg).substr (0, sep)));
    hi = float (ni::wire::parse_number (std::string_view (arg).substr (sep + 1)));
    return true;
}

void parse_slots (const std::string& arg, std::vector<unsigned int>& out)
{
    size_t i = 0;
    while (i < arg.size())
    {
        size_t j = arg.find (',', i);
        if (j == std::string::npos)
            j = arg.size();

        const std::string field = arg.substr (i, j - i);
        if (! field.empty())
        {
            const long v = std::strtol (field.c_str(), nullptr, 10);
            if (v > 0 && v < 1000)
                out.push_back (static_cast<unsigned int> (v));
        }
        i = j + 1;
    }
}

bool parse_compare (const std::string& arg, int& a, int& b, bool& on)
{
    const size_t first = arg.find (':');
    if (first == std::string::npos)
        return false;
    const size_t second = arg.find (':', first + 1);
    if (second == std::string::npos)
        return false;

    const int ra = int (std::strtol (arg.substr (0, first).c_str(), nullptr, 10));
    const int rb = int (std::strtol (arg.substr (first + 1, second - first - 1).c_str(), nullptr, 10));
    /* A channel index is never negative. Anything past what is actually open
     * is the RECEIVER's to refuse: it is the only side that knows how many
     * buses opened, and a slot that failed to open shifts every index after. */
    if (ra < 0 || rb < 0)
        return false;
    a = ra;
    b = rb;
    on = arg.compare (second + 1, std::string::npos, "0") != 0;
    return true;
}

void parse_channels (const std::string& arg, std::vector<int>& out)
{
    size_t i = 0;
    while (i < arg.size())
    {
        size_t j = arg.find (',', i);
        if (j == std::string::npos)
            j = arg.size();

        const std::string field = arg.substr (i, j - i);
        if (! field.empty())
        {
            /* strtol reports "not a number" only through endptr, and an empty
             * or alphabetic field would otherwise arrive as a perfectly good
             * channel 0. */
            char* end = nullptr;
            const long v = std::strtol (field.c_str(), &end, 10);
            if (end != field.c_str() && v >= 0 && v < 64)
                out.push_back (int (v));
        }
        i = j + 1;
    }
}

} // namespace spectro::wire

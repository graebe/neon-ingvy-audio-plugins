// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

#include "BusSource.h"

#include <cmath>

namespace ni::ui
{

std::vector<BusSource> liveBuses (const std::vector<BusSource>& sources)
{
    std::vector<BusSource> out;
    for (const auto& s : sources)
        if (s.live)
            out.push_back (s);
    return out;
}

juce::String busName (const BusSource& s)
{
    return s.label.isNotEmpty() ? s.label : "Bus " + juce::String (s.slot);
}

juce::String rateHint (int sampleRate)
{
    return juce::String ((int) std::floor (sampleRate / 1000.0 + 0.5)) + "k";
}

} // namespace ni::ui

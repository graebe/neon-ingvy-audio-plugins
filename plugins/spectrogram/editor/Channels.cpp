// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

#include "Channels.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace ni::spectrogram
{

std::vector<Source> liveBuses (const std::vector<Source>& sources)
{
    std::vector<Source> out;
    for (const auto& s : sources)
        if (s.live)
            out.push_back (s);
    return out;
}

juce::String sourceName (const Source& s)
{
    return s.label.isNotEmpty() ? s.label : "Bus " + juce::String (s.slot);
}

juce::StringArray channelNames (const std::vector<Source>& sources)
{
    juce::StringArray names { "input" };
    for (const auto& s : liveBuses (sources))
        names.add (sourceName (s));
    return names;
}

/* A rate that has not arrived is 0, and 0 would refuse every bus as a
 * mismatch: so the first bus stands in until the session's rate is known. */
int referenceRate (const Transport& t, const std::vector<Source>& sources)
{
    if (t.sampleRate > 0)
        return t.sampleRate;
    const auto buses = liveBuses (sources);
    return buses.empty() ? 0 : buses.front().sampleRate;
}

juce::String viewSummary (const std::vector<int>& view, const juce::StringArray& names)
{
    juce::StringArray shown;
    for (int ch : view)
        if (ch >= 0 && ch < names.size())
            shown.add (names[ch]);
    if (shown.isEmpty())
        return "nothing";
    const auto joined = shown.joinIntoString (", ");
    return joined.length() <= 17 ? joined : shown[0] + " +" + juce::String (shown.size() - 1);
}

std::vector<ni::ui::CheckList::Option> viewOptions (const std::vector<Source>& sources, int rate)
{
    const auto buses = liveBuses (sources);
    std::vector<ni::ui::CheckList::Option> out;
    out.push_back ({ 0, "input", {}, false });
    for (size_t i = 0; i < buses.size(); ++i)
    {
        const auto& bus = buses[i];
        const bool off = bus.sampleRate != rate;
        out.push_back ({ (int) i + 1, sourceName (bus),
                         off ? juce::String ((int) std::floor (bus.sampleRate / 1000.0 + 0.5)) + "k" : juce::String(),
                         off });
    }
    return out;
}

std::vector<int> listenSlots (const std::vector<int>& view, int compareA, int compareB, bool clash,
                              const std::vector<Source>& sources)
{
    const auto buses = liveBuses (sources);
    std::set<int> want;
    for (int ch : view)
        if (ch > 0)
            want.insert (ch);
    if (clash)
    {
        want.insert (compareA);
        want.insert (compareB);
    }
    want.erase (0);

    std::vector<int> slots;
    int taken = 0;
    for (int ch : want)
    {
        if (taken++ == maxListen)
            break;
        if (ch >= 1 && ch <= (int) buses.size())
            slots.push_back (buses[(size_t) ch - 1].slot);
    }
    return slots;
}

} // namespace ni::spectrogram

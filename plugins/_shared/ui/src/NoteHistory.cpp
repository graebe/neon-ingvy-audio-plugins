// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The history. NoteHistory.h has its rules.
 */
#include "NoteHistory.h"

#include <algorithm>

namespace ni::ui
{

NoteHistory::NoteHistory (double keepQuarters) : keep (std::max (1.0, keepQuarters)) {}

void NoteHistory::start (int midi, int velocity, double at)
{
    if (midi < 0 || midi > 127)
        return;
    for (const auto& n : played)
        if (n.midi == midi && n.sounding())
            return;
    played.push_back ({ midi, velocity, at });
}

void NoteHistory::stop (int midi, double at)
{
    for (auto it = played.rbegin(); it != played.rend(); ++it)
        if (it->midi == midi && it->sounding())
        {
            it->end = std::max (it->start, at);
            return;
        }
}

void NoteHistory::stopAll (double at)
{
    for (auto& n : played)
        if (n.sounding())
            n.end = std::max (n.start, at);
}

void NoteHistory::mark (double at, const juce::String& text)
{
    if (! names.empty() && names.back().text == text)
        return;
    names.push_back ({ at, text });
}

void NoteHistory::setClock (const Clock& c)
{
    now = c;
    prune();
}

std::pair<int, int> NoteHistory::range (double from) const
{
    int low = -1, high = -1;
    for (const auto& n : played)
    {
        if (n.end < from)
            continue;
        low = low < 0 ? n.midi : std::min (low, n.midi);
        high = std::max (high, n.midi);
    }
    return { low, high };
}

void NoteHistory::clear()
{
    played.clear();
    names.clear();
}

void NoteHistory::prune()
{
    const double oldest = now.now - keep;
    /* Not just from the front: a note still sounding there would keep every
     * finished one after it. */
    played.erase (std::remove_if (played.begin(), played.end(), [&] (const Note& n) { return n.end < oldest; }),
                  played.end());
    /* The last mark before the window still names what is on screen at its
     * left edge, so it stays. */
    while (names.size() > 1 && names[1].at < oldest)
        names.pop_front();
}

} // namespace ni::ui

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A Listen-In bus as a picker lists it -- the Spectrogram's channels, the
 * Side-Chain's kick. What a bus is to a person: its slot, whether something
 * publishes on it now, the rate it publishes at, and the name typed into its
 * Listen-In. The editors are handed these by their models (abus_probe is the
 * plugin's, not the kit's).
 */
#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace ni::ui
{

/* A Listen-In bus that exists, sending or not: a muted one is still where the
 * user put it. */
struct BusSource
{
    int slot = 0;            // 1-based, the bus's identity
    bool live = false;       // publishing now
    int sampleRate = 0;      // the sender's
    juce::String label;      // what was typed into the Listen-In; may be empty

    bool operator== (const BusSource& o) const
    {
        return slot == o.slot && live == o.live && sampleRate == o.sampleRate && label == o.label;
    }
    bool operator!= (const BusSource& o) const { return ! (*this == o); }
};

/* The live buses, in slot order. */
std::vector<BusSource> liveBuses (const std::vector<BusSource>& sources);

/* A bus's name in a picker: its label, or "Bus <slot>" for a Listen-In nobody
 * named -- it is still a bus somebody inserted. */
juce::String busName (const BusSource&);

/* A rate as a picker hints it: "44k", "96k". */
juce::String rateHint (int sampleRate);

} // namespace ni::ui

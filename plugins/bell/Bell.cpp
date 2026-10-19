// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Bell: the single filters' shell (plugins/_shared/single-filter) with
 * a bell in it.
 */
#include "SingleFilter.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ni::sf::Processor (SF_SHAPE_BELL);
}

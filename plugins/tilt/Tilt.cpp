// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Tilt: the single filters' shell (plugins/_shared/single-filter) with
 * a tilt in it.
 */
#include "SingleFilter.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ni::sf::Processor (SF_SHAPE_TILT);
}

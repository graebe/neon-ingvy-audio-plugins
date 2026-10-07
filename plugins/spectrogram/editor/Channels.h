// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The channels the window can look at and compare: this track, then every
 * live Listen-In bus in slot order -- the order the plugin opens them in, so
 * channel n here is channel n there. The web editor's busList, channelNames,
 * viewSummary, viewOptions and neededSlots (App.jsx), as plain functions.
 *
 * TWO SEPARATE QUESTIONS, ONE LIST. The view is what the picture is OF (the
 * channels ADDED by the engine into one stream); the comparison is what the
 * clash measures, which has nothing to do with what is on screen. Which buses
 * the plugin must open is neither a third control nor a third list to keep in
 * step: it is DERIVED from those two (listenSlots).
 */
#pragma once

#include "CheckList.h"
#include "Model.h"

#include <vector>

namespace ni::spectrogram
{

/* Buses this window reads at once beside its own input: the receiver's
 * SRECV_MAX_SOURCES less the own channel. */
inline constexpr int maxListen = 3;

/* The live buses, in slot order: channel n is the (n-1)-th of these. */
std::vector<Source> liveBuses (const std::vector<Source>& sources);

/* A bus's name in the picker: its label, or "Bus <slot>" for a Listen-In
 * nobody named -- it is still a bus somebody inserted. */
juce::String sourceName (const Source&);

/* "input", then each live bus: "input" rather than "this track", because it
 * has to fit a 112 px field. */
juce::StringArray channelNames (const std::vector<Source>& sources);

/* The rate a bus has to match: the session's, else the first bus's, else 0. */
int referenceRate (const Transport&, const std::vector<Source>& sources);

/* The view's face: the names while they fit in 17 characters, then the
 * first and a count ("bass +2"), and "nothing" for none. */
juce::String viewSummary (const std::vector<int>& view, const juce::StringArray& names);

/* The View list's rows: every channel, a bus at another rate shown and
 * refused, with its rate as the hint ("96k"). */
std::vector<ni::ui::CheckList::Option> viewOptions (const std::vector<Source>& sources, int rate);

/* The buses to open: every bus the view shows, and both ends of the
 * comparison while the clash is on -- at most maxListen, the lowest
 * channels first, as slots. */
std::vector<int> listenSlots (const std::vector<int>& view, int compareA, int compareB, bool clash,
                              const std::vector<Source>& sources);

} // namespace ni::spectrogram

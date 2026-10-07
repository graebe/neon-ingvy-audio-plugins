// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The boxes the Ground's rings come from and break against.
 *
 * "Every box edge and the window border emit one slow ring ... The rings
 * travel through the background only; panels, wells and the step grid are
 * solid to them" (README, Motion). Which boxes those are is the window's
 * layout, so they are marked on the components themselves -- the web kit's
 * Ground found them by class (.panel, .plot, .well, .grid, .ring) and by
 * [data-wave-source] -- and the window collects them whenever its layout
 * settles (EditorFrame).
 *
 * The kit's boxes mark themselves: Panel, PlotWell and the Spectrogram's
 * picture. Anything else that is one in the design's sense -- a step grid, a
 * ring -- is marked by whoever builds it, with setWaveSource.
 */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace ni::ui
{

void setWaveSource (juce::Component&, bool isSource = true);
bool isWaveSource (const juce::Component&);

/* Every visible, marked component under `root` (root itself included), as a
 * rectangle in `relativeTo`'s coordinates, in tree order. A marked component's
 * own children are not searched: a box inside a box adds no wall. */
std::vector<juce::Rectangle<float>> collectWaveSources (const juce::Component& root,
                                                       const juce::Component& relativeTo);

} // namespace ni::ui

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The host's window around a product's editor. PluginEditor.h says what it
 * keeps fixed.
 */
#include "PluginEditor.h"

#include <cmath>

namespace ni
{

PluginEditor::PluginEditor (juce::AudioProcessor& p, std::unique_ptr<juce::Component> d, int width, int height,
                            juce::RangedAudioParameter* zoomParameter, std::vector<float> zoomScales)
    : juce::AudioProcessorEditor (p),
      design (std::move (d)),
      fit (*design, width, height),
      scales (std::move (zoomScales))
{
    setLookAndFeel (&look->lookAndFeel);
    setOpaque (true);
    setResizable (false, false);
    addAndMakeVisible (fit);
    if (zoomParameter != nullptr && ! scales.empty())
        zoom = std::make_unique<ni::ui::ParamBinding> (*zoomParameter, [this] { applyZoom(); });
    applyZoom();
}

PluginEditor::~PluginEditor()
{
    setLookAndFeel (nullptr);
}

float PluginEditor::scale() const noexcept
{
    if (zoom == nullptr)
        return 1.0f;
    const int steps = std::max (1, (int) scales.size() - 1);
    const int choice = (int) std::lround (zoom->value() * (float) steps);
    return scales[(size_t) std::clamp (choice, 0, (int) scales.size() - 1)];
}

void PluginEditor::applyZoom()
{
    const auto bounds = fit.boundsAt (scale());
    if (bounds.getWidth() != getWidth() || bounds.getHeight() != getHeight())
        setSize (bounds.getWidth(), bounds.getHeight());
}

void PluginEditor::resized()
{
    fit.setBounds (getLocalBounds());
}

} // namespace ni

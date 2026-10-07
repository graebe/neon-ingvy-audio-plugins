// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The host's window around a product's editor. PluginEditor.h says what it
 * keeps fixed.
 */
#include "PluginEditor.h"

#include "Processor.h"

#include <cmath>

namespace ni
{

PluginEditor::PluginEditor (juce::AudioProcessor& p, std::unique_ptr<juce::Component> d, int width, int height,
                            juce::RangedAudioParameter* zoomParameter, std::vector<float> zoomScales)
    : juce::AudioProcessorEditor (p),
      design (std::move (d)),
      fit (std::make_unique<ni::ui::FixedDesign> (*design, width, height)),
      scales (std::move (zoomScales))
{
    setLookAndFeel (&look->lookAndFeel);
    setOpaque (true);
    setResizable (false, false);
    addAndMakeVisible (*fit);
    if (zoomParameter != nullptr && ! scales.empty())
        zoom = std::make_unique<ni::ui::ParamBinding> (*zoomParameter, [this] { applyZoom(); });
    applyZoom();
    opened();
}

PluginEditor::PluginEditor (juce::AudioProcessor& p, std::unique_ptr<juce::Component> d, FollowDesign)
    : juce::AudioProcessorEditor (p),
      design (std::move (d))
{
    setLookAndFeel (&look->lookAndFeel);
    setOpaque (true);
    setResizable (false, false);
    addAndMakeVisible (*design);
    setSize (design->getWidth(), design->getHeight());
    opened();
}

PluginEditor::~PluginEditor()
{
    if (auto* p = dynamic_cast<ni::Processor*> (&processor))
        p->editorClosed();
    setLookAndFeel (nullptr);
}

void PluginEditor::opened()
{
    if (auto* p = dynamic_cast<ni::Processor*> (&processor))
        p->editorOpened();
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
    const auto bounds = fit->boundsAt (scale());
    if (bounds.getWidth() != getWidth() || bounds.getHeight() != getHeight())
        setSize (bounds.getWidth(), bounds.getHeight());
}

void PluginEditor::resized()
{
    if (fit != nullptr)
        fit->setBounds (getLocalBounds());
    else
        design->setTopLeftPosition (0, 0);
}

/* A design that sizes itself asked for a new size: the window takes it, and
 * with it the host. */
void PluginEditor::childBoundsChanged (juce::Component* child)
{
    if (fit == nullptr && child == design.get()
        && (design->getWidth() != getWidth() || design->getHeight() != getHeight()))
        setSize (design->getWidth(), design->getHeight());
}

} // namespace ni

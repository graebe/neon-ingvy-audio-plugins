// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The window a host opens: a product's editor at its design size, in the
 * kit's look, scaled by the product's Zoom.
 *
 * THE DESIGN IS ONE COMPONENT AT ONE SIZE (native-ui.md: "a fixed design size,
 * scaled to fit"). This holds it in an ni::ui::FixedDesign, so it is laid out
 * in design pixels whatever the window's size, and sizes the window to the
 * design times the zoom: a Zoom parameter's choice picks one of `scales`, and
 * a change from the host or the editor resizes the window at once. A window
 * without a Zoom is the design's size. Not resizable by dragging: the design
 * system's windows have a fixed size, and the steps are the user's choice.
 *
 * A DESIGN THAT SIZES ITSELF -- the Trance Gate's grows a row of pads at a
 * time with its Length, and scales itself -- is shown as it is instead
 * (FollowDesign): the window is the design's size and follows it whenever the
 * design changes it, so the host is asked for each new size.
 *
 * THE KIT'S LOOK is the default while any editor is open
 * (uv::SharedLookAndFeel), so every font JUCE makes inside it is an embedded
 * face.
 */
#pragma once

#include "Fit.h"
#include "ParamBinding.h"
#include "UvLookAndFeel.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <vector>

namespace ni
{

class PluginEditor final : public juce::AudioProcessorEditor
{
public:
    /* `design` is laid out at width x height. `zoom` is null, or a choice
     * parameter whose n-th choice is scales[n]. */
    PluginEditor (juce::AudioProcessor&, std::unique_ptr<juce::Component> design, int width, int height,
                  juce::RangedAudioParameter* zoom = nullptr, std::vector<float> scales = {});

    /* `design` sizes itself, and the window follows it. */
    struct FollowDesign
    {
    };
    PluginEditor (juce::AudioProcessor&, std::unique_ptr<juce::Component> design, FollowDesign);
    ~PluginEditor() override;

    float scale() const noexcept;

    juce::Component& getDesign() noexcept { return *design; }

    void resized() override;
    void childBoundsChanged (juce::Component*) override;

private:
    void applyZoom();
    /* Tells an ni::Processor its window is open; the destructor, closed. */
    void opened();

    juce::SharedResourcePointer<uv::SharedLookAndFeel> look;
    std::unique_ptr<juce::Component> design;
    /* Null for a design that sizes itself. */
    std::unique_ptr<ni::ui::FixedDesign> fit;
    std::vector<float> scales;
    std::unique_ptr<ni::ui::ParamBinding> zoom;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};

} // namespace ni

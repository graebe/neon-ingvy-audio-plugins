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
    ~PluginEditor() override;

    float scale() const noexcept;

    void resized() override;

private:
    void applyZoom();

    juce::SharedResourcePointer<uv::SharedLookAndFeel> look;
    std::unique_ptr<juce::Component> design;
    ni::ui::FixedDesign fit;
    std::vector<float> scales;
    std::unique_ptr<ni::ui::ParamBinding> zoom;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};

} // namespace ni

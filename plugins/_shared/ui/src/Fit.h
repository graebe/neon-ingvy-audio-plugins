// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A window of fixed design size, scaled to whatever size its host gives it --
 * the web kit's lib/fit.js, for JUCE.
 *
 * Every editor is designed at one size ("A plugin window has a fixed size and
 * no scrollbar", README, Spacing and layout) and laid out in that size's
 * pixels, always. When the host makes the window bigger or smaller -- Live's
 * own zoom, a user dragging the corner -- the whole design scales, so every
 * proportion, every 4px step of the grid and every hairline stays where the
 * design put it relative to the rest. Nothing reflows, nothing scrolls,
 * nothing is laid out past the edge.
 *
 * HOW: the design is one component at its design size, and FixedDesign
 * gives it a scale transform from its top-left corner (CSS's
 * `transform-origin: top left`). JUCE maps the mouse and the focus through
 * the transform, so a control at design coordinates is hit where it is drawn.
 * An editor keeps its host-facing size in proportion with
 * constrainToDesign(), so the scale is the same both ways and nothing is
 * letterboxed.
 *
 * This is the USER's scale. The display's -- a Retina screen, Windows'
 * scaling -- is the host's and JUCE's (AudioProcessorEditor::setScaleFactor)
 * and multiplies with it; an editor sees neither, only its design pixels.
 */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ni::ui
{

/* The smallest scale the window goes to: fit.js's floor. */
inline constexpr float minFitScale = 0.1f;

/* The factor that fits a design `designWidth` wide into `viewportWidth`; a
 * viewport of no width (not laid out yet) is the design's own. Never below
 * minFitScale. */
float fitScale (float viewportWidth, float designWidth);

/* The height a design `designHeight` tall takes at `scale`, rounded up: what
 * the host is asked for. */
int scaledHeight (float designHeight, float scale);

/* An editor's resize rules: its aspect is the design's, and it goes from
 * `minScale` to `maxScale` of it. */
void constrainToDesign (juce::ComponentBoundsConstrainer&, int designWidth, int designHeight,
                        float minScale = 0.5f, float maxScale = 2.0f);

class FixedDesign final : public juce::Component
{
public:
    /* `design` is laid out at designWidth x designHeight and must outlive
     * this; it becomes this component's only child. */
    FixedDesign (juce::Component& design, int designWidth, int designHeight);
    ~FixedDesign() override;

    float getScale() const noexcept { return scale; }
    int getDesignWidth() const noexcept { return width; }
    int getDesignHeight() const noexcept { return height; }

    /* This component's size at `scale`: what to ask the host for. */
    juce::Rectangle<int> boundsAt (float scale) const;

    void resized() override;

private:
    juce::Component& design;
    const int width, height;
    float scale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE (FixedDesign)
};

} // namespace ni::ui

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A state the plugin reports: the Toggle card's LED form (bundle.css .ph-led).
 *
 * AN 8px ROUND LENS, then its label 8px to the right in the `label` style
 * (11px capitals, tracked, ink-muted), on a control-h row so it lines up with
 * a select or a meter beside it. The lens says the state:
 *
 *   off     ink-dim, no light
 *   on      uv with glow-led
 *   warn    amber with its own 6px halo (.ph-led.warn)  -- armed, or a state
 *           worth a look; the window's one amber mark
 *   clip    red with its own 6px halo (.ph-led.clip)    -- clipping, or a
 *           failure
 *
 * "A user-settable boolean is a switch; a state the plugin reports is an LED"
 * (Toggle card): an LED takes no press and no keyboard focus. It does take the
 * pointer, for nothing but its info line -- a status says WHAT, and the line
 * under the pointer says why (Info.h). To a screen reader it is a label whose
 * title is the label text.
 *
 * "An LED switches without a fade" (Motion): setStatus repaints, nothing
 * animates.
 *
 * The halo reaches past the lens and past the row, so the LED is Luminous: the
 * part of its light outside its bounds is its parent's to paint (Luminous.h).
 */
#pragma once

#include "Luminous.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ni::ui
{

class Led final : public juce::Component, public Luminous
{
public:
    enum class Status
    {
        off,
        on,
        warn,
        clip,
    };

    explicit Led (const juce::String& label = {});
    ~Led() override;

    /* The lens, and the gap to the label: .ph-led .lens, .ph-led { gap }. */
    static constexpr float lensSize = 8.0f;
    static constexpr float labelGap = 8.0f;

    void setStatus (Status);
    Status getStatus() const noexcept { return status; }

    void setLabel (const juce::String&);
    const juce::String& getLabel() const noexcept { return label; }

    /* The lens, the gap and the label: its own width. */
    int idealWidth() const;
    /* The same for another label: what a caller sizes a fixed cell by, so the
     * row does not move when the label changes. */
    static int idealWidthFor (const juce::String& label);

    /* The lens, in its own coordinates: on the left, centred vertically. */
    juce::Rectangle<float> lens() const;

    void paint (juce::Graphics&) override;
    void paintLight (juce::Graphics&) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    void drawHalo (juce::Graphics&) const;

    juce::String label;
    Status status = Status::off;

    JUCE_DECLARE_NON_COPYABLE (Led)
};

} // namespace ni::ui

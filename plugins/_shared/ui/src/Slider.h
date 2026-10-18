// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A slider: the Slider card of Ultraviolet, vertical or horizontal.
 *
 * THE CARD: a bg-200 track on a line-100 hairline -- 24 wide and as tall as
 * it is laid out, or 24 tall and as wide; inside it, 8px from either end, a
 * 2px line-200 rail; over the rail from its start to the value, the uv fill
 * with its uv-deep glow (box-shadow 0 0 6px); and the thumb, a 16 x 6 ink bar
 * with 2px corners (6 x 16 lying down), centred on the value. Disabled, the
 * fill and the thumb are ink-dim. Keyboard focus is glow-focus round the
 * track.
 *
 * WHERE THE SYSTEM PUTS ONE, rather than the default knob: a level (wet,
 * dry, gain) or a mixer-style row of levels, standing; a single time-like
 * setting (swing), lying down -- never both in one row. The label and the
 * value text are the owner's, beside it: the card draws neither.
 *
 * INTERACTION, the Interaction conventions' (Ultraviolet 2.1.0): a slider
 * is clicked where it should be -- on a rail, where the pointer is is the
 * value -- where a knob is pulled up and down.
 *
 *   press        the value jumps to the pointer, and a drag follows it along
 *                the rail; Shift held from the press drags five times finer,
 *                from where the value was, not from the pointer. The gesture
 *                opens on the FIRST MOVE or on a press that changed the value
 *                (onBegin)
 *   double-click onReset
 *   arrows       a step, 1 % (Shift: 0.2 %), Page a page, Home and End the
 *                ends: each key one complete edit (onCommit)
 *
 * IT KNOWS NOTHING ABOUT WHAT IT SETS: 0 to 1 in, 0 to 1 out. setValue sends
 * nothing.
 */
#pragma once

#include "Focus.h"
#include "Luminous.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>

namespace ni::ui
{

class Slider : public juce::Component,
               public Luminous
{
public:
    enum class Orientation
    {
        vertical,
        horizontal,
    };

    /* The card's numbers. */
    static constexpr int thickness = 24;
    static constexpr int inset = 8;
    static constexpr double fineRatio = 5.0;

    explicit Slider (Orientation = Orientation::horizontal);
    ~Slider() override;

    Orientation getOrientation() const noexcept { return orientation; }

    /* What it shows, 0 to 1. Sends nothing. */
    void setValue (float normalised);
    float getValue() const noexcept { return value; }

    /* The text a screen reader reads for the value: the owner's. */
    void setValueText (const juce::String&);
    const juce::String& getValueText() const noexcept { return valueText; }

    /* ---- what it asks for */
    std::function<void()> onBegin;
    std::function<void (float normalised)> onInput;
    std::function<void()> onEnd;
    std::function<void (float normalised)> onCommit;
    std::function<void()> onReset;

    /* The track, the rail's travel, and the thumb, in its own coordinates. */
    juce::Rectangle<float> track() const;
    juce::Rectangle<float> rail() const;
    juce::Rectangle<float> thumb() const;

    /* The value under a point, from the rail. */
    float valueAt (juce::Point<float>) const;

    void paint (juce::Graphics&) override;
    void paintLight (juce::Graphics&) override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override;
    void focusLost (FocusChangeType) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    struct Drag
    {
        juce::Point<float> from;
        float startValue;
        bool fine;
        bool begun;
    };

    float along (juce::Point<float>) const;
    void input (float);

    const Orientation orientation;
    float value = 0.0f;
    juce::String valueText;
    std::optional<Drag> drag;
    FocusVisibility focus { *this };

    JUCE_DECLARE_NON_COPYABLE (Slider)
};

} // namespace ni::ui

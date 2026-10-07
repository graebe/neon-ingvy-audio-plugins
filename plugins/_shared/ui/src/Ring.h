// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The pattern on the window's face, editable where it is drawn: the Ring
 * card's editable variant (Ultraviolet 1.1.0), and the Trance Gate web
 * editor's Ring.jsx.
 *
 * THE DRAWING, from the card, in fractions of its size D (240 in a window):
 *
 *   wedges     one annular wedge per step, filling 80 % of its slot, the
 *              first starting at 12 o'clock and the rest clockwise; the outer
 *              edge at D x 114/240, the band min(34, max(24, 900/n)) deep at
 *              240 -- 34px at 16 steps, never under 24 -- narrowing from its
 *              inner edge as the count rises, so the ring never changes size
 *   rail       every wedge in line-200 first, a lit one included, so a
 *              partial amount has a track to sit in
 *   on         uv from the inner edge outward, the lit depth being its amount
 *              and never under 5 %, with glow-led
 *   tie        a 1px uv outline of the wedge with a uv arc across its middle
 *              (45 % to 55 % of the band), as on a Step
 *   pending    drawn on or tied but not sounding yet (a fade has not brought
 *              it in): a 1px uv outline of the wedge, no fill and no glow --
 *              hollow, so it is never mistaken for a gap, as on a Step
 *   filled     drawn off but sounding (a hole Fade Out has not removed yet):
 *              uv at Step::filledAlpha from the inner edge, the lit depth
 *              being its level, with no outline and no glow -- what sounds is
 *              filled, and it is never mistaken for a step that was drawn
 *   cursor     the step being edited: a 1px ink outline from 3px inside the
 *              band to 3px outside it
 *   playhead   an ink dot of D x 7/240 just inside the band at the step being
 *              played, with glow-led, while the transport runs
 *   centre     the count as a `readout` and its `label` under it
 *   focus      glow-focus round the ring, for the keyboard only
 *
 * THE POINTER. A press on a wedge is onPress (step, shift); the owner says
 * whether a sweep may follow, and a sweep then calls onSweep once for every
 * further wedge the pointer crosses -- painting what the press set, never
 * toggling each in turn. The hit band reaches 6px past either edge of the
 * wedges (a 1px wedge at 128 steps is otherwise unhittable); the hole in the
 * middle is not a button.
 *
 * THE KEYBOARD: the ring is its count. countKey (Keys.h): the arrows by one,
 * Page Up and Down to the next detent (by four where there is none), Home and
 * End to the ends -- each onCount (n). It draws no detent ticks: its angle is
 * a position in the pattern, not a count. To a screen reader it is a slider
 * named by its title ("Length"), its value read by valueText ("16 steps").
 *
 * The centre carries its own info line (setCentreInfo); the rest of the ring
 * the ring's. Solid to the Ground's rings; its light reaches past its box,
 * so it is Luminous.
 */
#pragma once

#include "Focus.h"
#include "Luminous.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace ni::ui
{

class Ring : public juce::Component,
             public Luminous
{
public:
    /* The window's ring: "the 240px ring once per window, as its face". */
    static constexpr int defaultSize = 240;
    static constexpr int maxSteps = 128;
    /* How far past the band a press still lands on a wedge. */
    static constexpr float hitSlack = 6.0f;

    /* What one wedge shows. */
    struct StepState
    {
        bool on = false;
        bool tie = false;
        float amount = 1.0f;   // the lit depth of an on or filled step, 0..1
        bool pending = false;
        bool filled = false;

        bool operator== (const StepState& o) const
        {
            return on == o.on && tie == o.tie && pending == o.pending && filled == o.filled
                && juce::exactlyEqual (amount, o.amount);
        }
        bool operator!= (const StepState& o) const { return ! (*this == o); }
    };

    /* The radii of the band at `size` for `count` steps. */
    struct Band
    {
        float centre, outer, inner;
    };
    static Band bandFor (float size, int count);

    Ring();
    ~Ring() override;

    /* The count, its range and its detents (in steps). */
    void setCount (int);
    int getCount() const noexcept { return count; }
    void setRange (int min, int max);
    void setDetents (std::vector<double> steps);
    const std::vector<double>& getDetents() const noexcept { return detents; }

    void setStep (int index, const StepState&);
    const StepState& getStep (int index) const;
    /* -1 for none. */
    void setCursor (int);
    /* The step being played, and whether the transport runs. */
    void setPlayhead (int step, bool playing);
    int getPlayhead() const noexcept { return playhead; }
    bool isPlaying() const noexcept { return playing; }

    /* The centre's number and the label under it. */
    void setCentre (const juce::String& value, const juce::String& label);
    const juce::String& getCentreValue() const noexcept { return centreValue; }

    /* The centre's own info line. */
    void setCentreInfo (const juce::String&);
    juce::Component& centre() noexcept;

    /* The value a screen reader hears for a count ("16 steps"). */
    std::function<juce::String (int count)> valueText;

    /* The wedge under a point in the ring's coordinates, or -1: the band and
     * its slack, never the hole. */
    int stepAt (juce::Point<float>) const;
    /* A wedge between two radii, as drawn. */
    juce::Path wedge (int index, float innerRadius, float outerRadius) const;
    Band band() const;

    /* ---- what it asks for */
    std::function<bool (int step, bool shift)> onPress;
    std::function<void (int step)> onSweep;
    std::function<void (int count)> onCount;

    bool isFocusShown() const { return focus.isVisible(); }

    void paint (juce::Graphics&) override;
    void paintLight (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override;
    void focusLost (FocusChangeType) override;

private:
    class Centre;

    /* What glows: the lit wedges and the ties. A pending wedge and a filled
     * one do not, as a Step that is neither on nor tied does not. */
    juce::Path litPath() const;
    /* The lit depth of a wedge, between the band's inner edge and this. */
    float litOuter (const StepState&, const Band&) const;
    void changed();
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    int count = 16, minCount = 1, maxCount = maxSteps;
    std::vector<double> detents;
    std::array<StepState, maxSteps> steps {};
    int cursor = -1;
    int playhead = -1;
    bool playing = false;
    juce::String centreValue, centreLabel;

    std::unique_ptr<Centre> centreBox;
    FocusVisibility focus { *this };

    /* The sweep under way: the wedges it has painted. */
    bool sweeping = false;
    std::array<bool, maxSteps> swept {};

    JUCE_DECLARE_NON_COPYABLE (Ring)
};

} // namespace ni::ui

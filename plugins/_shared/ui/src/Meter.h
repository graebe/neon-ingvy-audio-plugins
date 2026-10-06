// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A level, read-only -- the Meter card of Ultraviolet 1.1.0, which Listen-In's
 * local Meter.jsx became.
 *
 * A 12px (space-3) bg-200 bed on a line-100 hairline, square, filled from the
 * left in flat uv with its glow (box-shadow 0 0 6px uv-deep), as wide as the
 * row gives it. No gradient, no peak colours, no transition: the fill jumps to
 * the level, as every value does ("Controls never animate").
 *
 * THE SCALE IS THE PLUGIN'S. setLevel takes 0..1 on it -- linear in dB, not in
 * amplitude, so a quiet part that is plainly audible does not read as an empty
 * bar. The meter maps nothing (Listen-In's meterFraction is the plugin's).
 *
 * LIVE OR OFF. A meter whose signal is measured but goes nowhere -- an input
 * that is not being published -- fills in line-200 without glow, so "nothing
 * is playing" and "playing, going nowhere" look different.
 *
 * Display only: it takes no pointer and no keyboard (a level the user SETS is
 * a Slider). To assistive technology it is what the web's role="meter" is: a
 * progress bar from 0 to 100, titled by setTitle ("Input level").
 *
 * The glow reaches past the bed (6px of blur radius), so the meter is
 * Luminous: its container paints the light outside it (Luminous.h), and the
 * meter paints the part that falls on its own bed.
 */
#pragma once

#include "Luminous.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ni::ui
{

class Meter final : public juce::Component, public Luminous
{
public:
    Meter();

    /* The bed's height: space-3. A meter is as wide as its row, at least 64. */
    static constexpr int height = 12;
    static constexpr int minWidth = 64;

    /* 0..1 on the plugin's scale; clamped. */
    void setLevel (float level);
    float getLevel() const noexcept { return level; }

    /* Whether the signal goes anywhere: live fills in uv with glow, off in
     * line-200 without. */
    void setLive (bool);
    bool isLive() const noexcept { return live; }

    /* The fill, in this component's coordinates: inside the hairline, from
     * the left, level of the way across. */
    juce::Rectangle<float> fillBounds() const;

    void paint (juce::Graphics&) override;
    void paintLight (juce::Graphics&) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    void drawGlow (juce::Graphics&) const;

    float level = 0.0f;
    bool live = true;

    JUCE_DECLARE_NON_COPYABLE (Meter)
};

} // namespace ni::ui

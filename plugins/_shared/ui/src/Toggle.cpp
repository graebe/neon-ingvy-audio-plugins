// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A switch. Toggle.h has its states.
 */
#include "Toggle.h"

#include "ChildLights.h"
#include "UvLight.h"
#include "UvTokens.h"
#include "UvType.h"

#include <cmath>

namespace ni::ui
{

namespace
{
namespace c = uv::tok::colour;

constexpr float hair = uv::tok::stroke::strokeHair;

/* .switch { width: 28px; height: 14px }, .switch-knob { 8 x 8 at 2px, 16px
 * on }, positioned inside the hairline. */
constexpr float housingW = 28.0f;
constexpr float housingH = 14.0f;
constexpr float squareSize = 8.0f;
constexpr float squareOff = 2.0f;
constexpr float squareOn = 16.0f;
} // namespace

Toggle::Toggle (const juce::String& text)
    : Pressable (juce::AccessibilityRole::toggleButton)
{
    setLabel (text);
}

Toggle::~Toggle() = default;

void Toggle::setLabel (const juce::String& text)
{
    label = text;
    setTitle (text);
    stateChanged();
}

void Toggle::setOn (bool shouldBeOn)
{
    if (on == shouldBeOn)
        return;
    on = shouldBeOn;
    checkedChanged();
}

int Toggle::idealWidth() const
{
    const auto& style = uv::tok::type::label;
    const float words = label.isEmpty() ? 0.0f
                                        : uv::tok::space::space2
                                            + uv::type::width (uv::type::font (style), uv::type::cased (style, label));
    return (int) std::ceil (housingW + words);
}

juce::Rectangle<float> Toggle::housing() const
{
    return { 0.0f, std::round (((float) getHeight() - housingH) * 0.5f), housingW, housingH };
}

juce::Rectangle<float> Toggle::square() const
{
    const auto h = housing();
    return { h.getX() + hair + (on ? squareOn : squareOff), h.getY() + hair + squareOff, squareSize, squareSize };
}

bool Toggle::isHousingHovered() const
{
    return isHovered() && housing().contains (pointerPosition());
}

void Toggle::mouseMove (const juce::MouseEvent& e)
{
    const bool before = isHousingHovered();
    Pressable::mouseMove (e);
    if (before != isHousingHovered())
        repaint();
}

void Toggle::pressed()
{
    if (onChange)
        onChange (! on);   // may delete this
}

void Toggle::paint (juce::Graphics& g)
{
    const bool live = isEnabled();
    const auto box = housing();

    if (live)
    {
        g.setColour (isHousingHovered() ? c::bg300 : c::bg200);
        g.fillRect (box);
    }
    g.setColour (! live ? c::line100 : (on ? c::uv : c::line200));
    g.drawRect (box, hair);

    /* The lit square's halo is painted with the square, over its own housing
     * and its hairline, as CSS paints a filter on a child; the part past the
     * row is paintLight()'s. */
    if (on && live)
        uv::light::glowLed (g, square());
    g.setColour (! live ? c::line100 : (on ? c::uv : c::inkDim));
    g.fillRect (square());

    if (label.isNotEmpty())
    {
        const auto& style = uv::tok::type::label;
        uv::type::draw (g, uv::type::cased (style, label),
                        getLocalBounds().toFloat().withTrimmedLeft (housingW + uv::tok::space::space2),
                        uv::type::font (style), live ? c::inkMuted : c::inkDim);
    }
}

void Toggle::paintLight (juce::Graphics& g)
{
    if (on && isEnabled())
    {
        juce::Graphics::ScopedSaveState state (g);
        excludeOwnBounds (g, *this);
        uv::light::glowLed (g, square());
    }
    Pressable::paintLight (g);
}

} // namespace ni::ui

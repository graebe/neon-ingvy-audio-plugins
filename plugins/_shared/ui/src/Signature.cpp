// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The publisher's signature. Signature.h has the card.
 */
#include "Signature.h"

#include "UvLight.h"
#include "UvType.h"

#include <cmath>

namespace ni::ui
{

namespace c = uv::tok::colour;

namespace
{
juce::String words()
{
    return juce::String (Signature::name).toUpperCase();
}
} // namespace

Signature::Signature()
{
    /* aria-label="Neon Ingvy": what a screen reader says, whatever the
     * capitals the eye sees. */
    setTitle (name);
    setSize (preferredWidth(), height);
}

int Signature::preferredWidth()
{
    /* Tracking is CSS letter-spacing, after every letter, the last included:
     * uv::type::width measures it the same way. */
    const float text = uv::type::width (uv::type::font (style), words());
    return (int) std::ceil (markSize + gap + text);
}

juce::Rectangle<float> Signature::markBounds() const
{
    /* align-items: center -- the square sits in the middle of the line. */
    return { 0.0f, ((float) getHeight() - markSize) * 0.5f, markSize, markSize };
}

void Signature::paintLight (juce::Graphics& g)
{
    uv::light::glowLed (g, markBounds());
}

void Signature::paint (juce::Graphics& g)
{
    g.setColour (c::uv);
    g.fillRect (markBounds());

    uv::type::draw (g, words(),
                    getLocalBounds().toFloat().withTrimmedLeft (markSize + gap),
                    uv::type::font (style), c::inkMuted);
}

std::unique_ptr<juce::AccessibilityHandler> Signature::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::staticText);
}

} // namespace ni::ui

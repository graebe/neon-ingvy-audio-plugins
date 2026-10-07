// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A reported state. Led.h has the card.
 */
#include "Led.h"

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

/* .ph-led.warn .lens { box-shadow: 0 0 6px var(--amber) }, and .clip's in red:
 * box-shadows, so the 6px is a blur radius. */
constexpr uv::tok::ShadowLayer warnHalo { 0.0f, 0.0f, 6.0f, 0.0f, uv::tok::argb::ultraviolet::amber };
constexpr uv::tok::ShadowLayer clipHalo { 0.0f, 0.0f, 6.0f, 0.0f, uv::tok::argb::ultraviolet::red };

juce::Colour lensColour (Led::Status s)
{
    switch (s)
    {
        case Led::Status::on:   return c::uv;
        case Led::Status::warn: return c::amber;
        case Led::Status::clip: return c::red;
        case Led::Status::off:  break;
    }
    return c::inkDim;
}

float labelWidth (const juce::String& text)
{
    const auto& style = uv::tok::type::label;
    return text.isEmpty() ? 0.0f
                          : Led::labelGap + uv::type::width (uv::type::font (style), uv::type::cased (style, text));
}
} // namespace

Led::Led (const juce::String& text)
{
    /* The pointer, for the info line under it; nothing to press or focus. */
    setInterceptsMouseClicks (true, false);
    setWantsKeyboardFocus (false);
    setLabel (text);
    setSize (idealWidth(), (int) uv::tok::size::controlH);
}

Led::~Led() = default;

void Led::setStatus (Status s)
{
    if (s == status)
        return;
    status = s;
    repaint();
    relight (*this);
}

void Led::setLabel (const juce::String& text)
{
    if (text == label)
        return;
    label = text;
    setTitle (text);
    repaint();
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent (juce::AccessibilityEvent::titleChanged);
}

int Led::idealWidthFor (const juce::String& text)
{
    return (int) std::ceil (lensSize + labelWidth (text));
}

int Led::idealWidth() const
{
    return idealWidthFor (label);
}

juce::Rectangle<float> Led::lens() const
{
    return { 0.0f, std::round (((float) getHeight() - lensSize) * 0.5f), lensSize, lensSize };
}

void Led::drawHalo (juce::Graphics& g) const
{
    juce::Path shape;
    shape.addEllipse (lens());
    switch (status)
    {
        case Status::on:   uv::light::glowLed (g, shape); break;
        case Status::warn: uv::light::shadow (g, shape, warnHalo); break;
        case Status::clip: uv::light::shadow (g, shape, clipHalo); break;
        case Status::off:  break;
    }
}

void Led::paint (juce::Graphics& g)
{
    /* The halo's share inside the row, under the lens, as CSS paints a
     * box-shadow; the rest is paintLight()'s (ChildLights.h). */
    drawHalo (g);
    g.setColour (lensColour (status));
    g.fillEllipse (lens());

    if (label.isNotEmpty())
    {
        const auto& style = uv::tok::type::label;
        uv::type::draw (g, uv::type::cased (style, label),
                        getLocalBounds().toFloat().withTrimmedLeft (lensSize + labelGap),
                        uv::type::font (style), c::inkMuted);
    }
}

void Led::paintLight (juce::Graphics& g)
{
    if (status == Status::off)
        return;
    juce::Graphics::ScopedSaveState state (g);
    excludeOwnBounds (g, *this);
    drawHalo (g);
}

std::unique_ptr<juce::AccessibilityHandler> Led::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::staticText);
}

} // namespace ni::ui

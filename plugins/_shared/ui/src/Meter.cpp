// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The level meter. Meter.h has the card.
 */
#include "Meter.h"

#include "UvLight.h"
#include "UvTokens.h"

#include <cmath>

namespace ni::ui
{

namespace c = uv::tok::colour;

namespace
{
/* .ph-meter .fill { box-shadow: 0 0 6px var(--uv-deep) } -- a box-shadow, so
 * its 6px is a blur radius. */
constexpr uv::tok::ShadowLayer fillGlow { 0.0f, 0.0f, 6.0f, 0.0f, uv::tok::argb::ultraviolet::uvDeep };

/* role="meter", aria-valuemin 0, aria-valuemax 100. */
class MeterValue final : public juce::AccessibilityRangedNumericValueInterface
{
public:
    explicit MeterValue (const Meter& m) : meter (m) {}

    bool isReadOnly() const override { return true; }
    double getCurrentValue() const override { return std::round (meter.getLevel() * 100.0f); }
    void setValue (double) override {}
    AccessibleValueRange getRange() const override { return { { 0.0, 100.0 }, 1.0 }; }

private:
    const Meter& meter;
};
} // namespace

Meter::Meter()
{
    /* A display: nothing to press, nothing to focus. */
    setInterceptsMouseClicks (false, false);
    setWantsKeyboardFocus (false);
    setSize (minWidth, height);
}

void Meter::setLevel (float l)
{
    const float next = juce::jlimit (0.0f, 1.0f, std::isfinite (l) ? l : 0.0f);
    if (juce::exactlyEqual (next, level))
        return;
    level = next;
    repaint();
    lightChanged (*this);
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
}

void Meter::setLive (bool on)
{
    if (on == live)
        return;
    live = on;
    repaint();
    lightChanged (*this);
}

juce::Rectangle<float> Meter::fillBounds() const
{
    /* position: absolute inside the 1px border; the right edge on a whole
     * pixel, so a level reads as a hard edge rather than a smear. */
    const auto inside = getLocalBounds().toFloat().reduced (uv::tok::size::hairline);
    return inside.withWidth (std::round (inside.getWidth() * level));
}

void Meter::drawGlow (juce::Graphics& g) const
{
    const auto fill = fillBounds();
    if (live && ! fill.isEmpty())
        uv::light::shadow (g, fill, fillGlow);
}

void Meter::paintLight (juce::Graphics& g)
{
    drawGlow (g);
}

void Meter::paint (juce::Graphics& g)
{
    const auto bed = getLocalBounds().toFloat();
    g.setColour (c::bg200);
    g.fillRect (bed);
    g.setColour (c::line100);
    g.drawRect (bed, uv::tok::stroke::strokeHair);

    /* The fill's light over the bed and its hairline, as CSS paints a child's
     * box-shadow over its parent's border; then the fill over its light. */
    drawGlow (g);

    const auto fill = fillBounds();
    if (! fill.isEmpty())
    {
        g.setColour (live ? c::uv : c::line200);
        g.fillRect (fill);
    }
}

std::unique_ptr<juce::AccessibilityHandler> Meter::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler> (
        *this, juce::AccessibilityRole::progressBar, juce::AccessibilityActions(),
        juce::AccessibilityHandler::Interfaces { std::make_unique<MeterValue> (*this) });
}

} // namespace ni::ui

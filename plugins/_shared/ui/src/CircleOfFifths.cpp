// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The circle of fifths. CircleOfFifths.h has its looks and its keys.
 */
#include "CircleOfFifths.h"

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
/* Room outside the discs for the amber ring and the glow. */
constexpr float margin = 8.0f;
constexpr float ringGap = 4.0f;

bool has (std::uint16_t set, int pc) { return (set >> pc) & 1u; }

/* The tonic as an adjustable value: its place on the ring, 0 to 11, read out
 * as its name and settable by one. */
class TonicValue final : public juce::AccessibilityValueInterface
{
public:
    explicit TonicValue (CircleOfFifths& o) : owner (o) {}

    bool isReadOnly() const override { return false; }
    double getCurrentValue() const override { return CircleOfFifths::placeOf (owner.getState().tonic); }
    void setValue (double v) override { ask (CircleOfFifths::pitchAt ((int) std::lround (v))); }
    juce::String getCurrentValueAsString() const override { return owner.getState().names[(size_t) owner.getState().tonic]; }
    void setValueAsString (const juce::String& name) override
    {
        const auto& names = owner.getState().names;
        for (int pc = 0; pc < 12; ++pc)
            if (names[(size_t) pc].equalsIgnoreCase (name.trim()))
                return ask (pc);
    }
    AccessibleValueRange getRange() const override { return { { 0.0, 11.0 }, 1.0 }; }

private:
    void ask (int pc) const
    {
        if (owner.onTonicSelected)
            owner.onTonicSelected (pc);
    }

    CircleOfFifths& owner;
};
} // namespace

CircleOfFifths::CircleOfFifths()
{
    setWantsKeyboardFocus (true);
    setTitle ("Key");
}

CircleOfFifths::~CircleOfFifths() = default;

void CircleOfFifths::setState (const State& next)
{
    if (next == state)
        return;
    const bool tonicMoved = next.tonic != state.tonic;
    state = next;
    repaint();
    lightChanged (*this);
    if (tonicMoved)
        if (auto* handler = getAccessibilityHandler())
            handler->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
}

float CircleOfFifths::radius() const
{
    const float side = (float) std::min (getWidth(), getHeight());
    return std::max (0.0f, side * 0.5f - discSize * 0.5f - margin);
}

juce::Rectangle<float> CircleOfFifths::disc (int pitchClass) const
{
    const auto mid = getLocalBounds().toFloat().getCentre();
    const float angle = juce::MathConstants<float>::twoPi * (float) placeOf (pitchClass) / 12.0f;
    const juce::Point<float> at { mid.x + radius() * std::sin (angle), mid.y - radius() * std::cos (angle) };
    return juce::Rectangle<float> (discSize, discSize).withCentre (at);
}

juce::Rectangle<float> CircleOfFifths::centre() const
{
    /* The widest useful box inside the ring, clear of the discs by 4px: a
     * select and a line of text across, more than a square would give. */
    const float inner = std::max (0.0f, radius() - discSize * 0.5f - ringGap);
    const float w = inner * 1.65f;
    const float h = 2.0f * std::sqrt (std::max (0.0f, inner * inner - w * w * 0.25f));
    return juce::Rectangle<float> (w, h).withCentre (getLocalBounds().toFloat().getCentre());
}

int CircleOfFifths::keyAt (juce::Point<float> p) const
{
    for (int pc = 0; pc < 12; ++pc)
        if (disc (pc).getCentre().getDistanceFrom (p) <= discSize * 0.5f)
            return pc;
    return -1;
}

bool CircleOfFifths::glows (int pc) const
{
    return (has (state.lit, pc) || pc == state.root) && ! state.dimmed;
}

void CircleOfFifths::ask (int pitchClass)
{
    if (onTonicSelected)
        onTonicSelected (pitchClass);   // may delete this
}

void CircleOfFifths::paint (juce::Graphics& g)
{
    const auto mid = getLocalBounds().toFloat().getCentre();
    g.setColour (c::line100);
    g.drawEllipse (juce::Rectangle<float> (2.0f * radius(), 2.0f * radius()).withCentre (mid), hair);

    const auto lit = state.dimmed ? c::uvDeep : c::uv;
    const auto font = uv::type::font (uv::tok::type::button);

    for (int pc = 0; pc < 12; ++pc)
    {
        const auto box = disc (pc);
        const bool inScale = has (state.scale, pc);
        const bool sounding = has (state.lit, pc);
        const bool root = pc == state.root;

        juce::Colour fill = inScale ? c::bg200 : c::bg100;
        juce::Colour edge = inScale ? c::line200 : c::line100;
        juce::Colour ink = inScale ? c::inkMuted : c::inkDim;
        if (pc == hovered && ! root)
            fill = c::bg300;
        if (sounding)
        {
            edge = lit;
            ink = lit;
        }
        if (root)
        {
            fill = lit;
            edge = lit;
            ink = c::onUv;
        }

        juce::Path shape;
        shape.addEllipse (box);
        if (glows (pc))
            uv::light::glowLed (g, shape);
        g.setColour (fill);
        g.fillPath (shape);
        g.setColour (edge);
        g.strokePath (shape, juce::PathStrokeType (hair));
        uv::type::draw (g, state.names[(size_t) pc], box, font, ink, juce::Justification::centred);

        if (pc == state.tonic)
        {
            g.setColour (c::amber);
            g.drawEllipse (box.expanded (ringGap), hair);
            if (focus.isVisible())
                uv::light::glowFocus (g, box.expanded (ringGap), box.getWidth());
        }
    }

    if (state.caption.isNotEmpty())
    {
        const auto& hint = uv::tok::type::hint;
        const auto inner = centre();
        uv::type::draw (g, state.caption, inner.withTrimmedTop (inner.getHeight() - hint.lineHeight),
                        uv::type::font (hint), c::inkDim, juce::Justification::centred);
    }
}

void CircleOfFifths::paintLight (juce::Graphics& g)
{
    juce::Graphics::ScopedSaveState state_ (g);
    excludeOwnBounds (g, *this);
    for (int pc = 0; pc < 12; ++pc)
        if (glows (pc))
        {
            juce::Path shape;
            shape.addEllipse (disc (pc));
            uv::light::glowLed (g, shape);
        }
}

void CircleOfFifths::mouseMove (const juce::MouseEvent& e)
{
    const int now = keyAt (e.position);
    if (now != hovered)
    {
        hovered = now;
        repaint();
    }
}

void CircleOfFifths::mouseExit (const juce::MouseEvent&)
{
    if (hovered != -1)
    {
        hovered = -1;
        repaint();
    }
}

void CircleOfFifths::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isLeftButtonDown())
        return;
    focus.pointerUsed();
    const int pc = keyAt (e.position);
    if (pc >= 0)
        ask (pc);
}

bool CircleOfFifths::keyPressed (const juce::KeyPress& key)
{
    const int place = placeOf (state.tonic);
    int to = -1;
    if (key == juce::KeyPress::rightKey || key == juce::KeyPress::upKey)
        to = pitchAt (place + 1);
    else if (key == juce::KeyPress::leftKey || key == juce::KeyPress::downKey)
        to = pitchAt (place - 1);
    else if (key == juce::KeyPress::homeKey)
        to = 0;
    if (to < 0)
        return false;
    focus.keyUsed();
    repaint();
    ask (to);
    return true;
}

void CircleOfFifths::focusGained (FocusChangeType cause)
{
    focus.focusGained (cause);
    repaint();
}

void CircleOfFifths::focusLost (FocusChangeType)
{
    focus.focusLost();
    repaint();
}

std::unique_ptr<juce::AccessibilityHandler> CircleOfFifths::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler> (
        *this, juce::AccessibilityRole::slider, juce::AccessibilityActions(),
        juce::AccessibilityHandler::Interfaces { std::make_unique<TonicValue> (*this) });
}

} // namespace ni::ui

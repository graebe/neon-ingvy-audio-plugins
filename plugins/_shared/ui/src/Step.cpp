// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * One cell of a sequencer. Step.h has the card and the states.
 */
#include "Step.h"

#include "ChildLights.h"
#include "UvLight.h"
#include "UvTokens.h"
#include "UvType.h"

namespace ni::ui
{

namespace
{
namespace c = uv::tok::colour;

constexpr float hairline = uv::tok::size::hairline;

/* --dip: the window ground at a quarter, which reads as a shadow crossing a
 * lit step rather than a tint of it. */
juce::Colour dip()
{
    return c::bg000.withAlpha (0.25f);
}
} // namespace

Step::Step()
{
    setSize (size, size);
    /* What it does is the grid's; it only says what it is. */
    setInterceptsMouseClicks (true, true);
}

Step::~Step() = default;

void Step::setState (const State& s)
{
    if (s == state)
        return;
    const bool lightMoved = glows() || state.cursor;
    state = s;
    repaint();
    if (lightMoved || glows() || state.cursor)
        relight (*this);
}

bool Step::isPending() const noexcept
{
    return state.drawn != Drawn::off && ! (state.level > 0.0f);
}

bool Step::isFilled() const noexcept
{
    return state.drawn == Drawn::off && state.level > 0.0f;
}

bool Step::isLit() const noexcept
{
    /* A tie holds the step before it on: it is drawn as a bar, not a fill. */
    return state.level > 0.0f && state.drawn != Drawn::tie;
}

float Step::litHeight() const noexcept
{
    if (! isLit())
        return 0.0f;
    const float amount = state.drawn == Drawn::on ? state.amount : 1.0f;
    return juce::jlimit (minLit, 1.0f, amount * state.level);
}

float Step::fillTop() const noexcept
{
    const auto inner = getLocalBounds().toFloat().reduced (hairline);
    return isLit() ? inner.getBottom() - inner.getHeight() * litHeight() : (float) getHeight();
}

juce::Colour Step::numberColourAt (float y) const noexcept
{
    if (isLit() && y >= fillTop())
        return isFilled() ? c::bg000 : c::onUv;
    return state.numberIsControl ? c::ink : c::inkDim;
}

bool Step::glows() const noexcept
{
    /* .on and .play carry glow-led; a step waiting for the fade does not. */
    return (state.play || state.drawn == Drawn::on) && ! isPending();
}

void Step::paint (juce::Graphics& g)
{
    const auto box = getLocalBounds().toFloat();
    const auto inner = box.reduced (hairline);
    const bool pending = isPending();
    const bool on = state.drawn == Drawn::on && ! pending;
    const bool tie = state.drawn == Drawn::tie && ! pending;

    /* THE WELL: bg-200, the playhead's bg-300 -- except under a hole that is
     * still sounding, which keeps the ordinary well -- and a waiting step's
     * bg-100 over either. */
    auto well = c::bg200;
    if (state.play && ! isFilled())
        well = c::bg300;
    if (state.waiting)
        well = c::bg100;
    g.setColour (well);
    g.fillRect (box);

    /* THE BORDER IS WHAT WAS DRAWN: uv for a step that is on, tied or waiting
     * on the fade, and the playhead's; line-200 on the first of a beat that
     * is off; amber for an accent over all of them. */
    auto border = c::line100;
    if (state.beat && state.drawn == Drawn::off)
        border = c::line200;
    if (on || tie || pending || state.play)
        border = c::uv;
    if (state.accent)
        border = c::amber;
    g.setColour (border);
    g.drawRect (box, hairline);

    /* A tie's outline is the hairline doubled inward. */
    if (tie)
    {
        g.setColour (c::uv);
        g.drawRect (inner, hairline);
    }

    /* THE FILL IS WHAT IS HEARD, from the bottom; a hole still sounding at
     * filledAlpha, and the playhead over a lit step as --dip. */
    if (isLit())
    {
        const float alpha = isFilled() ? filledAlpha : 1.0f;
        const auto lit = inner.withTop (fillTop());
        g.setColour (c::uv.withMultipliedAlpha (alpha));
        g.fillRect (lit);
        if (state.play)
        {
            g.setColour (dip().withMultipliedAlpha (alpha));
            g.fillRect (lit);
        }
    }

    if (tie)
    {
        g.setColour (c::uv);
        g.fillRect (inner.withSizeKeepingCentre (inner.getWidth(), 2.0f));
    }

    /* The playhead on a step that is neither on nor tied: a wash, since the
     * step is not on and must not read as uv. */
    if (state.play && ! on && ! tie)
    {
        g.setColour (c::uvGlow.withMultipliedAlpha (0.5f));
        g.fillRect (inner);
    }

    /* THE NUMBER, in the colour of what is behind it: above the fill's top
     * edge in the well's, below it in the fill's. */
    if (state.number > 0)
    {
        const auto text = juce::String (state.number);
        const juce::Rectangle<float> at { 4.0f, 2.0f, box.getWidth() - 4.0f, uv::tok::type::hint.lineHeight };
        const int split = juce::roundToInt (fillTop());
        for (const bool overFill : { false, true })
        {
            const juce::Graphics::ScopedSaveState keep (g);
            if (g.reduceClipRegion (overFill ? getLocalBounds().withTop (split) : getLocalBounds().withBottom (split)))
                uv::type::draw (g, text, at, uv::type::hint(), numberColourAt (overFill ? (float) getHeight() : 0.0f),
                                juce::Justification::centredLeft);
        }
    }
}

void Step::paintLight (juce::Graphics& g)
{
    excludeOwnBounds (g, *this);
    const auto box = getLocalBounds().toFloat();
    if (glows())
        uv::light::glowLed (g, box);
    if (state.cursor)
    {
        /* outline: 1px solid ink; outline-offset: 2px. */
        g.setColour (c::ink);
        g.drawRect (box.expanded (3.0f), hairline);
    }
}

} // namespace ni::ui

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The panel. Panel.h has the card and its two forms.
 */
#include "Panel.h"

#include "Info.h"
#include "Luminous.h"
#include "UvTokens.h"
#include "UvType.h"
#include "WaveSource.h"

#include <cmath>

namespace ni::ui
{

namespace
{
namespace c = uv::tok::colour;

juce::String casedTitle (const juce::String& text)
{
    return uv::type::cased (uv::tok::type::title, text);
}
} // namespace

/*
 * The title as a component of its own, so the pointer can be ON it -- which
 * is where the panel's info line shows -- and so the compact form can turn
 * it a quarter. The panel's group carries its name for assistive technology,
 * so the title itself stays out of the way of a screen reader.
 */
class Panel::Title final : public juce::Component
{
public:
    Title() { setAccessible (false); }

    juce::String text;
    bool upright = true;

    void paint (juce::Graphics& g) override
    {
        const auto font = uv::type::font (uv::tok::type::title);
        const auto box = getLocalBounds().toFloat();

        if (upright)
        {
            uv::type::draw (g, casedTitle (text), box, font, c::inkMuted, juce::Justification::centredLeft);
            return;
        }

        /* writing-mode: vertical-rl, then rotate(180deg): the line runs bottom
         * to top, its glyphs turned a quarter anticlockwise. Drawn upright in a
         * box turned with it. */
        juce::Graphics::ScopedSaveState state (g);
        g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi,
                                                         box.getCentreX(), box.getCentreY()));
        const auto line = juce::Rectangle<float> (box.getHeight(), box.getWidth()).withCentre (box.getCentre());
        uv::type::draw (g, casedTitle (text), line, font, c::inkMuted, juce::Justification::centred);
    }
};

Panel::Panel (const juce::String& t, Form f)
    : titleText (t), form (f), title (std::make_unique<Title>())
{
    setWaveSource (*this);
    title->text = titleText;
    addChildComponent (*title);
    juce::Component::setTitle (titleText);
}

Panel::~Panel() = default;

void Panel::setTitleText (const juce::String& t)
{
    if (t == titleText)
        return;
    titleText = t;
    title->text = t;
    juce::Component::setTitle (t);
    resized();
    repaint();
}

void Panel::setForm (Form f)
{
    if (f == form)
        return;
    form = f;
    resized();
    repaint();
}

void Panel::setTitleInfo (const juce::String& line)
{
    setInfo (*title, line);
    setDescription (line);
}

juce::Component& Panel::titleComponent() noexcept
{
    return *title;
}

juce::Rectangle<int> Panel::contentBounds() const
{
    auto inside = getLocalBounds().reduced (Panel::inset);
    if (titleText.isEmpty())
        return inside;

    if (form == Form::standard)
        return inside.withTrimmedTop (titleLine + titleGap);
    /* The title's 16px column up the left edge, then space-3. */
    return inside.withTrimmedLeft (titleLine + titleGap);
}

void Panel::resized()
{
    title->setVisible (titleText.isNotEmpty());
    title->upright = form == Form::standard;

    const auto inside = getLocalBounds().reduced (Panel::inset);
    if (form == Form::standard)
    {
        title->setBounds (inside.withHeight (titleLine));
        return;
    }

    /* align-self: center -- as long as its words, centred on the height. */
    const int length = (int) std::ceil (uv::type::width (uv::type::font (uv::tok::type::title),
                                                         casedTitle (titleText)));
    title->setBounds (juce::Rectangle<int> (titleLine, juce::jmin (length, inside.getHeight()))
                          .withCentre ({ inside.getX() + titleLine / 2, getHeight() / 2 }));
}

void Panel::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (c::bg100);
    g.fillRect (r);
    g.setColour (c::line100);
    g.drawRect (r, uv::tok::stroke::strokeHair);

    paintChildLights (g, *this);
}

std::unique_ptr<juce::AccessibilityHandler> Panel::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::group);
}

} // namespace ni::ui
